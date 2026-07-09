#include "HotkeyManager.h"

namespace pmf {

bool HotkeyManager::RegisterAll(HWND target, const Hotkeys& hk) {
    bool ok = true;
    ok &= RegisterHotKey(target, kHotkeyToggle, hk.toggleMods | MOD_NOREPEAT, hk.toggleKey) == TRUE;
    ok &= RegisterHotKey(target, kHotkeyNextProfile, hk.nextProfileMods | MOD_NOREPEAT, hk.nextProfileKey) == TRUE;
    ok &= RegisterHotKey(target, kHotkeyPrevProfile, hk.prevProfileMods | MOD_NOREPEAT, hk.prevProfileKey) == TRUE;
    return ok;
}

void HotkeyManager::UnregisterAll(HWND target) {
    UnregisterHotKey(target, kHotkeyToggle);
    UnregisterHotKey(target, kHotkeyNextProfile);
    UnregisterHotKey(target, kHotkeyPrevProfile);
}

} // namespace pmf
