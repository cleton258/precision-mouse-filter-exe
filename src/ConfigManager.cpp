#include "ConfigManager.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>

namespace pmf {
namespace {

std::wstring Trim(const std::wstring& s) {
    size_t start = s.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    size_t end = s.find_last_not_of(L" \t\r\n");
    return s.substr(start, end - start + 1);
}

double GetD(const std::map<std::wstring, std::wstring>& m, const wchar_t* key, double def) {
    auto it = m.find(key);
    if (it == m.end()) return def;
    try { return std::stod(it->second); } catch (...) { return def; }
}

unsigned int GetU(const std::map<std::wstring, std::wstring>& m, const wchar_t* key, unsigned int def) {
    auto it = m.find(key);
    if (it == m.end()) return def;
    try { return static_cast<unsigned int>(std::stoul(it->second)); } catch (...) { return def; }
}

bool ReadKeyValueFile(const std::wstring& path, std::map<std::wstring, std::wstring>& out) {
    std::wifstream file(path.c_str());
    if (!file.is_open()) return false;
    std::wstring line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == L'#') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        out[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
    }
    return true;
}

} // namespace

namespace {
bool ReadSettingsFromPath(const std::wstring& path, FilterSettings& outSettings, Hotkeys& outHotkeys) {
    std::map<std::wstring, std::wstring> kv;
    if (!ReadKeyValueFile(path, kv)) return false;

    FilterSettings s; // start from defaults so missing keys (incl. profiles
                      // saved by older versions) degrade gracefully
    s.filterIntensity = GetD(kv, L"filterIntensity", s.filterIntensity);
    s.smoothingIntensity = GetD(kv, L"smoothingIntensity", s.smoothingIntensity);
    s.sensitivity = GetD(kv, L"sensitivity", s.sensitivity);
    s.straightLineIntensity = GetD(kv, L"straightLineIntensity", s.straightLineIntensity);
    s.antiSpikeSensitivity = GetD(kv, L"antiSpikeSensitivity", s.antiSpikeSensitivity);
    s.antiJitterIntensity = GetD(kv, L"antiJitterIntensity", s.antiJitterIntensity);
    s.snapAngleIntensity = GetD(kv, L"snapAngleIntensity", s.snapAngleIntensity);
    s.highSensitivityFilterIntensity =
        GetD(kv, L"highSensitivityFilterIntensity", s.highSensitivityFilterIntensity);
    s.flickStabilizerIntensity = GetD(kv, L"flickStabilizerIntensity", s.flickStabilizerIntensity);
    s.horizontalStabilizerIntensity =
        GetD(kv, L"horizontalStabilizerIntensity", s.horizontalStabilizerIntensity);
    s.verticalStabilizerIntensity =
        GetD(kv, L"verticalStabilizerIntensity", s.verticalStabilizerIntensity);
    s.motionPredictionIntensity = GetD(kv, L"motionPredictionIntensity", s.motionPredictionIntensity);
    s.adaptiveNoiseReduction = GetD(kv, L"adaptiveNoiseReduction", s.adaptiveNoiseReduction);
    s.accelerationControl = GetD(kv, L"accelerationControl", s.accelerationControl);
    s.responseCurve = static_cast<ResponseCurve>(
        GetU(kv, L"responseCurve", static_cast<unsigned int>(s.responseCurve)));
    s.responseCurveIntensity = GetD(kv, L"responseCurveIntensity", s.responseCurveIntensity);
    s.customCurveExponent = GetD(kv, L"customCurveExponent", s.customCurveExponent);
    s.pollingCompensation = GetD(kv, L"pollingCompensation", s.pollingCompensation);
    s.eventLossCompensation = GetD(kv, L"eventLossCompensation", s.eventLossCompensation);
    s.mouseDpi = GetD(kv, L"mouseDpi", s.mouseDpi);
    s.enabled = GetU(kv, L"enabled", s.enabled ? 1 : 0) != 0;

    Hotkeys h;
    h.toggleMods = GetU(kv, L"toggleMods", h.toggleMods);
    h.toggleKey = GetU(kv, L"toggleKey", h.toggleKey);
    h.nextProfileMods = GetU(kv, L"nextProfileMods", h.nextProfileMods);
    h.nextProfileKey = GetU(kv, L"nextProfileKey", h.nextProfileKey);
    h.prevProfileMods = GetU(kv, L"prevProfileMods", h.prevProfileMods);
    h.prevProfileKey = GetU(kv, L"prevProfileKey", h.prevProfileKey);

    outSettings = s;
    outHotkeys = h;
    return true;
}

bool WriteSettingsToPath(const std::wstring& path, const FilterSettings& s, const Hotkeys& h) {
    std::wofstream file(path.c_str(), std::ios::trunc);
    if (!file.is_open()) return false;
    file << L"# Precision Mouse Filter profile\n";
    file << L"filterIntensity=" << s.filterIntensity << L"\n";
    file << L"smoothingIntensity=" << s.smoothingIntensity << L"\n";
    file << L"sensitivity=" << s.sensitivity << L"\n";
    file << L"straightLineIntensity=" << s.straightLineIntensity << L"\n";
    file << L"antiSpikeSensitivity=" << s.antiSpikeSensitivity << L"\n";
    file << L"antiJitterIntensity=" << s.antiJitterIntensity << L"\n";
    file << L"snapAngleIntensity=" << s.snapAngleIntensity << L"\n";
    file << L"highSensitivityFilterIntensity=" << s.highSensitivityFilterIntensity << L"\n";
    file << L"flickStabilizerIntensity=" << s.flickStabilizerIntensity << L"\n";
    file << L"horizontalStabilizerIntensity=" << s.horizontalStabilizerIntensity << L"\n";
    file << L"verticalStabilizerIntensity=" << s.verticalStabilizerIntensity << L"\n";
    file << L"motionPredictionIntensity=" << s.motionPredictionIntensity << L"\n";
    file << L"adaptiveNoiseReduction=" << s.adaptiveNoiseReduction << L"\n";
    file << L"accelerationControl=" << s.accelerationControl << L"\n";
    file << L"responseCurve=" << static_cast<unsigned int>(s.responseCurve) << L"\n";
    file << L"responseCurveIntensity=" << s.responseCurveIntensity << L"\n";
    file << L"customCurveExponent=" << s.customCurveExponent << L"\n";
    file << L"pollingCompensation=" << s.pollingCompensation << L"\n";
    file << L"eventLossCompensation=" << s.eventLossCompensation << L"\n";
    file << L"mouseDpi=" << s.mouseDpi << L"\n";
    file << L"enabled=" << (s.enabled ? 1 : 0) << L"\n";
    file << L"toggleMods=" << h.toggleMods << L"\n";
    file << L"toggleKey=" << h.toggleKey << L"\n";
    file << L"nextProfileMods=" << h.nextProfileMods << L"\n";
    file << L"nextProfileKey=" << h.nextProfileKey << L"\n";
    file << L"prevProfileMods=" << h.prevProfileMods << L"\n";
    file << L"prevProfileKey=" << h.prevProfileKey << L"\n";
    
    // Verify that write was successful
    if (file.fail()) {
        return false;
    }
    file.close();
    return !file.fail();
}
} // namespace

FilterSettings GetPresetSettings(PresetId id) {
    FilterSettings s; // start from the same neutral baseline as everything else
    switch (id) {
        case PresetId::Competitive:
            // Minimal smoothing/latency; only the essentials to keep aim
            // crisp -- light anti-spike and anti-jitter only.
            s.filterIntensity = 20.0;
            s.smoothingIntensity = 15.0;
            s.straightLineIntensity = 10.0;
            s.antiSpikeSensitivity = 40.0;
            s.antiJitterIntensity = 15.0;
            s.flickStabilizerIntensity = 20.0;
            s.responseCurve = ResponseCurve::Linear;
            break;
        case PresetId::Precision:
            // Favors stability of aim over raw speed-following: more
            // straight-line and snap assistance, moderate smoothing.
            s.filterIntensity = 45.0;
            s.smoothingIntensity = 40.0;
            s.straightLineIntensity = 55.0;
            s.snapAngleIntensity = 25.0;
            s.antiSpikeSensitivity = 50.0;
            s.antiJitterIntensity = 35.0;
            s.motionPredictionIntensity = 10.0;
            break;
        case PresetId::HighSensitivity:
            // Tuned for high-DPI/high-sensitivity setups where small hand
            // tremor turns into large on-screen jitter.
            s.filterIntensity = 55.0;
            s.smoothingIntensity = 45.0;
            s.antiJitterIntensity = 45.0;
            s.highSensitivityFilterIntensity = 70.0;
            s.antiSpikeSensitivity = 55.0;
            s.mouseDpi = 3200.0;
            break;
        case PresetId::PoorSensor:
            // For mice with a noisy/inconsistent sensor or flaky
            // wireless/USB link: leans on adaptive noise reduction and
            // event-loss/polling compensation rather than just raw smoothing.
            s.filterIntensity = 50.0;
            s.smoothingIntensity = 40.0;
            s.antiSpikeSensitivity = 70.0;
            s.antiJitterIntensity = 40.0;
            s.adaptiveNoiseReduction = 70.0;
            s.pollingCompensation = 60.0;
            s.eventLossCompensation = 70.0;
            break;
        case PresetId::MaxStability:
            // Heaviest overall stabilization; noticeably more smoothing
            // latency in exchange for the steadiest possible cursor.
            s.filterIntensity = 75.0;
            s.smoothingIntensity = 70.0;
            s.straightLineIntensity = 70.0;
            s.snapAngleIntensity = 40.0;
            s.antiSpikeSensitivity = 80.0;
            s.antiJitterIntensity = 65.0;
            s.horizontalStabilizerIntensity = 40.0;
            s.verticalStabilizerIntensity = 40.0;
            s.flickStabilizerIntensity = 60.0;
            s.adaptiveNoiseReduction = 40.0;
            break;
    }
    return s;
}

std::vector<std::pair<PresetId, std::wstring>> ListPresets() {
    return {
        {PresetId::Competitive, L"Competitivo"},
        {PresetId::Precision, L"Precisao"},
        {PresetId::HighSensitivity, L"Alta Sensibilidade"},
        {PresetId::PoorSensor, L"Sensor Ruim"},
        {PresetId::MaxStability, L"Estabilidade Maxima"},
    };
}

ConfigManager::ConfigManager() {
    // SHGetFolderPathW + CSIDL_APPDATA (rather than SHGetKnownFolderPath +
    // FOLDERID_RoamingAppData) is used deliberately: the CSIDL constant is a
    // plain integer with no external GUID-storage/import-library concerns,
    // keeping this portable across toolchains with zero extra linking.
    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        baseDir_ = std::wstring(appData) + L"\\PrecisionMouseFilter";
    } else {
        baseDir_ = L"."; // fall back to the working directory if this ever fails
    }
    CreateDirectoryW(baseDir_.c_str(), nullptr);
    CreateDirectoryW(ProfilesDirectory().c_str(), nullptr);
}

std::wstring ConfigManager::ProfilesDirectory() const {
    return baseDir_ + L"\\Profiles";
}

std::wstring ConfigManager::ProfilePath(const std::wstring& name) const {
    return ProfilesDirectory() + L"\\" + name + L".pmfprofile";
}

std::wstring ConfigManager::StateFilePath() const {
    return baseDir_ + L"\\state.txt";
}

std::vector<std::wstring> ConfigManager::ListProfiles() const {
    std::vector<std::wstring> result;
    WIN32_FIND_DATAW findData;
    std::wstring pattern = ProfilesDirectory() + L"\\*.pmfprofile";
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return result;
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring fileName(findData.cFileName);
            size_t dot = fileName.rfind(L".pmfprofile");
            if (dot != std::wstring::npos) {
                result.push_back(fileName.substr(0, dot));
            }
        }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
    std::sort(result.begin(), result.end());
    return result;
}

bool ConfigManager::LoadProfile(const std::wstring& name, FilterSettings& outSettings,
                                 Hotkeys& outHotkeys) const {
    return ReadSettingsFromPath(ProfilePath(name), outSettings, outHotkeys);
}

bool ConfigManager::SaveProfile(const std::wstring& name, const FilterSettings& s,
                                  const Hotkeys& h) const {
    return WriteSettingsToPath(ProfilePath(name), s, h);
}

bool ConfigManager::ExportProfileToFile(const std::wstring& path, const FilterSettings& s,
                                          const Hotkeys& h) const {
    return WriteSettingsToPath(path, s, h);
}

bool ConfigManager::ImportProfileFromFile(const std::wstring& path, FilterSettings& outSettings,
                                            Hotkeys& outHotkeys) const {
    return ReadSettingsFromPath(path, outSettings, outHotkeys);
}

bool ConfigManager::DeleteProfile(const std::wstring& name) const {
    return DeleteFileW(ProfilePath(name).c_str()) == TRUE;
}

std::wstring ConfigManager::GetLastActiveProfile() const {
    std::wifstream file(StateFilePath().c_str());
    if (!file.is_open()) return L"";
    std::wstring name;
    std::getline(file, name);
    return Trim(name);
}

void ConfigManager::SetLastActiveProfile(const std::wstring& name) const {
    std::wofstream file(StateFilePath().c_str(), std::ios::trunc);
    if (file.is_open()) file << name;
}

} // namespace pmf
