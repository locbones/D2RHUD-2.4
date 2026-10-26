#pragma once
#include "../HudLocalization.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace HudOptions {
enum class Category { Display,Gameplay,Items,Sound,Advanced,Workshop };
inline constexpr const char* Categories[]{"Display","Gameplay","Items","Sound","Advanced","Workshop"};
inline constexpr const char* CategoryHelp[]{
    "Display preferences and information shown during play.",
    "Combat rules, summons and character behavior.",
    "Item appearance, picking up items, stack quantities and drops.",
    "Chat notifications and their sounds.",
    "Compatibility settings and combat diagnostics.",
    "Unfinished features for testing and experimentation."
};
inline std::string Lower(std::string_view text){return HudLocale::Fold(text);}
struct Filter {
    Category category=Category::Display;
    char query[128]{};
    int matches=0;
    bool Visible(Category section,std::string_view label,std::string_view keywords={}){
        const auto search=Lower(query);
        if(search.empty()){if(category!=section)return false;}
        else {
            const auto text=Lower(std::string(label)+" "+std::string(keywords)+" "+Categories[int(section)]+" "+
                HudLocale::Translate(label)+" "+HudLocale::Tr(Categories[int(section)]));
            if(text.find(search)==std::string::npos)return false;
        }
        ++matches;return true;
    }
};
}
