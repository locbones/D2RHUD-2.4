#include "HudSoundLibrary.h"
#include "../../ProcessLifetime.h"
#include <Windows.h>
#include <xaudio2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace HudSounds {
namespace {
using Microsoft::WRL::ComPtr;
constexpr DWORD Rate=44100, MaxSeconds=10;
struct Clip { std::vector<BYTE> bytes; WAVEFORMATEX format{}; };
Clip Chime() {
    Clip clip;clip.format={WAVE_FORMAT_PCM,1,Rate,Rate*2,2,16,0};
    const size_t count=Rate/5;clip.bytes.resize(count*2);
    for(size_t i=0;i<count;++i){
        const double t=double(i)/Rate,envelope=std::sin(3.141592653589793*double(i)/count);
        const int16_t sample=int16_t(6000*envelope*envelope*std::sin(2*3.141592653589793*660*t));
        clip.bytes[i*2]=BYTE(sample&255);clip.bytes[i*2+1]=BYTE((uint16_t(sample)>>8)&255);
    }return clip;
}
Clip Decode(const std::string& path) {
    if(path.empty())return Chime();
    const auto file=std::filesystem::path(std::u8string(path.begin(),path.end()));
    std::error_code error;
    if(!std::filesystem::is_regular_file(file,error)||std::filesystem::file_size(file,error)>32*1024*1024||error)return Chime();
    ComPtr<IMFSourceReader> reader;
    if(FAILED(MFCreateSourceReaderFromURL(file.c_str(),nullptr,&reader)))return Chime();
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);
    if(FAILED(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE)))return Chime();
    ComPtr<IMFMediaType> type;
    if(FAILED(MFCreateMediaType(&type)))return Chime();
    type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);type->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM);
    type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);
    if(FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,type.Get())))return Chime();
    if(FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,&type)))return Chime();
    WAVEFORMATEX* wave=nullptr;UINT32 waveSize=0;
    if(FAILED(MFCreateWaveFormatExFromMFMediaType(type.Get(),&wave,&waveSize)))return Chime();
    Clip clip;clip.format=*wave;CoTaskMemFree(wave);
    if(clip.format.wFormatTag!=WAVE_FORMAT_PCM||clip.format.wBitsPerSample!=16||!clip.format.nAvgBytesPerSec||!clip.format.nBlockAlign||!clip.format.nChannels||clip.format.nChannels>2)return Chime();
    const size_t limit=(std::min)(size_t(clip.format.nAvgBytesPerSec)*MaxSeconds,size_t(8*1024*1024));
    // Local files only, bounded decoding and allocation, exclusively on the worker.
    for(size_t samples=0;samples<4096&&clip.bytes.size()<limit;++samples){
        DWORD flags=0;ComPtr<IMFSample> sample;
        if(FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,nullptr,&flags,nullptr,&sample)))return Chime();
        if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)return Chime();
        if(sample){ComPtr<IMFMediaBuffer> buffer;if(FAILED(sample->ConvertToContiguousBuffer(&buffer)))return Chime();
            BYTE* data=nullptr;DWORD length=0;if(FAILED(buffer->Lock(&data,nullptr,&length)))return Chime();
            const size_t count=(std::min)(size_t(length),limit-clip.bytes.size());
            clip.bytes.insert(clip.bytes.end(),data,data+count);buffer->Unlock();
        }if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;
    }
    clip.bytes.resize(clip.bytes.size()/clip.format.nBlockAlign*clip.format.nBlockAlign);
    return clip.bytes.empty()?Chime():clip;
}
struct Audio {
    std::mutex mutex;std::condition_variable wake;
    std::string path;int volume=0;bool pending=false,started=false;
};
Audio& Sound(){return HudLifetime::State<Audio>();}
void Run() {
    const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const bool media=SUCCEEDED(MFStartup(MF_VERSION,MFSTARTUP_LITE));
    ComPtr<IXAudio2> engine;IXAudio2MasteringVoice* master=nullptr;
    std::string cachedPath;std::filesystem::file_time_type cachedStamp{};Clip clip;
    auto& state=Sound();
    for(;;){
        std::string path;int volume;
        {std::unique_lock lock(state.mutex);state.wake.wait(lock,[&]{return state.pending;});
            path=state.path;volume=state.volume;state.pending=false;}
        if(volume<=0)continue;
        try{
            if(!engine){if(FAILED(XAudio2Create(&engine)))continue;if(FAILED(engine->CreateMasteringVoice(&master))){engine.Reset();continue;}}
            std::error_code error;const auto file=std::filesystem::path(std::u8string(path.begin(),path.end()));
            const auto stamp=path.empty()?std::filesystem::file_time_type{}:std::filesystem::last_write_time(file,error);
            if(clip.bytes.empty()||path!=cachedPath||stamp!=cachedStamp){clip=media?Decode(path):Chime();cachedPath=path;cachedStamp=stamp;}
            IXAudio2SourceVoice* voice=nullptr;
            if(FAILED(engine->CreateSourceVoice(&voice,&clip.format)))continue;
            voice->SetVolume(float(volume)/100.f);
            XAUDIO2_BUFFER buffer{};buffer.AudioBytes=UINT32(clip.bytes.size());buffer.pAudioData=clip.bytes.data();buffer.Flags=XAUDIO2_END_OF_STREAM;
            if(SUCCEEDED(voice->SubmitSourceBuffer(&buffer))&&SUCCEEDED(voice->Start())){
                const auto until=GetTickCount64()+MaxSeconds*1000+500;
                for(;;){XAUDIO2_VOICE_STATE status{};voice->GetState(&status,XAUDIO2_VOICE_NOSAMPLESPLAYED);
                    if(!status.BuffersQueued||GetTickCount64()>=until)break;
                    {std::lock_guard lock(state.mutex);if(state.pending||state.volume<=0)break;}Sleep(15);
                }
            }voice->DestroyVoice();
        }catch(...){} // Sound failure must never interrupt gameplay.
    }
    (void)initialized; // Process-lifetime worker: no DLL shutdown join or game-thread wait.
}
}
void PlayNotification(const std::string& path,int volumePercent) {
    auto& state=Sound();
    std::lock_guard lock(state.mutex);
    state.path=path;state.volume=std::clamp(volumePercent,0,100);
    state.pending=state.volume>0;
    if(!state.started&&state.pending){try{std::thread(Run).detach();state.started=true;}catch(...){state.pending=false;return;}}
    state.wake.notify_one();
}
}
