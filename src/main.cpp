#include <windows.h>
#include <commctrl.h>
#include "SharedState.h"
#include "MouseFilterPipeline.h"
#include "InputEngine.h"
#include "ConfigManager.h"
#include "MetricsMonitor.h"
#include "MainWindow.h"

// Note: the Common Controls v6 dependency (for themed trackbars/buttons) is
// declared in resources/app.manifest, which is embedded via resources/app.rc.
// It is deliberately NOT duplicated here via a linker pragma, since that
// would conflict with the manifest embedded from the .rc file.

using namespace pmf;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    SharedState state;
    MouseFilterPipeline pipeline;
    ConfigManager config;

    FilterSettings settings;
    Hotkeys hotkeys;
    std::wstring activeProfile = config.GetLastActiveProfile();
    if (activeProfile.empty() || !config.LoadProfile(activeProfile, settings, hotkeys)) {
        activeProfile = L"Default";
        auto existing = config.ListProfiles();
        bool found = false;
        for (auto& p : existing) if (p == activeProfile) found = true;
        if (found) {
            config.LoadProfile(activeProfile, settings, hotkeys);
        } else {
            config.SaveProfile(activeProfile, settings, hotkeys);
        }
    }
    config.SetLastActiveProfile(activeProfile);
    pipeline.UpdateSettings(settings);

    InputEngine engine(state, pipeline);
    if (!engine.Start()) {
        MessageBoxW(nullptr,
                     L"Falha ao inicializar a captura de Raw Input do mouse.\n"
                     L"O filtro nao sera aplicado, mas a interface ainda funciona.",
                     L"Precision Mouse Filter", MB_ICONERROR);
    }
    state.filterEnabled.store(settings.enabled, std::memory_order_relaxed);

    MetricsMonitor metrics(state);
    metrics.Start();

    MainWindow window(state, pipeline, engine, config, activeProfile, settings, hotkeys);
    if (!window.Create(hInstance, nCmdShow)) {
        metrics.Stop();
        engine.Stop();
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    metrics.Stop();
    engine.Stop();
    return static_cast<int>(msg.wParam);
}
