#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <utility>
#include "MouseFilterPipeline.h"

namespace pmf {

struct Hotkeys {
    UINT toggleMods = MOD_CONTROL | MOD_ALT;
    UINT toggleKey = VK_F9;
    UINT nextProfileMods = MOD_CONTROL | MOD_ALT;
    UINT nextProfileKey = VK_F10;
    UINT prevProfileMods = MOD_CONTROL | MOD_ALT;
    UINT prevProfileKey = VK_F11;
};

// Named, built-in starting points. These are NOT saved profile files -- they
// are computed in code so they can never be accidentally deleted, and so
// they always reflect the current pipeline's parameter meanings.
enum class PresetId {
    Competitive = 0,
    Precision = 1,
    HighSensitivity = 2,
    PoorSensor = 3,
    MaxStability = 4,
};

FilterSettings GetPresetSettings(PresetId id);
std::vector<std::pair<PresetId, std::wstring>> ListPresets();

// Profiles are stored as simple "key=value" text files under
// %APPDATA%\PrecisionMouseFilter\Profiles\<name>.pmfprofile -- no external
// parsing library needed, easy to hand-edit, and trivially portable.
class ConfigManager {
public:
    ConfigManager();

    // Profile names are wstrings (not std::string) so names with accented
    // characters -- e.g. "Padrão", "Precisão" -- work correctly everywhere,
    // since the UI and Win32 file APIs are both Unicode (wide) natively.
    std::vector<std::wstring> ListProfiles() const;
    bool LoadProfile(const std::wstring& name, FilterSettings& outSettings, Hotkeys& outHotkeys) const;
    bool SaveProfile(const std::wstring& name, const FilterSettings& settings, const Hotkeys& hotkeys) const;
    bool DeleteProfile(const std::wstring& name) const;

    // Export/import a profile to/from an arbitrary file path chosen by the
    // user (e.g. via a save/open file dialog), using the same key=value
    // format as regular profiles, so exported files can also be hand-edited
    // or shared between machines.
    bool ExportProfileToFile(const std::wstring& path, const FilterSettings& settings,
                              const Hotkeys& hotkeys) const;
    bool ImportProfileFromFile(const std::wstring& path, FilterSettings& outSettings,
                                Hotkeys& outHotkeys) const;

    std::wstring GetLastActiveProfile() const;
    void SetLastActiveProfile(const std::wstring& name) const;

private:
    std::wstring ProfilesDirectory() const;
    std::wstring ProfilePath(const std::wstring& name) const;
    std::wstring StateFilePath() const;

    std::wstring baseDir_;
};

} // namespace pmf
