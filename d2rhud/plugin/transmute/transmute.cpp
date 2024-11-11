//plugin/autosort/autosort.cpp
#include "transmute.h"
#include <imgui.h>
#include <Windows.h>
#include "../../D2/D2Ptrs.h"
#include "../KeyMappings.h"
#include <iostream>
#include <fstream>
#include <string>


bool Transmute::OnKeyPressed(short key)
{
    std::string filename = "../Launcher/D2RLAN_Config.txt";
    std::string searchString = "Transmute: ";
    std::string result = readTextFollowingString(filename, searchString);

    if (!result.empty()) {
        // Remove any leading or trailing whitespace from the result
        result.erase(result.find_last_not_of(" \n\r\t") + 1);
        result.erase(0, result.find_first_not_of(" \n\r\t"));

        auto it = keyMap.find(result);
        if (it != keyMap.end() && key == it->second) { // Compare key with the mapped key code
            D2CLIENT_Transmute();
            return true; // returning true prevents other plugins from accessing this key press
        }
    }

    return false;
}
