#include "MainWindow.h"
#include "HotkeyManager.h"
#include "resource.h"
#include <commctrl.h>
#include <commdlg.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cwchar>

namespace pmf {

MainWindow* MainWindow::s_instance = nullptr;

namespace {
constexpr wchar_t kClassName[] = L"PrecisionMouseFilter_MainWnd";
constexpr wchar_t kScrollPanelClassName[] = L"PrecisionMouseFilter_ScrollPanel";
constexpr int kTimerStats = 1;
constexpr int kRowHeight = 50;
constexpr int kSliderWidth = 260;
constexpr int kPanelWidth = 420;

enum ControlId {
    kSliderBase = 100,        // 100..100+N-1
    kComboResponseCurve = 190,
    kEditDpi = 191,

    kComboProfile = 200,
    kBtnNew = 201,
    kBtnSave = 202,
    kBtnDelete = 203,
    kBtnEnable = 204,
    kHotkeyBtnBase = 210,      // 210..212
    kComboPreset = 220,
    kBtnApplyPreset = 221,
    kBtnExport = 222,
    kBtnImport = 223,
    kBtnRestoreDefaults = 224,
    kBtnThemeToggle = 225,
    kBtnCalibrate = 230,
    kTabControl = 240,
};

// Data-driven slider list: every entry maps directly to one FilterSettings
// field, so the UI never falls out of sync with what the pipeline actually
// reads. Each tooltip is deliberately short (a sentence or two) rather than
// a full manual page, per the "no bundled documentation" delivery rule --
// this text is UI content, not a docs file.
std::vector<SliderDef> BuildSliderDefs() {
    return {
        {&FilterSettings::filterIntensity, L"Suavizacao principal (jitter)",
         L"Reduz pequenas oscilacoes em repouso. Mais alto = mais suave, com mais lag perceptivel.",
         0, 100, 1.0},
        {&FilterSettings::smoothingIntensity, L"Persistencia da suavizacao",
         L"Por quanto tempo a suavizacao continua durante o movimento, mesmo em velocidade alta.",
         0, 100, 1.0},
        {&FilterSettings::sensitivity, L"Sensibilidade geral",
         L"Multiplicador simples de velocidade do cursor. Nao depende da velocidade do movimento.",
         10, 300, 100.0},
        {&FilterSettings::straightLineIntensity, L"Preservar linha reta",
         L"Amortece o eixo secundario quando o movimento ja e quase reto.",
         0, 100, 1.0},
        {&FilterSettings::antiSpikeSensitivity, L"Anti-spike",
         L"Detecta e limita saltos anormais do sensor (perda de polling, ruido USB, sensor com defeito).",
         0, 100, 1.0},
        {&FilterSettings::antiJitterIntensity, L"Anti-jitter",
         L"Zona-morta dedicada para microtremores; nao remove movimento lento real.",
         0, 100, 1.0},
        {&FilterSettings::snapAngleIntensity, L"Snap angle",
         L"Estabiliza movimentos quase retos, ajustando suavemente para 0/45/90 graus.",
         0, 100, 1.0},
        {&FilterSettings::highSensitivityFilterIntensity, L"Filtro de alta sensibilidade (DPI)",
         L"Estabilizacao extra para configuracoes de DPI elevado.",
         0, 100, 1.0},
        {&FilterSettings::flickStabilizerIntensity, L"Estabilizador de flicks",
         L"Reduz oscilacao/overshoot logo apos um movimento rapido.",
         0, 100, 1.0},
        {&FilterSettings::horizontalStabilizerIntensity, L"Estabilizador horizontal",
         L"Suavizacao extra aplicada apenas ao eixo horizontal.",
         0, 100, 1.0},
        {&FilterSettings::verticalStabilizerIntensity, L"Estabilizador vertical",
         L"Suavizacao extra aplicada apenas ao eixo vertical.",
         0, 100, 1.0},
        {&FilterSettings::motionPredictionIntensity, L"Predicao de movimento",
         L"Extrapola levemente a continuacao do seu proprio movimento; nunca cria movimento sozinho.",
         0, 100, 1.0},
        {&FilterSettings::adaptiveNoiseReduction, L"Reducao adaptativa de ruido",
         L"Amplia a suavizacao automaticamente quando o sensor parece instavel/ruidoso.",
         0, 100, 1.0},
        {&FilterSettings::accelerationControl, L"Controle de aceleracao",
         L"Negativo desacelera movimentos rapidos; positivo os amplifica. Zero = neutro.",
         -100, 100, 1.0},
        {&FilterSettings::responseCurveIntensity, L"Intensidade da curva de resposta",
         L"O quanto a curva escolhida abaixo influencia o ganho por velocidade.",
         0, 100, 1.0},
        {&FilterSettings::pollingCompensation, L"Compensacao de polling rate",
         L"Suaviza variacoes no intervalo entre reports do sensor.",
         0, 100, 1.0},
        {&FilterSettings::eventLossCompensation, L"Compensacao de perda de eventos",
         L"Evita que um report perdido seja lido como uma desaceleracao falsa.",
         0, 100, 1.0},
    };
}

void SetStatic(HWND hwnd, const wchar_t* text) {
    SetWindowTextW(hwnd, text);
}

} // namespace

// Passed by pointer via DialogBoxParamW's lParam to WizardDlgProcStatic,
// which lives for the lifetime of that single modal dialog call only.
struct WizardPhaseData {
    SharedState* state = nullptr;
    std::wstring title;
    std::wstring instruction;
    int totalSeconds = 4;
    int elapsedTicks = 0;
    double jitterSum = 0.0;
    int jitterCount = 0;
    bool canceled = false;
};

MainWindow::MainWindow(SharedState& state, MouseFilterPipeline& pipeline, InputEngine& engine,
                        ConfigManager& config, std::wstring activeProfile,
                        FilterSettings settings, Hotkeys hotkeys)
    : state_(state),
      pipeline_(pipeline),
      engine_(engine),
      config_(config),
      activeProfile_(std::move(activeProfile)),
      settings_(settings),
      hotkeys_(hotkeys) {
    s_instance = this;
    sliderDefs_ = BuildSliderDefs();
}

MainWindow::~MainWindow() {
    if (bgBrush_) DeleteObject(bgBrush_);
    if (s_instance == this) s_instance = nullptr;
}

bool MainWindow::Create(HINSTANCE hInstance, int nCmdShow) {
    hInstance_ = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MainWindow::WndProcStatic;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    WNDCLASSEXW wcPanel{};
    wcPanel.cbSize = sizeof(wcPanel);
    wcPanel.lpfnWndProc = &MainWindow::ScrollPanelProcStatic;
    wcPanel.hInstance = hInstance;
    wcPanel.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcPanel.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wcPanel.lpszClassName = kScrollPanelClassName;
    RegisterClassExW(&wcPanel);

    hwnd_ = CreateWindowExW(
        0, kClassName, L"Precision Mouse Filter",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 680,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd_) return false;

    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                WS_POPUP | TTS_ALWAYSTIP,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                hwnd_, nullptr, hInstance, nullptr);
    SetWindowPos(tooltip_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    CreateTabs(hwnd_);

    RefreshSliderLabels();
    RefreshProfileList();
    ApplyHotkeys();
    ApplyTheme();

    SetTimer(hwnd_, kTimerStats, 250, nullptr);

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
    return true;
}

LRESULT CALLBACK MainWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (s_instance) return s_instance->WndProc(hwnd, msg, w, l);
    return DefWindowProcW(hwnd, msg, w, l);
}

void MainWindow::CreateTabs(HWND hwnd) {
    HINSTANCE hi = hInstance_;

    tabControl_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                   5, 5, 450, 610, hwnd,
                                   reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kTabControl)), hi, nullptr);

    TCITEMW tie{};
    tie.mask = TCIF_TEXT;
    const wchar_t* tabNames[3] = {L"Filtros", L"Diagnostico", L"Perfis"};
    for (int i = 0; i < 3; ++i) {
        tie.pszText = const_cast<wchar_t*>(tabNames[i]);
        TabCtrl_InsertItem(tabControl_, i, &tie);
    }

    RECT display{5, 5, 455, 615};
    TabCtrl_AdjustRect(tabControl_, FALSE, &display);
    // display now holds, relative to tabControl_'s parent (hwnd), the usable
    // content rect below the tab strip.
    int px = display.left, py = display.top;
    int pw = display.right - display.left, ph = display.bottom - display.top;

    pageFilters_ = CreateWindowExW(WS_EX_CONTROLPARENT, kScrollPanelClassName, L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
                                    px, py, pw, ph, hwnd, nullptr, hi, nullptr);
    pageDiagnostics_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_CLIPSIBLINGS,
                                       px, py, pw, ph, hwnd, nullptr, hi, nullptr);
    pageProfiles_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_CLIPSIBLINGS,
                                     px, py, pw, ph, hwnd, nullptr, hi, nullptr);

    CreateFiltersPage(pageFilters_);
    CreateDiagnosticsPage(pageDiagnostics_);
    CreateProfilesPage(pageProfiles_);

    ShowWindow(pageFilters_, SW_SHOW);
    ShowWindow(pageDiagnostics_, SW_HIDE);
    ShowWindow(pageProfiles_, SW_HIDE);
}

void MainWindow::CreateFiltersPage(HWND parent) {
    HINSTANCE hi = hInstance_;
    int y = 10;

    sliders_.resize(sliderDefs_.size());
    sliderLabels_.resize(sliderDefs_.size());

    for (size_t i = 0; i < sliderDefs_.size(); ++i) {
        const SliderDef& def = sliderDefs_[i];
        HWND lbl = CreateWindowExW(0, L"STATIC", def.label, WS_CHILD | WS_VISIBLE,
                                    10, y, kSliderWidth, 16, parent, nullptr, hi, nullptr);
        HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                       WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                       10, y + 18, kSliderWidth, 26, parent,
                                       reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSliderBase + i)),
                                       hi, nullptr);
        SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(def.rangeMin, def.rangeMax));
        HWND valueLbl = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                                         10 + kSliderWidth + 10, y + 18, 90, 20, parent, nullptr, hi, nullptr);

        TOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = parent;
        ti.uId = reinterpret_cast<UINT_PTR>(slider);
        ti.lpszText = const_cast<wchar_t*>(def.tooltip);
        SendMessageW(tooltip_, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&ti));

        sliders_[i] = slider;
        sliderLabels_[i] = valueLbl;
        scrollChildren_.push_back(lbl);
        scrollChildren_.push_back(slider);
        scrollChildren_.push_back(valueLbl);
        y += kRowHeight;
    }

    CreateWindowExW(0, L"STATIC", L"Tipo de curva de resposta:", WS_CHILD | WS_VISIBLE,
                     10, y, 200, 18, parent, nullptr, hi, nullptr);
    comboResponseCurve_ = CreateWindowExW(0, L"COMBOBOX", L"",
                                           WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                           210, y - 2, 150, 100, parent,
                                           reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kComboResponseCurve)),
                                           hi, nullptr);
    const wchar_t* curveNames[4] = {L"Linear", L"Exponencial", L"Suave", L"Personalizada"};
    for (auto* n : curveNames) {
        SendMessageW(comboResponseCurve_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
    }
    scrollChildren_.push_back(comboResponseCurve_);
    y += 34;

    CreateWindowExW(0, L"STATIC", L"DPI do mouse (para normalizar filtros por sensibilidade):",
                     WS_CHILD | WS_VISIBLE, 10, y, kPanelWidth - 20, 18, parent, nullptr, hi, nullptr);
    y += 20;
    editDpi_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"800",
                                WS_CHILD | WS_VISIBLE | ES_NUMBER,
                                10, y, 100, 22, parent,
                                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kEditDpi)), hi, nullptr);
    scrollChildren_.push_back(editDpi_);
    y += 40;

    scrollRange_ = y;
    LayoutScrollPanel();
}

void MainWindow::LayoutScrollPanel() {
    RECT rc;
    GetClientRect(pageFilters_, &rc);
    int pageHeight = rc.bottom - rc.top;

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(scrollRange_, pageHeight);
    si.nPage = static_cast<UINT>(std::max(pageHeight, 1));
    si.nPos = 0;
    SetScrollInfo(pageFilters_, SB_VERT, &si, TRUE);
}

void MainWindow::CreateDiagnosticsPage(HWND parent) {
    HINSTANCE hi = hInstance_;
    int y = 10;
    auto addLine = [&](HWND& target, const wchar_t* initial) {
        target = CreateWindowExW(0, L"STATIC", initial, WS_CHILD | WS_VISIBLE,
                                  10, y, kPanelWidth - 20, 20, parent, nullptr, hi, nullptr);
        y += 26;
    };
    addLine(statPolling_, L"Taxa de polling: --");
    addLine(statLatency_, L"Latencia de processamento: --");
    addLine(statCpu_, L"Uso de CPU (processo): --");
    addLine(statMemory_, L"Uso de memoria (processo): --");
    addLine(statJitter_, L"Jitter (instabilidade): --");
    addLine(statSpikes_, L"Spikes detectados: --");
    addLine(statLostEvents_, L"Eventos perdidos: --");
    addLine(statQuality_, L"Qualidade estimada do sensor: --");
    y += 10;
    addLine(statStatus_, L"");

    y += 10;
    CreateWindowExW(0, L"STATIC",
                     L"O assistente pede 3 movimentos (lento, rapido, circular)\n"
                     L"e sugere uma configuracao inicial com base neles.",
                     WS_CHILD | WS_VISIBLE, 10, y, kPanelWidth - 20, 34, parent, nullptr, hi, nullptr);
    y += 38;
    btnCalibrate_ = CreateWindowExW(0, L"BUTTON", L"Calibrar automaticamente", WS_CHILD | WS_VISIBLE,
                                     10, y, 220, 26, parent,
                                     reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kBtnCalibrate)), hi, nullptr);
}

void MainWindow::CreateProfilesPage(HWND parent) {
    HINSTANCE hi = hInstance_;
    int y = 10;

    CreateWindowExW(0, L"STATIC", L"Perfil:", WS_CHILD | WS_VISIBLE, 10, y + 3, 40, 18, parent, nullptr, hi, nullptr);
    comboProfile_ = CreateWindowExW(0, L"COMBOBOX", L"",
                                     WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                     55, y, 140, 200, parent,
                                     reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kComboProfile)), hi, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Novo", WS_CHILD | WS_VISIBLE,
                    200, y, 60, 24, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kBtnNew)), hi, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Salvar", WS_CHILD | WS_VISIBLE,
                    265, y, 60, 24, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kBtnSave)), hi, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Excluir", WS_CHILD | WS_VISIBLE,
                    330, y, 60, 24, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kBtnDelete)), hi, nullptr);
    y += 34;

    CreateWindowExW(0, L"STATIC", L"Predefinicao:", WS_CHILD | WS_VISIBLE, 10, y + 3, 75, 18, parent, nullptr, hi, nullptr);
    comboPreset_ = CreateWindowExW(0, L"COMBOBOX", L"",
                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                    90, y, 170, 150, parent,
                                    reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kComboPreset)), hi, nullptr);
    for (auto& p : ListPresets()) {
        SendMessageW(comboPreset_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.second.c_str()));
    }
    SendMessageW(comboPreset_, CB_SETCURSEL, 0, 0);
    btnApplyPreset_ = CreateWindowExW(0, L"BUTTON", L"Aplicar", WS_CHILD | WS_VISIBLE,
                                       265, y, 125, 24, parent,
                                       reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kBtnApplyPreset)), hi, nullptr);
    y += 36;

    btnExport_ = CreateWindowExW(0, L"BUTTON", L"Exportar perfil...", WS_CHILD | WS_VISIBLE,
                                  10, y, 135, 26, parent,
                                  reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kBtnExport)), hi, nullptr);
    btnImport_ = CreateWindowExW(0, L"BUTTON", L"Importar perfil...", WS_CHILD | WS_VISIBLE,
                                  150, y, 135, 26, parent,
                                  reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kBtnImport)), hi, nullptr);
    btnRestoreDefaults_ = CreateWindowExW(0, L"BUTTON", L"Restaurar padrao", WS_CHILD | WS_VISIBLE,
                                           290, y, 135, 26, parent,
                                           reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kBtnRestoreDefaults)), hi, nullptr);
    y += 36;

    btnEnable_ = CreateWindowExW(0, L"BUTTON", L"Filtro: ATIVADO", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  10, y, 290, 28, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kBtnEnable)), hi, nullptr);
    btnThemeToggle_ = CreateWindowExW(0, L"BUTTON", L"Tema: Claro", WS_CHILD | WS_VISIBLE,
                                       305, y, 120, 28, parent,
                                       reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kBtnThemeToggle)), hi, nullptr);
    y += 38;

    CreateWindowExW(0, L"STATIC", L"Atalhos (clique em Alterar e pressione a combinacao):",
                     WS_CHILD | WS_VISIBLE, 10, y, kPanelWidth - 20, 16, parent, nullptr, hi, nullptr);
    y += 20;

    const wchar_t* hotkeyNames[3] = {
        L"Ativar/Desativar filtro:", L"Proximo perfil:", L"Perfil anterior:",
    };
    for (int i = 0; i < 3; ++i) {
        CreateWindowExW(0, L"STATIC", hotkeyNames[i], WS_CHILD | WS_VISIBLE,
                         10, y + 3, 150, 18, parent, nullptr, hi, nullptr);
        hotkeyLabels_[i] = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                            165, y + 3, 150, 18, parent, nullptr, hi, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Alterar", WS_CHILD | WS_VISIBLE,
                        330, y, 70, 24, parent,
                        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kHotkeyBtnBase + i)), hi, nullptr);
        y += 30;
    }
}

void MainWindow::OnTabChanged() {
    int sel = TabCtrl_GetCurSel(tabControl_);
    ShowWindow(pageFilters_, sel == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(pageDiagnostics_, sel == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(pageProfiles_, sel == 2 ? SW_SHOW : SW_HIDE);
}

LRESULT CALLBACK MainWindow::ScrollPanelProcStatic(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (s_instance) return s_instance->ScrollPanelProc(hwnd, msg, w, l);
    return DefWindowProcW(hwnd, msg, w, l);
}

LRESULT MainWindow::ScrollPanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_VSCROLL: {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int newPos = si.nPos;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: newPos -= 24; break;
                case SB_LINEDOWN: newPos += 24; break;
                case SB_PAGEUP: newPos -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN: newPos += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: newPos = si.nTrackPos; break;
                default: break;
            }
            int maxPos = std::max(si.nMax - static_cast<int>(si.nPage) + 1, si.nMin);
            newPos = std::clamp(newPos, si.nMin, maxPos);
            int delta = si.nPos - newPos;
            if (delta != 0) {
                si.fMask = SIF_POS;
                si.nPos = newPos;
                SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
                ScrollWindowEx(hwnd, 0, delta, nullptr, nullptr, nullptr, nullptr,
                               SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            for (int i = 0; i < std::abs(notches); ++i) {
                SendMessageW(hwnd, WM_VSCROLL,
                             MAKEWPARAM(notches > 0 ? SB_LINEUP : SB_LINEDOWN, 0), 0);
            }
            return 0;
        }
        case WM_SIZE:
            LayoutScrollPanel();
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void MainWindow::RefreshSliderLabels() {
    for (size_t i = 0; i < sliderDefs_.size(); ++i) {
        double value = settings_.*(sliderDefs_[i].field);
        int pos = static_cast<int>(std::lround(value * sliderDefs_[i].toControlScale));
        SendMessageW(sliders_[i], TBM_SETPOS, TRUE, pos);
        wchar_t buf[64];
        if (sliderDefs_[i].toControlScale != 1.0) {
            swprintf(buf, 64, L"%.2fx", value);
        } else {
            swprintf(buf, 64, L"%.0f / %d", value, sliderDefs_[i].rangeMax);
        }
        SetStatic(sliderLabels_[i], buf);
    }
    SendMessageW(comboResponseCurve_, CB_SETCURSEL, static_cast<int>(settings_.responseCurve), 0);
    wchar_t dpiBuf[32];
    swprintf(dpiBuf, 32, L"%.0f", settings_.mouseDpi);
    SetWindowTextW(editDpi_, dpiBuf);

    ApplySettingsToPipeline();
}

void MainWindow::OnSliderChanged(int index) {
    const SliderDef& def = sliderDefs_[index];
    int pos = static_cast<int>(SendMessageW(sliders_[index], TBM_GETPOS, 0, 0));
    double value = static_cast<double>(pos) / def.toControlScale;
    settings_.*(def.field) = value;

    wchar_t buf[64];
    if (def.toControlScale != 1.0) {
        swprintf(buf, 64, L"%.2fx", value);
    } else {
        swprintf(buf, 64, L"%.0f / %d", value, def.rangeMax);
    }
    SetStatic(sliderLabels_[index], buf);

    ApplySettingsToPipeline();
}

void MainWindow::ApplySettingsToPipeline() {
    pipeline_.UpdateSettings(settings_);
}

void MainWindow::RefreshProfileList() {
    SendMessageW(comboProfile_, CB_RESETCONTENT, 0, 0);
    auto profiles = config_.ListProfiles();
    int selectIndex = -1;
    for (size_t i = 0; i < profiles.size(); ++i) {
        SendMessageW(comboProfile_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(profiles[i].c_str()));
        if (profiles[i] == activeProfile_) selectIndex = static_cast<int>(i);
    }
    if (selectIndex >= 0) {
        SendMessageW(comboProfile_, CB_SETCURSEL, selectIndex, 0);
    }
}

void MainWindow::OnProfileSelected() {
    int sel = static_cast<int>(SendMessageW(comboProfile_, CB_GETCURSEL, 0, 0));
    if (sel < 0) return;
    wchar_t buf[256];
    SendMessageW(comboProfile_, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(buf));
    std::wstring name(buf);

    FilterSettings s;
    Hotkeys h;
    if (config_.LoadProfile(name, s, h)) {
        activeProfile_ = name;
        settings_ = s;
        hotkeys_ = h;
        config_.SetLastActiveProfile(name);
        RefreshSliderLabels();
        ApplyHotkeys();
        RefreshHotkeyLabels();

        SetStatic(btnEnable_, settings_.enabled ? L"Filtro: ATIVADO" : L"Filtro: DESATIVADO");
        state_.filterEnabled.store(settings_.enabled, std::memory_order_relaxed);
    }
}

void MainWindow::OnSaveProfile() {
    config_.SaveProfile(activeProfile_, settings_, hotkeys_);
    MessageBoxW(hwnd_, (L"Perfil \"" + activeProfile_ + L"\" salvo.").c_str(),
                L"Precision Mouse Filter", MB_ICONINFORMATION);
}

void MainWindow::OnNewProfile() {
    std::wstring base = L"Novo Perfil";
    std::wstring name = base;
    auto profiles = config_.ListProfiles();
    int suffix = 2;
    auto exists = [&](const std::wstring& n) {
        return std::find(profiles.begin(), profiles.end(), n) != profiles.end();
    };
    while (exists(name)) {
        name = base + L" " + std::to_wstring(suffix++);
    }
    FilterSettings defaults;
    config_.SaveProfile(name, defaults, hotkeys_);
    activeProfile_ = name;
    settings_ = defaults;
    config_.SetLastActiveProfile(name);
    RefreshProfileList();
    RefreshSliderLabels();
}

void MainWindow::OnDeleteProfile() {
    auto profiles = config_.ListProfiles();
    if (profiles.size() <= 1) {
        MessageBoxW(hwnd_, L"Nao e possivel excluir o unico perfil restante.",
                    L"Precision Mouse Filter", MB_ICONWARNING);
        return;
    }
    config_.DeleteProfile(activeProfile_);
    RefreshProfileList();
    OnProfileSelected();
}

void MainWindow::OnToggleEnabled() {
    settings_.enabled = !settings_.enabled;
    state_.filterEnabled.store(settings_.enabled, std::memory_order_relaxed);
    SetStatic(btnEnable_, settings_.enabled ? L"Filtro: ATIVADO" : L"Filtro: DESATIVADO");
    ApplySettingsToPipeline();
}

void MainWindow::OnApplyPreset() {
    int sel = static_cast<int>(SendMessageW(comboPreset_, CB_GETCURSEL, 0, 0));
    if (sel < 0) return;
    auto presets = ListPresets();
    if (sel >= static_cast<int>(presets.size())) return;

    FilterSettings preset = GetPresetSettings(presets[sel].first);
    // Presets only override the filter parameters -- never the user's
    // chosen flat sensitivity, DPI, or enabled/disabled state.
    double keepSensitivity = settings_.sensitivity;
    double keepDpi = settings_.mouseDpi;
    bool keepEnabled = settings_.enabled;
    settings_ = preset;
    settings_.sensitivity = keepSensitivity;
    settings_.mouseDpi = keepDpi;
    settings_.enabled = keepEnabled;

    RefreshSliderLabels();
}

void MainWindow::OnExportProfile() {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Perfil Precision Mouse Filter (*.pmfprofile)\0*.pmfprofile\0Todos os arquivos\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"pmfprofile";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&ofn)) {
        if (config_.ExportProfileToFile(path, settings_, hotkeys_)) {
            MessageBoxW(hwnd_, L"Perfil exportado com sucesso.", L"Precision Mouse Filter", MB_ICONINFORMATION);
        } else {
            MessageBoxW(hwnd_, L"Falha ao exportar o perfil.", L"Precision Mouse Filter", MB_ICONERROR);
        }
    }
}

void MainWindow::OnImportProfile() {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Perfil Precision Mouse Filter (*.pmfprofile)\0*.pmfprofile\0Todos os arquivos\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        FilterSettings s;
        Hotkeys h;
        if (config_.ImportProfileFromFile(path, s, h)) {
            settings_ = s;
            hotkeys_ = h;
            RefreshSliderLabels();
            ApplyHotkeys();
            RefreshHotkeyLabels();
            SetStatic(btnEnable_, settings_.enabled ? L"Filtro: ATIVADO" : L"Filtro: DESATIVADO");
            state_.filterEnabled.store(settings_.enabled, std::memory_order_relaxed);
            MessageBoxW(hwnd_, L"Perfil importado. Clique em Salvar para gravar no perfil atual.",
                        L"Precision Mouse Filter", MB_ICONINFORMATION);
        } else {
            MessageBoxW(hwnd_, L"Falha ao importar o arquivo selecionado.", L"Precision Mouse Filter", MB_ICONERROR);
        }
    }
}

void MainWindow::OnRestoreDefaults() {
    if (MessageBoxW(hwnd_, L"Restaurar todos os valores para o padrao de fabrica?",
                     L"Precision Mouse Filter", MB_ICONQUESTION | MB_YESNO) != IDYES) {
        return;
    }
    double keepSensitivity = settings_.sensitivity;
    double keepDpi = settings_.mouseDpi;
    settings_ = FilterSettings{};
    settings_.sensitivity = keepSensitivity;
    settings_.mouseDpi = keepDpi;
    RefreshSliderLabels();
}

void MainWindow::OnToggleTheme() {
    darkTheme_ = !darkTheme_;
    ApplyTheme();
    SetStatic(btnThemeToggle_, darkTheme_ ? L"Tema: Escuro" : L"Tema: Claro");
}

void MainWindow::ApplyTheme() {
    if (bgBrush_) {
        DeleteObject(bgBrush_);
        bgBrush_ = nullptr;
    }
    COLORREF bg = darkTheme_ ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE);
    bgBrush_ = CreateSolidBrush(bg);
    // Practical, best-effort theming: recolors the window/static backgrounds
    // and text. Standard Win32 trackbars/buttons keep their native (light)
    // chrome, since re-skinning them fully requires owner-draw for every
    // control -- a much larger change than the sliders/values themselves.
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::OnRunCalibration() {
    if (MessageBoxW(hwnd_,
            L"O assistente vai pedir 3 movimentos (lento, rapido, circular), de poucos segundos "
            L"cada, e sugerir uma configuracao inicial com base neles.\n\nDeseja iniciar?",
            L"Calibracao automatica", MB_ICONQUESTION | MB_YESNO) != IDYES) {
        return;
    }

    struct Phase { const wchar_t* title; const wchar_t* instr; };
    const Phase phases[3] = {
        {L"Fase 1 de 3: Movimento lento", L"Mova o mouse bem devagar, de forma continua, por alguns segundos."},
        {L"Fase 2 de 3: Movimento rapido", L"Faca movimentos rapidos (flicks) de um lado para o outro."},
        {L"Fase 3 de 3: Movimento circular", L"Desenhe circulos continuos com o mouse."},
    };

    double avgJitter[3] = {0.0, 0.0, 0.0};
    uint64_t spikeDelta[3] = {0, 0, 0};
    uint64_t lostDelta[3] = {0, 0, 0};
    double peakSpeed[3] = {0.0, 0.0, 0.0};
    bool aborted = false;

    for (int i = 0; i < 3 && !aborted; ++i) {
        uint64_t startSpikes = state_.totalSpikeCount.load(std::memory_order_relaxed);
        uint64_t startLost = state_.totalLostEventCount.load(std::memory_order_relaxed);
        state_.peakSpeedCountsPerSec.store(0.0, std::memory_order_relaxed);

        WizardPhaseData data;
        data.state = &state_;
        data.title = phases[i].title;
        data.instruction = phases[i].instr;
        data.totalSeconds = 4;

        INT_PTR r = DialogBoxParamW(hInstance_, MAKEINTRESOURCEW(IDD_WIZARD_STEP), hwnd_,
                                     &MainWindow::WizardDlgProcStatic,
                                     reinterpret_cast<LPARAM>(&data));
        if (r != IDOK || data.canceled) {
            aborted = true;
            break;
        }

        avgJitter[i] = data.jitterCount > 0 ? (data.jitterSum / data.jitterCount) : 0.0;
        spikeDelta[i] = state_.totalSpikeCount.load(std::memory_order_relaxed) - startSpikes;
        lostDelta[i] = state_.totalLostEventCount.load(std::memory_order_relaxed) - startLost;
        peakSpeed[i] = state_.peakSpeedCountsPerSec.load(std::memory_order_relaxed);
    }

    if (aborted) {
        MessageBoxW(hwnd_, L"Calibracao cancelada. Nenhuma alteracao foi aplicada.",
                    L"Calibracao automatica", MB_ICONINFORMATION);
        return;
    }

    // Heuristic mapping from measured phase statistics to slider values.
    // These are principled starting points to be fine-tuned afterward, not
    // a claim of a single scientifically optimal fit for every mouse/hand.
    double restJitter = avgJitter[0];
    double avgSpikesPerPhase = (spikeDelta[0] + spikeDelta[1] + spikeDelta[2]) / 3.0;
    double avgLostPerPhase = (lostDelta[0] + lostDelta[1] + lostDelta[2]) / 3.0;
    double fastPeakSpeed = peakSpeed[1];

    FilterSettings s = settings_;
    s.filterIntensity = std::clamp(30.0 + restJitter * 200.0, 15.0, 80.0);
    s.antiJitterIntensity = std::clamp(restJitter * 180.0, 0.0, 75.0);
    s.antiSpikeSensitivity = std::clamp(35.0 + avgSpikesPerPhase * 8.0, 30.0, 90.0);
    s.eventLossCompensation = std::clamp(avgLostPerPhase * 15.0, 0.0, 85.0);
    s.pollingCompensation = std::clamp(avgLostPerPhase * 10.0, 0.0, 70.0);
    s.highSensitivityFilterIntensity = fastPeakSpeed > 3000.0 ? 55.0 : 20.0;
    s.flickStabilizerIntensity = fastPeakSpeed > 3000.0 ? 45.0 : 20.0;
    s.snapAngleIntensity = std::clamp(avgJitter[2] * 100.0, 10.0, 40.0);
    s.straightLineIntensity = 35.0;

    settings_ = s;
    RefreshSliderLabels();

    if (MessageBoxW(hwnd_,
                     L"Calibracao concluida e aplicada. Deseja salvar como um novo perfil \"Calibrado\"?",
                     L"Calibracao automatica", MB_ICONQUESTION | MB_YESNO) == IDYES) {
        std::wstring name = L"Calibrado";
        auto profiles = config_.ListProfiles();
        int suffix = 2;
        while (std::find(profiles.begin(), profiles.end(), name) != profiles.end()) {
            name = L"Calibrado " + std::to_wstring(suffix++);
        }
        config_.SaveProfile(name, settings_, hotkeys_);
        activeProfile_ = name;
        config_.SetLastActiveProfile(name);
        RefreshProfileList();
    }
}

INT_PTR CALLBACK MainWindow::HotkeyDlgProcStatic(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG:
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, static_cast<LONG_PTR>(lParam));
            SetFocus(hDlg);
            return TRUE;
        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            auto* result = reinterpret_cast<HotkeyCaptureResult*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            if (result) {
                result->vk = static_cast<UINT>(wParam);
                result->mods = 0;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) result->mods |= MOD_CONTROL;
                if (GetAsyncKeyState(VK_MENU) & 0x8000) result->mods |= MOD_ALT;
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) result->mods |= MOD_SHIFT;
                result->captured = true;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            return FALSE;
        case WM_CLOSE:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK MainWindow::WizardDlgProcStatic(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto* data = reinterpret_cast<WizardPhaseData*>(lParam);
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, static_cast<LONG_PTR>(lParam));
            SetDlgItemTextW(hDlg, IDC_WIZARD_TITLE, data->title.c_str());
            SetDlgItemTextW(hDlg, IDC_WIZARD_INSTR, data->instruction.c_str());
            wchar_t buf[64];
            swprintf(buf, 64, L"%d s restantes", data->totalSeconds);
            SetDlgItemTextW(hDlg, IDC_WIZARD_COUNTDOWN, buf);
            SetTimer(hDlg, 1, 100, nullptr);
            return TRUE;
        }
        case WM_TIMER: {
            auto* data = reinterpret_cast<WizardPhaseData*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            if (!data) return TRUE;
            data->elapsedTicks++;
            double jitter = data->state->jitterScore.load(std::memory_order_relaxed);
            data->jitterSum += jitter;
            data->jitterCount++;

            int elapsedSec = data->elapsedTicks / 10;
            int remaining = data->totalSeconds - elapsedSec;
            if (remaining < 0) remaining = 0;
            wchar_t buf[64];
            swprintf(buf, 64, L"%d s restantes", remaining);
            SetDlgItemTextW(hDlg, IDC_WIZARD_COUNTDOWN, buf);

            if (elapsedSec >= data->totalSeconds) {
                KillTimer(hDlg, 1);
                EndDialog(hDlg, IDOK);
            }
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL) {
                auto* data = reinterpret_cast<WizardPhaseData*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
                if (data) data->canceled = true;
                KillTimer(hDlg, 1);
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            return FALSE;
        case WM_CLOSE:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

void MainWindow::OnChangeHotkey(int which) {
    HotkeyCaptureResult result;
    INT_PTR r = DialogBoxParamW(hInstance_, MAKEINTRESOURCEW(IDD_HOTKEY_CAPTURE), hwnd_,
                                 &MainWindow::HotkeyDlgProcStatic,
                                 reinterpret_cast<LPARAM>(&result));
    if (r != IDOK || !result.captured) return;

    HotkeyManager::UnregisterAll(hwnd_);
    switch (which) {
        case 0: hotkeys_.toggleMods = result.mods; hotkeys_.toggleKey = result.vk; break;
        case 1: hotkeys_.nextProfileMods = result.mods; hotkeys_.nextProfileKey = result.vk; break;
        case 2: hotkeys_.prevProfileMods = result.mods; hotkeys_.prevProfileKey = result.vk; break;
    }
    ApplyHotkeys();
    RefreshHotkeyLabels();
}

void MainWindow::ApplyHotkeys() {
    HotkeyManager::UnregisterAll(hwnd_);
    if (!HotkeyManager::RegisterAll(hwnd_, hotkeys_)) {
        MessageBoxW(hwnd_,
                    L"Um ou mais atalhos ja estao em uso por outro programa. "
                    L"Escolha combinacoes diferentes.",
                    L"Precision Mouse Filter", MB_ICONWARNING);
    }
    RefreshHotkeyLabels();
}

std::wstring MainWindow::HotkeyToString(UINT mods, UINT vk) const {
    std::wstring s;
    if (mods & MOD_CONTROL) s += L"Ctrl+";
    if (mods & MOD_ALT) s += L"Alt+";
    if (mods & MOD_SHIFT) s += L"Shift+";
    wchar_t name[64] = {0};
    if (vk >= VK_F1 && vk <= VK_F24) {
        swprintf(name, 64, L"F%d", static_cast<int>(vk - VK_F1 + 1));
    } else {
        UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        LONG lParamValue = static_cast<LONG>(scanCode << 16);
        if (GetKeyNameTextW(lParamValue, name, 64) == 0) {
            swprintf(name, 64, L"0x%02X", vk);
        }
    }
    s += name;
    return s;
}

void MainWindow::RefreshHotkeyLabels() {
    SetStatic(hotkeyLabels_[0], HotkeyToString(hotkeys_.toggleMods, hotkeys_.toggleKey).c_str());
    SetStatic(hotkeyLabels_[1], HotkeyToString(hotkeys_.nextProfileMods, hotkeys_.nextProfileKey).c_str());
    SetStatic(hotkeyLabels_[2], HotkeyToString(hotkeys_.prevProfileMods, hotkeys_.prevProfileKey).c_str());
}

void MainWindow::RefreshStats() {
    wchar_t buf[160];
    double polling = state_.pollingRateHz.load(std::memory_order_relaxed);
    double latency = state_.avgProcessingLatencyUs.load(std::memory_order_relaxed);
    double cpu = state_.cpuUsagePercent.load(std::memory_order_relaxed);
    double memory = state_.memoryUsageMB.load(std::memory_order_relaxed);
    double jitter = state_.jitterScore.load(std::memory_order_relaxed);

    swprintf(buf, 160, L"Taxa de polling: %.0f Hz", polling);
    SetStatic(statPolling_, buf);
    swprintf(buf, 160, L"Latencia de processamento: %.3f ms", latency / 1000.0);
    SetStatic(statLatency_, buf);
    swprintf(buf, 160, L"Uso de CPU (processo): %.1f%%", cpu);
    SetStatic(statCpu_, buf);
    swprintf(buf, 160, L"Uso de memoria (processo): %.1f MB", memory);
    SetStatic(statMemory_, buf);
    swprintf(buf, 160, L"Jitter (instabilidade): %.0f%%", jitter * 100.0);
    SetStatic(statJitter_, buf);

    ULONGLONG nowMs = GetTickCount64();
    uint64_t spikes = state_.totalSpikeCount.load(std::memory_order_relaxed);
    uint64_t lost = state_.totalLostEventCount.load(std::memory_order_relaxed);
    if (prevStatsTickMs_ != 0) {
        double elapsedMin = static_cast<double>(nowMs - prevStatsTickMs_) / 60000.0;
        if (elapsedMin > 0.0) {
            spikesPerMinute_ = static_cast<double>(spikes - prevSpikeCount_) / elapsedMin;
            lostEventsPerMinute_ = static_cast<double>(lost - prevLostEventCount_) / elapsedMin;
        }
    }
    prevSpikeCount_ = spikes;
    prevLostEventCount_ = lost;
    prevStatsTickMs_ = nowMs;

    swprintf(buf, 160, L"Spikes detectados: %llu totais (~%.1f/min)",
             static_cast<unsigned long long>(spikes), spikesPerMinute_);
    SetStatic(statSpikes_, buf);
    swprintf(buf, 160, L"Eventos perdidos: %llu totais (~%.1f/min)",
             static_cast<unsigned long long>(lost), lostEventsPerMinute_);
    SetStatic(statLostEvents_, buf);

    double quality = 100.0 - std::clamp(jitter * 40.0 + spikesPerMinute_ * 2.0 + lostEventsPerMinute_ * 3.0,
                                         0.0, 100.0);
    swprintf(buf, 160, L"Qualidade estimada do sensor: %.0f%%", quality);
    SetStatic(statQuality_, buf);

    int64_t lastMs = state_.lastInputTickMs.load(std::memory_order_relaxed);
    int64_t nowMs64 = static_cast<int64_t>(GetTickCount64());
    bool recentInput = lastMs != 0 && (nowMs64 - lastMs) < 2000;
    bool enabled = state_.filterEnabled.load(std::memory_order_relaxed);

    if (!enabled) {
        SetStatic(statStatus_, L"Status: filtro desativado (passthrough)");
    } else if (recentInput) {
        SetStatic(statStatus_, L"Status: capturando e filtrando normalmente");
    } else {
        SetStatic(statStatus_, L"Status: ativo (aguardando movimento do mouse)");
    }
}

LRESULT MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_HSCROLL: {
            HWND src = reinterpret_cast<HWND>(lParam);
            for (size_t i = 0; i < sliders_.size(); ++i) {
                if (sliders_[i] == src) { OnSliderChanged(static_cast<int>(i)); break; }
            }
            return 0;
        }
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr->hwndFrom == tabControl_ && hdr->code == TCN_SELCHANGE) {
                OnTabChanged();
            }
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int notify = HIWORD(wParam);
            if (id == kComboProfile && notify == CBN_SELCHANGE) {
                OnProfileSelected();
            } else if (id == kComboResponseCurve && notify == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessageW(comboResponseCurve_, CB_GETCURSEL, 0, 0));
                if (sel >= 0) {
                    settings_.responseCurve = static_cast<ResponseCurve>(sel);
                    ApplySettingsToPipeline();
                }
            } else if (id == kEditDpi && notify == EN_CHANGE) {
                wchar_t buf[32];
                GetWindowTextW(editDpi_, buf, 32);
                double dpi = wcstod(buf, nullptr);
                if (dpi >= 100.0 && dpi <= 32000.0) {
                    settings_.mouseDpi = dpi;
                    ApplySettingsToPipeline();
                }
            } else if (id == kBtnNew) {
                OnNewProfile();
            } else if (id == kBtnSave) {
                OnSaveProfile();
            } else if (id == kBtnDelete) {
                OnDeleteProfile();
            } else if (id == kBtnEnable) {
                OnToggleEnabled();
            } else if (id == kBtnApplyPreset) {
                OnApplyPreset();
            } else if (id == kBtnExport) {
                OnExportProfile();
            } else if (id == kBtnImport) {
                OnImportProfile();
            } else if (id == kBtnRestoreDefaults) {
                OnRestoreDefaults();
            } else if (id == kBtnThemeToggle) {
                OnToggleTheme();
            } else if (id == kBtnCalibrate) {
                OnRunCalibration();
            } else if (id >= kHotkeyBtnBase && id < kHotkeyBtnBase + 3) {
                OnChangeHotkey(id - kHotkeyBtnBase);
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORDLG: {
            if (!darkTheme_) break;
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, RGB(230, 230, 230));
            SetBkColor(hdc, RGB(32, 32, 32));
            SetBkMode(hdc, OPAQUE);
            return reinterpret_cast<LRESULT>(bgBrush_);
        }
        case WM_HOTKEY: {
            switch (wParam) {
                case kHotkeyToggle:
                    OnToggleEnabled();
                    break;
                case kHotkeyNextProfile: {
                    int count = static_cast<int>(SendMessageW(comboProfile_, CB_GETCOUNT, 0, 0));
                    if (count > 0) {
                        int sel = static_cast<int>(SendMessageW(comboProfile_, CB_GETCURSEL, 0, 0));
                        sel = (sel + 1) % count;
                        SendMessageW(comboProfile_, CB_SETCURSEL, sel, 0);
                        OnProfileSelected();
                    }
                    break;
                }
                case kHotkeyPrevProfile: {
                    int count = static_cast<int>(SendMessageW(comboProfile_, CB_GETCOUNT, 0, 0));
                    if (count > 0) {
                        int sel = static_cast<int>(SendMessageW(comboProfile_, CB_GETCURSEL, 0, 0));
                        sel = (sel - 1 + count) % count;
                        SendMessageW(comboProfile_, CB_SETCURSEL, sel, 0);
                        OnProfileSelected();
                    }
                    break;
                }
            }
            return 0;
        }
        case WM_TIMER:
            if (wParam == kTimerStats) RefreshStats();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kTimerStats);
            HotkeyManager::UnregisterAll(hwnd);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace pmf
