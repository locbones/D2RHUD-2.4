#define NOMINMAX
#include "../d2rhud/plugin/D2RHUD/NotificationSound.cpp"
#include <cassert>
#include <fstream>
#include <iostream>

int main(int argc,char** argv){
    assert(argc==2);
    const auto root=std::filesystem::path(argv[1])/std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(root);
    const auto chime=HudSounds::Chime();
    assert(chime.format.wFormatTag==WAVE_FORMAT_PCM&&chime.format.nBlockAlign==2&&chime.bytes.size()==44100/5*2);
    int peak=0;for(size_t i=0;i<chime.bytes.size();i+=2)peak=(std::max)(peak,std::abs(int(int16_t(uint16_t(chime.bytes[i])|uint16_t(chime.bytes[i+1])<<8))));
    assert(peak>1000&&peak<=6000); // Gentle default, no full-scale system beep.
    const auto file=root/L"notification-音.wav";
    {std::ofstream out(file,std::ios::binary);auto number=[&](uint32_t n,size_t count){for(size_t i=0;i<count;++i)out.put(char(n>>(i*8)));};
        out.write("RIFF",4);number(uint32_t(chime.bytes.size()+36),4);out.write("WAVEfmt ",8);number(16,4);number(1,2);number(1,2);number(44100,4);number(88200,4);number(2,2);number(16,2);out.write("data",4);number(uint32_t(chime.bytes.size()),4);out.write(reinterpret_cast<const char*>(chime.bytes.data()),chime.bytes.size());}
    assert(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)));
    assert(SUCCEEDED(MFStartup(MF_VERSION,MFSTARTUP_LITE)));
    const auto u8=file.u8string();const auto decoded=HudSounds::Decode(std::string(u8.begin(),u8.end()));
    assert(decoded.format.nSamplesPerSec==44100&&decoded.bytes==chime.bytes);
    assert(HudSounds::Decode("").bytes==chime.bytes);
    assert(HudSounds::Decode("C:/missing-notification-fixture.wav").bytes==chime.bytes);
    {std::ofstream out(file,std::ios::binary|std::ios::trunc);out<<"Invalid sound";}
    assert(HudSounds::Decode(std::string(u8.begin(),u8.end())).bytes==chime.bytes);
    // Muting doesn't initialize an audio device or a background thread.
    HudSounds::PlayNotification("",0);assert(!HudSounds::Sound().started&&!HudSounds::Sound().pending);
    HudSounds::PlayNotification("",-15);assert(HudSounds::Sound().volume==0&&!HudSounds::Sound().started);
    MFShutdown();CoUninitialize();std::filesystem::remove_all(root);
    std::cout<<"Notification audio: real PCM decoding, Unicode paths, bounded quiet fallback, corrupt/missing files and silent mute passed; no audible playback.\n";
}
