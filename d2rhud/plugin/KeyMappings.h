#pragma once
#ifndef KEY_MAPPINGS_H
#define KEY_MAPPINGS_H

#include <unordered_map>
#include <string>
#include <iostream>
#include <fstream>

// Declaration of readTextFollowingString function
extern std::string readTextFollowingString(const std::string& filename, const std::string& searchString);

extern std::unordered_map<std::string, short> keyMap;

#endif // KEY_MAPPINGS_H
