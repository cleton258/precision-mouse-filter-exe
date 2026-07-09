#pragma once
#include <windows.h>
#include "ConfigManager.h"

namespace pmf {

enum HotkeyId : int {
    kHotkeyToggle = 1,
    kHotkeyNextProfile = 2,
    kHotkeyPrevProfile = 3,
};

// Registers/unregisters the three global hotkeys (toggle filter, next
// profile, previous profile) against a target window. Because these are
// standard Windows global hotkeys (WM_HOTKEY messages), they work
// system-wide and are keyboard-only -- they function even if the mouse
// cursor were ever stuck, giving the user a reliable way to disable the
// filter no matter what.
class HotkeyManager {
public:
    static bool RegisterAll(HWND target, const Hotkeys& hk);
    static void UnregisterAll(HWND target);
};

} // namespace pmf
