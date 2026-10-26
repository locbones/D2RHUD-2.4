#pragma once
#include "HudLocalization.h"
#include <imgui.h>
#include <utility>

// Translate display text while retaining original widget IDs and stored values.
namespace HudText {
inline ImFont* fallbackFont=nullptr;
inline ImFont* FontFor(ImFont* font,const std::string& text){
    if(!font || !fallbackFont)return font;
    for(size_t i=0;i<text.size();){
        const auto c=static_cast<unsigned char>(text[i++]);unsigned point=c;
        int more=c<0x80?0:c<0xe0?1:c<0xf0?2:3;
        if(more){point=c&((1u<<(6-more))-1);while(more--&&i<text.size())point=(point<<6)|(static_cast<unsigned char>(text[i++])&63);}
        if(point>=128 && point<=0xffff && !font->FindGlyphNoFallback(ImWchar(point)))return fallbackFont;
    }return font;
}
inline const char* Arg(const char* value){return HudLocale::Tr(value);}
template<class T>T Arg(T value){return value;}
inline bool Begin(const char* name, bool* p_open = NULL, ImGuiWindowFlags flags = 0){return ImGui::Begin(name,p_open,flags);}
inline void SeparatorText(const char* label){ImGui::SeparatorText(HudLocale::Label(label).c_str());}
inline bool Button(const char* label, const ImVec2& size = ImVec2(0, 0)){return ImGui::Button(HudLocale::Label(label).c_str(),size);}
inline bool SmallButton(const char* label){return ImGui::SmallButton(HudLocale::Label(label).c_str());}
inline bool Checkbox(const char* label, bool* v){return ImGui::Checkbox(HudLocale::Label(label).c_str(),v);}
inline bool CheckboxFlags(const char* label, int* flags, int flags_value){return ImGui::CheckboxFlags(HudLocale::Label(label).c_str(),flags,flags_value);}
inline bool CheckboxFlags(const char* label, unsigned int* flags, unsigned int flags_value){return ImGui::CheckboxFlags(HudLocale::Label(label).c_str(),flags,flags_value);}
inline bool RadioButton(const char* label, bool active){return ImGui::RadioButton(HudLocale::Label(label).c_str(),active);}
inline bool RadioButton(const char* label, int* v, int v_button){return ImGui::RadioButton(HudLocale::Label(label).c_str(),v,v_button);}
inline bool BeginCombo(const char* label, const char* preview_value, ImGuiComboFlags flags = 0){return ImGui::BeginCombo(HudLocale::Label(label).c_str(),HudLocale::Tr(preview_value),flags);}
inline bool DragFloat(const char* label, float* v, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0){return ImGui::DragFloat(HudLocale::Label(label).c_str(),v,v_speed,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool DragFloat2(const char* label, float v[2], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0){return ImGui::DragFloat2(HudLocale::Label(label).c_str(),v,v_speed,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool DragFloat3(const char* label, float v[3], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0){return ImGui::DragFloat3(HudLocale::Label(label).c_str(),v,v_speed,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool DragFloat4(const char* label, float v[4], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0){return ImGui::DragFloat4(HudLocale::Label(label).c_str(),v,v_speed,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool DragInt(const char* label, int* v, float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0){return ImGui::DragInt(HudLocale::Label(label).c_str(),v,v_speed,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool DragInt2(const char* label, int v[2], float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0){return ImGui::DragInt2(HudLocale::Label(label).c_str(),v,v_speed,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool DragInt3(const char* label, int v[3], float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0){return ImGui::DragInt3(HudLocale::Label(label).c_str(),v,v_speed,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool DragInt4(const char* label, int v[4], float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0){return ImGui::DragInt4(HudLocale::Label(label).c_str(),v,v_speed,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0){return ImGui::SliderFloat(HudLocale::Label(label).c_str(),v,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool SliderFloat2(const char* label, float v[2], float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0){return ImGui::SliderFloat2(HudLocale::Label(label).c_str(),v,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool SliderFloat3(const char* label, float v[3], float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0){return ImGui::SliderFloat3(HudLocale::Label(label).c_str(),v,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool SliderFloat4(const char* label, float v[4], float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0){return ImGui::SliderFloat4(HudLocale::Label(label).c_str(),v,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool SliderAngle(const char* label, float* v_rad, float v_degrees_min = -360.0f, float v_degrees_max = +360.0f, const char* format = "%.0f deg", ImGuiSliderFlags flags = 0){return ImGui::SliderAngle(HudLocale::Label(label).c_str(),v_rad,v_degrees_min,v_degrees_max,HudLocale::Tr(format),flags);}
inline bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d", ImGuiSliderFlags flags = 0){return ImGui::SliderInt(HudLocale::Label(label).c_str(),v,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool SliderInt2(const char* label, int v[2], int v_min, int v_max, const char* format = "%d", ImGuiSliderFlags flags = 0){return ImGui::SliderInt2(HudLocale::Label(label).c_str(),v,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool SliderInt3(const char* label, int v[3], int v_min, int v_max, const char* format = "%d", ImGuiSliderFlags flags = 0){return ImGui::SliderInt3(HudLocale::Label(label).c_str(),v,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool SliderInt4(const char* label, int v[4], int v_min, int v_max, const char* format = "%d", ImGuiSliderFlags flags = 0){return ImGui::SliderInt4(HudLocale::Label(label).c_str(),v,v_min,v_max,HudLocale::Tr(format),flags);}
inline bool SliderScalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format = NULL, ImGuiSliderFlags flags = 0){return ImGui::SliderScalar(HudLocale::Label(label).c_str(),data_type,p_data,p_min,p_max,HudLocale::Tr(format),flags);}
inline bool InputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = NULL, void* user_data = NULL){return ImGui::InputText(HudLocale::Label(label).c_str(),buf,buf_size,flags,callback,user_data);}
inline bool InputTextMultiline(const char* label, char* buf, size_t buf_size, const ImVec2& size = ImVec2(0, 0), ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = NULL, void* user_data = NULL){return ImGui::InputTextMultiline(HudLocale::Label(label).c_str(),buf,buf_size,size,flags,callback,user_data);}
inline bool InputTextWithHint(const char* label, const char* hint, char* buf, size_t buf_size, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = NULL, void* user_data = NULL){return ImGui::InputTextWithHint(HudLocale::Label(label).c_str(),HudLocale::Tr(hint),buf,buf_size,flags,callback,user_data);}
inline bool InputFloat(const char* label, float* v, float step = 0.0f, float step_fast = 0.0f, const char* format = "%.3f", ImGuiInputTextFlags flags = 0){return ImGui::InputFloat(HudLocale::Label(label).c_str(),v,step,step_fast,HudLocale::Tr(format),flags);}
inline bool InputFloat2(const char* label, float v[2], const char* format = "%.3f", ImGuiInputTextFlags flags = 0){return ImGui::InputFloat2(HudLocale::Label(label).c_str(),v,HudLocale::Tr(format),flags);}
inline bool InputFloat3(const char* label, float v[3], const char* format = "%.3f", ImGuiInputTextFlags flags = 0){return ImGui::InputFloat3(HudLocale::Label(label).c_str(),v,HudLocale::Tr(format),flags);}
inline bool InputFloat4(const char* label, float v[4], const char* format = "%.3f", ImGuiInputTextFlags flags = 0){return ImGui::InputFloat4(HudLocale::Label(label).c_str(),v,HudLocale::Tr(format),flags);}
inline bool InputInt(const char* label, int* v, int step = 1, int step_fast = 100, ImGuiInputTextFlags flags = 0){return ImGui::InputInt(HudLocale::Label(label).c_str(),v,step,step_fast,flags);}
inline bool InputInt2(const char* label, int v[2], ImGuiInputTextFlags flags = 0){return ImGui::InputInt2(HudLocale::Label(label).c_str(),v,flags);}
inline bool InputInt3(const char* label, int v[3], ImGuiInputTextFlags flags = 0){return ImGui::InputInt3(HudLocale::Label(label).c_str(),v,flags);}
inline bool InputInt4(const char* label, int v[4], ImGuiInputTextFlags flags = 0){return ImGui::InputInt4(HudLocale::Label(label).c_str(),v,flags);}
inline bool InputDouble(const char* label, double* v, double step = 0.0, double step_fast = 0.0, const char* format = "%.6f", ImGuiInputTextFlags flags = 0){return ImGui::InputDouble(HudLocale::Label(label).c_str(),v,step,step_fast,HudLocale::Tr(format),flags);}
inline bool InputScalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_step = NULL, const void* p_step_fast = NULL, const char* format = NULL, ImGuiInputTextFlags flags = 0){return ImGui::InputScalar(HudLocale::Label(label).c_str(),data_type,p_data,p_step,p_step_fast,HudLocale::Tr(format),flags);}
inline bool ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags = 0){return ImGui::ColorEdit3(HudLocale::Label(label).c_str(),col,flags);}
inline bool ColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags = 0){return ImGui::ColorEdit4(HudLocale::Label(label).c_str(),col,flags);}
inline bool ColorPicker3(const char* label, float col[3], ImGuiColorEditFlags flags = 0){return ImGui::ColorPicker3(HudLocale::Label(label).c_str(),col,flags);}
inline bool ColorPicker4(const char* label, float col[4], ImGuiColorEditFlags flags = 0, const float* ref_col = NULL){return ImGui::ColorPicker4(HudLocale::Label(label).c_str(),col,flags,ref_col);}
inline bool TreeNode(const char* label){return ImGui::TreeNode(HudLocale::Label(label).c_str());}
inline bool TreeNodeEx(const char* label, ImGuiTreeNodeFlags flags = 0){return ImGui::TreeNodeEx(HudLocale::Label(label).c_str(),flags);}
inline bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0){return ImGui::CollapsingHeader(HudLocale::Label(label).c_str(),flags);}
inline bool CollapsingHeader(const char* label, bool* p_visible, ImGuiTreeNodeFlags flags = 0){return ImGui::CollapsingHeader(HudLocale::Label(label).c_str(),p_visible,flags);}
inline bool Selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0, 0)){return ImGui::Selectable(HudLocale::Label(label).c_str(),selected,flags,size);}
inline bool Selectable(const char* label, bool* p_selected, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0, 0)){return ImGui::Selectable(HudLocale::Label(label).c_str(),p_selected,flags,size);}
inline bool BeginMenu(const char* label, bool enabled = true){return ImGui::BeginMenu(HudLocale::Label(label).c_str(),enabled);}
inline bool MenuItem(const char* label, const char* shortcut = NULL, bool selected = false, bool enabled = true){return ImGui::MenuItem(HudLocale::Label(label).c_str(),HudLocale::Tr(shortcut),selected,enabled);}
inline bool MenuItem(const char* label, const char* shortcut, bool* p_selected, bool enabled = true){return ImGui::MenuItem(HudLocale::Label(label).c_str(),HudLocale::Tr(shortcut),p_selected,enabled);}
inline bool BeginPopupModal(const char* name, bool* p_open = NULL, ImGuiWindowFlags flags = 0){return ImGui::BeginPopupModal(HudLocale::Label(name).c_str(),p_open,flags);}
inline void OpenPopup(const char* id,ImGuiPopupFlags flags=0){ImGui::OpenPopup(HudLocale::Label(id).c_str(),flags);}
inline void OpenPopup(ImGuiID id,ImGuiPopupFlags flags=0){ImGui::OpenPopup(id,flags);}
inline bool BeginPopup(const char* id,ImGuiWindowFlags flags=0){return ImGui::BeginPopup(HudLocale::Label(id).c_str(),flags);}
inline bool IsPopupOpen(const char* id,ImGuiPopupFlags flags=0){return ImGui::IsPopupOpen(id?HudLocale::Label(id).c_str():nullptr,flags);}
inline void TableSetupColumn(const char* label, ImGuiTableColumnFlags flags = 0, float init_width_or_weight = 0.0f, ImGuiID user_id = 0){ImGui::TableSetupColumn(HudLocale::Label(label).c_str(),flags,init_width_or_weight,user_id);}
inline bool BeginTabItem(const char* label, bool* p_open = NULL, ImGuiTabItemFlags flags = 0){return ImGui::BeginTabItem(HudLocale::Label(label).c_str(),p_open,flags);}
template<class... A>void Text(const char* format,A... args){ImGui::Text(HudLocale::Tr(format),Arg(args)...);}
template<class... A>void TextColored(const ImVec4& color,const char* format,A... args){ImGui::TextColored(color,HudLocale::Tr(format),Arg(args)...);}
template<class... A>void TextDisabled(const char* format,A... args){ImGui::TextDisabled(HudLocale::Tr(format),Arg(args)...);}
template<class... A>void TextWrapped(const char* format,A... args){ImGui::TextWrapped(HudLocale::Tr(format),Arg(args)...);}
template<class... A>void BulletText(const char* format,A... args){ImGui::BulletText(HudLocale::Tr(format),Arg(args)...);}
template<class... A>void SetTooltip(const char* format,A... args){ImGui::SetTooltip(HudLocale::Tr(format),Arg(args)...);}
inline void TextUnformatted(const char* text,const char* end=nullptr){
    if(end){const auto translated=HudLocale::Translate(std::string_view(text,size_t(end-text)));ImGui::TextUnformatted(translated.c_str());}
    else ImGui::TextUnformatted(HudLocale::Tr(text));
}
inline ImVec2 CalcTextSize(const char* text,const char* end=nullptr,bool hide=false,float wrap=-1){
    if(end){const auto translated=HudLocale::Translate(std::string_view(text,size_t(end-text)));return ImGui::CalcTextSize(translated.c_str(),nullptr,hide,wrap);}
    return ImGui::CalcTextSize(HudLocale::Tr(text),nullptr,hide,wrap);
}
inline bool Combo(const char* label,int* selected,const char* const values[],int count,int height=-1){
    std::vector<const char*> translated;for(int i=0;i<count;++i)translated.push_back(HudLocale::Tr(values[i]));
    return ImGui::Combo(HudLocale::Label(label).c_str(),selected,translated.data(),count,height);
}
inline bool Combo(const char* label,int* selected,const char* values,int height=-1){
    std::vector<const char*> translated;for(auto p=values;p&&*p;p+=strlen(p)+1)translated.push_back(HudLocale::Tr(p));
    return ImGui::Combo(HudLocale::Label(label).c_str(),selected,translated.data(),int(translated.size()),height);
}
inline bool Combo(const char* label,int* selected,const char* (*getter)(void*,int),void* data,int count,int height=-1){
    std::vector<const char*> translated;for(int i=0;i<count;++i)translated.push_back(HudLocale::Tr(getter(data,i)));
    return ImGui::Combo(HudLocale::Label(label).c_str(),selected,translated.data(),count,height);
}
inline bool Combo(const char* label,int* selected,bool (*getter)(void*,int,const char**),void* data,int count,int height=-1){
    std::vector<const char*> translated;for(int i=0;i<count;++i){const char* value="";getter(data,i,&value);translated.push_back(HudLocale::Tr(value));}
    return ImGui::Combo(HudLocale::Label(label).c_str(),selected,translated.data(),count,height);
}
inline void ProgressBar(float fraction,const ImVec2& size=ImVec2(-FLT_MIN,0),const char* overlay=nullptr){ImGui::ProgressBar(fraction,size,overlay?HudLocale::Tr(overlay):nullptr);}
inline void AddText(ImDrawList* draw,const ImVec2& pos,ImU32 color,const char* text,const char* end=nullptr){
    const auto translated=HudLocale::Visible(end?std::string_view(text,size_t(end-text)):std::string_view(text));draw->AddText(pos,color,translated.c_str());
}
inline void AddText(ImDrawList* draw,ImFont* font,float size,const ImVec2& pos,ImU32 color,const char* text,const char* end=nullptr,float wrap=0,const ImVec4* clip=nullptr){
    const auto translated=HudLocale::Visible(end?std::string_view(text,size_t(end-text)):std::string_view(text));draw->AddText(FontFor(font,translated),size,pos,color,translated.c_str(),nullptr,wrap,clip);
}
inline ImVec2 CalcTextSizeA(ImFont* font,float size,float maximum,float wrap,const char* text,const char* end=nullptr){
    const auto translated=HudLocale::Visible(end?std::string_view(text,size_t(end-text)):std::string_view(text));
    return FontFor(font,translated)->CalcTextSizeA(size,maximum,wrap,translated.c_str());
}
}
