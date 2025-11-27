#include "KeyMappings.h"
#include <windows.h>

// Definition of readTextFollowingString function
std::string readTextFollowingString(const std::string& filename, const std::string& searchString) {
    std::ifstream infile(filename);

    if (!infile) {
        std::cerr << "Error opening file." << std::endl;
        return "";
    }

    std::string line;
    std::size_t pos;

    while (std::getline(infile, line)) {
        pos = line.find(searchString);
        if (pos != std::string::npos) {
            return line.substr(pos + searchString.length());
        }
    }

    return "";
}

std::unordered_map<std::string, short> keyMap = {

    // --- Mouse ---
    {"VK_LBUTTON", VK_LBUTTON},
    {"VK_RBUTTON", VK_RBUTTON},
    {"VK_MBUTTON", VK_MBUTTON},
    {"VK_XBUTTON1", VK_XBUTTON1},
    {"VK_XBUTTON2", VK_XBUTTON2},

    // --- Control ---
    {"VK_CANCEL", VK_CANCEL},
    {"VK_BACK", VK_BACK},
    {"VK_TAB", VK_TAB},
    {"VK_CLEAR", VK_CLEAR},
    {"VK_RETURN", VK_RETURN},
    {"VK_SHIFT", VK_SHIFT},
    {"VK_CONTROL", VK_CONTROL},
    {"VK_MENU", VK_MENU}, // ALT

    // --- System ---
    {"VK_PAUSE", VK_PAUSE},
    {"VK_CAPITAL", VK_CAPITAL},
    {"VK_ESCAPE", VK_ESCAPE},

    // --- IME ---
    {"VK_KANA", VK_KANA},
    {"VK_HANGUL", VK_HANGUL},
    {"VK_JUNJA", VK_JUNJA},
    {"VK_FINAL", VK_FINAL},
    {"VK_HANJA", VK_HANJA},
    {"VK_KANJI", VK_KANJI},
    {"VK_CONVERT", VK_CONVERT},
    {"VK_NONCONVERT", VK_NONCONVERT},
    {"VK_ACCEPT", VK_ACCEPT},
    {"VK_MODECHANGE", VK_MODECHANGE},
    {"VK_IME_ON", VK_IME_ON},
    {"VK_IME_OFF", VK_IME_OFF},

    // --- Navigation ---
    {"VK_SPACE", VK_SPACE},
    {"VK_PRIOR", VK_PRIOR},   // Page Up
    {"VK_NEXT", VK_NEXT},     // Page Down
    {"VK_END", VK_END},
    {"VK_HOME", VK_HOME},
    {"VK_LEFT", VK_LEFT},
    {"VK_UP", VK_UP},
    {"VK_RIGHT", VK_RIGHT},
    {"VK_DOWN", VK_DOWN},

    // --- Misc ---
    {"VK_SELECT", VK_SELECT},
    {"VK_PRINT", VK_PRINT},
    {"VK_EXECUTE", VK_EXECUTE},
    {"VK_SNAPSHOT", VK_SNAPSHOT},
    {"VK_INSERT", VK_INSERT},
    {"VK_DELETE", VK_DELETE},
    {"VK_HELP", VK_HELP},

    // --- Windows keys ---
    {"VK_LWIN", VK_LWIN},
    {"VK_RWIN", VK_RWIN},
    {"VK_APPS", VK_APPS},
    {"VK_SLEEP", VK_SLEEP},

    // --- Numpad ---
    {"VK_NUMPAD0", VK_NUMPAD0},
    {"VK_NUMPAD1", VK_NUMPAD1},
    {"VK_NUMPAD2", VK_NUMPAD2},
    {"VK_NUMPAD3", VK_NUMPAD3},
    {"VK_NUMPAD4", VK_NUMPAD4},
    {"VK_NUMPAD5", VK_NUMPAD5},
    {"VK_NUMPAD6", VK_NUMPAD6},
    {"VK_NUMPAD7", VK_NUMPAD7},
    {"VK_NUMPAD8", VK_NUMPAD8},
    {"VK_NUMPAD9", VK_NUMPAD9},
    {"VK_MULTIPLY", VK_MULTIPLY},
    {"VK_ADD", VK_ADD},
    {"VK_SEPARATOR", VK_SEPARATOR},
    {"VK_SUBTRACT", VK_SUBTRACT},
    {"VK_DECIMAL", VK_DECIMAL},
    {"VK_DIVIDE", VK_DIVIDE},

    // --- Function keys ---
    {"VK_F1", VK_F1}, {"VK_F2", VK_F2}, {"VK_F3", VK_F3}, {"VK_F4", VK_F4},
    {"VK_F5", VK_F5}, {"VK_F6", VK_F6}, {"VK_F7", VK_F7}, {"VK_F8", VK_F8},
    {"VK_F9", VK_F9}, {"VK_F10", VK_F10}, {"VK_F11", VK_F11}, {"VK_F12", VK_F12},
    {"VK_F13", VK_F13}, {"VK_F14", VK_F14}, {"VK_F15", VK_F15}, {"VK_F16", VK_F16},
    {"VK_F17", VK_F17}, {"VK_F18", VK_F18}, {"VK_F19", VK_F19}, {"VK_F20", VK_F20},
    {"VK_F21", VK_F21}, {"VK_F22", VK_F22}, {"VK_F23", VK_F23}, {"VK_F24", VK_F24},

    // --- Lock keys ---
    {"VK_NUMLOCK", VK_NUMLOCK},
    {"VK_SCROLL", VK_SCROLL},

    // --- Browser ---
    {"VK_BROWSER_BACK", VK_BROWSER_BACK},
    {"VK_BROWSER_FORWARD", VK_BROWSER_FORWARD},
    {"VK_BROWSER_REFRESH", VK_BROWSER_REFRESH},
    {"VK_BROWSER_STOP", VK_BROWSER_STOP},
    {"VK_BROWSER_SEARCH", VK_BROWSER_SEARCH},
    {"VK_BROWSER_FAVORITES", VK_BROWSER_FAVORITES},
    {"VK_BROWSER_HOME", VK_BROWSER_HOME},

    // --- Volume / Media ---
    {"VK_VOLUME_MUTE", VK_VOLUME_MUTE},
    {"VK_VOLUME_DOWN", VK_VOLUME_DOWN},
    {"VK_VOLUME_UP", VK_VOLUME_UP},
    {"VK_MEDIA_NEXT_TRACK", VK_MEDIA_NEXT_TRACK},
    {"VK_MEDIA_PREV_TRACK", VK_MEDIA_PREV_TRACK},
    {"VK_MEDIA_STOP", VK_MEDIA_STOP},
    {"VK_MEDIA_PLAY_PAUSE", VK_MEDIA_PLAY_PAUSE},

    // --- Launch keys ---
    {"VK_LAUNCH_MAIL", VK_LAUNCH_MAIL},
    {"VK_LAUNCH_MEDIA_SELECT", VK_LAUNCH_MEDIA_SELECT},
    {"VK_LAUNCH_APP1", VK_LAUNCH_APP1},
    {"VK_LAUNCH_APP2", VK_LAUNCH_APP2},

    // --- OEM Keys ---
    {"VK_OEM_1", VK_OEM_1},
    {"VK_OEM_PLUS", VK_OEM_PLUS},
    {"VK_OEM_COMMA", VK_OEM_COMMA},
    {"VK_OEM_MINUS", VK_OEM_MINUS},
    {"VK_OEM_PERIOD", VK_OEM_PERIOD},
    {"VK_OEM_2", VK_OEM_2},
    {"VK_OEM_3", VK_OEM_3},
    {"VK_OEM_4", VK_OEM_4},
    {"VK_OEM_5", VK_OEM_5},
    {"VK_OEM_6", VK_OEM_6},
    {"VK_OEM_7", VK_OEM_7},
    {"VK_OEM_8", VK_OEM_8},
    {"VK_OEM_102", VK_OEM_102},

    // --- Special OEM ---
    {"VK_OEM_AX", VK_OEM_AX},
    {"VK_OEM_NEC_EQUAL", VK_OEM_NEC_EQUAL},
    {"VK_OEM_FJ_JISHO", VK_OEM_FJ_JISHO},
    {"VK_OEM_FJ_MASSHOU", VK_OEM_FJ_MASSHOU},
    {"VK_OEM_FJ_TOUROKU", VK_OEM_FJ_TOUROKU},
    {"VK_OEM_FJ_LOYA", VK_OEM_FJ_LOYA},
    {"VK_OEM_FJ_ROYA", VK_OEM_FJ_ROYA},

    // --- Modifiers ---
    {"VK_LSHIFT", VK_LSHIFT},
    {"VK_RSHIFT", VK_RSHIFT},
    {"VK_LCONTROL", VK_LCONTROL},
    {"VK_RCONTROL", VK_RCONTROL},
    {"VK_LMENU", VK_LMENU},
    {"VK_RMENU", VK_RMENU},

    // --- Extra ---
    {"VK_PROCESSKEY", VK_PROCESSKEY},
    {"VK_PACKET", VK_PACKET},
    {"VK_ATTN", VK_ATTN},
    {"VK_CRSEL", VK_CRSEL},
    {"VK_EXSEL", VK_EXSEL},
    {"VK_EREOF", VK_EREOF},
    {"VK_PLAY", VK_PLAY},
    {"VK_ZOOM", VK_ZOOM},
    {"VK_NONAME", VK_NONAME},
    {"VK_PA1", VK_PA1},
    {"VK_OEM_CLEAR", VK_OEM_CLEAR},

    // --- Numeric Row ---
    {"VK_0", 0x30}, {"VK_1", 0x31}, {"VK_2", 0x32}, {"VK_3", 0x33},
    {"VK_4", 0x34}, {"VK_5", 0x35}, {"VK_6", 0x36}, {"VK_7", 0x37},
    {"VK_8", 0x38}, {"VK_9", 0x39},

    // --- A–Z ---
    {"VK_A", 0x41}, {"VK_B", 0x42}, {"VK_C", 0x43}, {"VK_D", 0x44},
    {"VK_E", 0x45}, {"VK_F", 0x46}, {"VK_G", 0x47}, {"VK_H", 0x48},
    {"VK_I", 0x49}, {"VK_J", 0x4A}, {"VK_K", 0x4B}, {"VK_L", 0x4C},
    {"VK_M", 0x4D}, {"VK_N", 0x4E}, {"VK_O", 0x4F}, {"VK_P", 0x50},
    {"VK_Q", 0x51}, {"VK_R", 0x52}, {"VK_S", 0x53}, {"VK_T", 0x54},
    {"VK_U", 0x55}, {"VK_V", 0x56}, {"VK_W", 0x57}, {"VK_X", 0x58},
    {"VK_Y", 0x59}, {"VK_Z", 0x5A},

    // --- Gamepad (ALL) ---
    {"VK_GAMEPAD_A", VK_GAMEPAD_A},
    {"VK_GAMEPAD_B", VK_GAMEPAD_B},
    {"VK_GAMEPAD_X", VK_GAMEPAD_X},
    {"VK_GAMEPAD_Y", VK_GAMEPAD_Y},
    {"VK_GAMEPAD_RIGHT_SHOULDER", VK_GAMEPAD_RIGHT_SHOULDER},
    {"VK_GAMEPAD_LEFT_SHOULDER", VK_GAMEPAD_LEFT_SHOULDER},
    {"VK_GAMEPAD_LEFT_TRIGGER", VK_GAMEPAD_LEFT_TRIGGER},
    {"VK_GAMEPAD_RIGHT_TRIGGER", VK_GAMEPAD_RIGHT_TRIGGER},
    {"VK_GAMEPAD_DPAD_UP", VK_GAMEPAD_DPAD_UP},
    {"VK_GAMEPAD_DPAD_DOWN", VK_GAMEPAD_DPAD_DOWN},
    {"VK_GAMEPAD_DPAD_LEFT", VK_GAMEPAD_DPAD_LEFT},
    {"VK_GAMEPAD_DPAD_RIGHT", VK_GAMEPAD_DPAD_RIGHT},
    {"VK_GAMEPAD_MENU", VK_GAMEPAD_MENU},
    {"VK_GAMEPAD_VIEW", VK_GAMEPAD_VIEW},
    {"VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON", VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON},
    {"VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON", VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON},
    {"VK_GAMEPAD_LEFT_THUMBSTICK_UP", VK_GAMEPAD_LEFT_THUMBSTICK_UP},
    {"VK_GAMEPAD_LEFT_THUMBSTICK_DOWN", VK_GAMEPAD_LEFT_THUMBSTICK_DOWN},
    {"VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT", VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT},
    {"VK_GAMEPAD_LEFT_THUMBSTICK_LEFT", VK_GAMEPAD_LEFT_THUMBSTICK_LEFT},
    {"VK_GAMEPAD_RIGHT_THUMBSTICK_UP", VK_GAMEPAD_RIGHT_THUMBSTICK_UP},
    {"VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN", VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN},
    {"VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT", VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT},
    {"VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT", VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT},
};


