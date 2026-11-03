#pragma once
#include <algorithm>
#include <cstring>
#include <string_view>

namespace ItemText {
// Capacities include the null byte. Do not leave an incomplete UTF-8 codepoint
// when a translated name needs more bytes than its native destination allows.
inline void Copy(char* destination,size_t capacity,std::string_view text) {
    if (!destination || !capacity) return;
    text = text.substr(0,text.find('\0'));
    size_t count = (std::min)(capacity-1,text.size());
    if (count < text.size())
        while (count && (static_cast<unsigned char>(text[count]) & 0xc0) == 0x80) --count;
    if (count) memmove(destination,text.data(),count);
    destination[count] = '\0';
}
}
