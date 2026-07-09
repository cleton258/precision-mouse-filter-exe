#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "SharedState.h"
#include "MouseFilterPipeline.h"
#include "InputEngine.h"
#include "ConfigManager.h"

namespace pmf {

struct HotkeyCaptureResult {
    UINT vk = 0;
    UINT mods = 0;
    bool captured = false;
};

// One entry per slider on the "Filtros" tab. Data-driven so the tab can hold
// all pipeline parameters without one hand-written control block each.
struct SliderDef {
    double FilterSettings::* field;
    const wchar_t* label;
    const wchar_t* tooltip;
    int rangeMin;
    int rangeMax;
    double toControlScale; // control position = value * scale (e.g. 100 for a 0..1 multiplier)
};

class MainWindow {
public:
    MainWindow(SharedState& state, MouseFilterPipeline& pipeline, InputEngine& engine,
               ConfigManager& config, std::wstring activeProfile,
               FilterSettings settings, Hotkeys hotkeys);
    ~MainWindow();

    bool Create(HINSTANCE hInstance, int nCmdShow);

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);
    static INT_PTR CALLBACK HotkeyDlgProcStatic(HWND, UINT, WPARAM, LPARAM);
    static INT_PTR CALLBACK WizardDlgProcStatic(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK ScrollPanelProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT ScrollPanelProc(HWND, UINT, WPARAM, LPARAM);

    void CreateTabs(HWND hwnd);
    void CreateFiltersPage(HWND parent);
    void CreateDiagnosticsPage(HWND parent);
    void CreateProfilesPage(HWND parent);
    void OnTabChanged();
    void LayoutScrollPanel();

    void OnSliderChanged(int index);
    void RefreshSliderLabels();
    void ApplySettingsToPipeline();
    void RefreshProfileList();
    void OnProfileSelected();
    void OnSaveProfile();
    void OnNewProfile();
    void OnDeleteProfile();
    void OnToggleEnabled();
    void OnChangeHotkey(int which); // 0=toggle, 1=next, 2=prev
    void ApplyHotkeys();
    void RefreshHotkeyLabels();
    void RefreshStats();
    std::wstring HotkeyToString(UINT mods, UINT vk) const;

    void OnApplyPreset();
    void OnExportProfile();
    void OnImportProfile();
    void OnRestoreDefaults();
    void OnToggleTheme();
    void ApplyTheme();
    void OnRunCalibration();

    SharedState& state_;
    MouseFilterPipeline& pipeline_;
    InputEngine& engine_;
    ConfigManager& config_;
    std::wstring activeProfile_;
    FilterSettings settings_;
    Hotkeys hotkeys_;

    HINSTANCE hInstance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND tabControl_ = nullptr;
    HWND pageFilters_ = nullptr;   // scroll panel (custom class)
    HWND pageDiagnostics_ = nullptr;
    HWND pageProfiles_ = nullptr;
    HWND tooltip_ = nullptr;

    std::vector<SliderDef> sliderDefs_;
    std::vector<HWND> sliders_;
    std::vector<HWND> sliderLabels_;
    HWND comboResponseCurve_ = nullptr;
    HWND editDpi_ = nullptr;

    HWND comboProfile_ = nullptr;
    HWND btnEnable_ = nullptr;
    HWND hotkeyLabels_[3]{};

    HWND statPolling_ = nullptr;
    HWND statLatency_ = nullptr;
    HWND statCpu_ = nullptr;
    HWND statMemory_ = nullptr;
    HWND statJitter_ = nullptr;
    HWND statSpikes_ = nullptr;
    HWND statLostEvents_ = nullptr;
    HWND statQuality_ = nullptr;
    HWND statStatus_ = nullptr;
    HWND btnCalibrate_ = nullptr;

    HWND comboPreset_ = nullptr;
    HWND btnApplyPreset_ = nullptr;
    HWND btnExport_ = nullptr;
    HWND btnImport_ = nullptr;
    HWND btnRestoreDefaults_ = nullptr;
    HWND btnThemeToggle_ = nullptr;

    // Diagnostics rate tracking (spikes/lost events per minute), sampled
    // from SharedState's cumulative counters on each stats refresh tick.
    uint64_t prevSpikeCount_ = 0;
    uint64_t prevLostEventCount_ = 0;
    ULONGLONG prevStatsTickMs_ = 0;
    double spikesPerMinute_ = 0.0;
    double lostEventsPerMinute_ = 0.0;

    bool darkTheme_ = false;
    HBRUSH bgBrush_ = nullptr;

    // Scroll panel state for the (potentially tall) Filtros page.
    int scrollPos_ = 0;
    int scrollRange_ = 0;
    std::vector<HWND> scrollChildren_; // repositioned together when scrolling

    static MainWindow* s_instance;
};

} // namespace pmf
