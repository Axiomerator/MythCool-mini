// SPDX-License-Identifier: MPL-2.0
#include "mini.h"
#include "resource.h"
#include <algorithm>
#include <commctrl.h>
#include <cstdio>
#include <fstream>
#include <memory>
#include <shellapi.h>
using namespace mini;
namespace {
constexpr wchar_t cls[] = L"MythCoolMiniWindow";
constexpr UINT tray_message = WM_APP + 1, exit_message = WM_APP + 2;
constexpr int ID_LIST = 100, ID_RELOAD = 101, ID_PAUSE = 102, ID_SLIDER = 103, ID_ZERO = 104,
              ID_RESTORE = 105, ID_DRIVER = 106, ID_AUTOSTART = 107;
std::unique_ptr<Engine> engine;
HWND listview, status_label, cpu_label, gpu_label, slider, light_label, pause_button;
HFONT font;
HICON large_icon, small_icon;
StartupState boot;
int restore_light = 100;

UINT taskbar_created;
int dpi = 96;
int px(int n) {
    return MulDiv(n, dpi, 96);
}
void output(const Json &j, const fs::path &p) {
    auto text = j.dump(2) + "\n";
    if (!p.empty()) {
        std::ofstream f(p, std::ios::binary);
        f << text;
        if (!f)
            throw std::runtime_error("Cannot write diagnostic output");
    } else {
        DWORD n = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), (DWORD)text.size(), &n, nullptr);
    }
}
HWND control(HWND parent, const wchar_t *c, const wchar_t *text, DWORD style, int x, int y, int w, int h,
             int id = 0) {
    HWND p = CreateWindowExW(0, c, text, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h), parent,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(p, WM_SETFONT, (WPARAM)font, TRUE);
    return p;
}
void icon(HWND h, DWORD action) {
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = h;
    n.uID = 1;
    n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    n.uCallbackMessage = tray_message;
    n.hIcon = small_icon;
    wcscpy_s(n.szTip, L"MythCool-mini · N360 参数续刷");
    Shell_NotifyIconW(action, &n);
}
void refresh_startup(HWND h) {
    boot = startup_state();
    if (auto button = GetDlgItem(h, ID_AUTOSTART)) {
        SendMessageW(button, BM_SETCHECK, boot.enabled && boot.matches ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(button, boot.error.empty() ? L"开机自启（管理员）" : L"开机自启（读取失败）");
    }
}
void toggle_startup(HWND h) {
    auto before = startup_state();
    if (!before.error.empty()) {
        MessageBoxW(h, before.error.c_str(), L"读取开机自启失败", MB_OK | MB_ICONERROR);
        refresh_startup(h);
        return;
    }
    auto result = set_startup(!(before.enabled && before.matches));
    if (!result.error.empty())
        MessageBoxW(h, result.error.c_str(), L"更新开机自启失败", MB_OK | MB_ICONERROR);
    refresh_startup(h);
}
void refresh(HWND h) {
    if (!engine || !IsWindowVisible(h))
        return;
    auto s = engine->snapshot();
    for (int id : {ID_SLIDER, ID_ZERO, ID_RESTORE})
        EnableWindow(GetDlgItem(h, id), !s.official);
    size_t enabled = 0, available = 0;
    for (auto &i : s.slots)
        if (i.enabled) {
            ++enabled;
            if (i.metric != Metric::Unknown && i.value != "--")
                ++available;
        }
    static const bool admin = is_elevated();
    SetWindowTextW(status_label,
                   ((admin ? std::wstring(L"管理员") : std::wstring(L"普通权限")) + L" · " +
                    std::to_wstring(available) + L"/" + std::to_wstring(enabled) + L" 项可用 · " + s.status +
                    L" · 已刷新 " + std::to_wstring(s.updates) + L" 次")
                       .c_str());
    SetWindowTextW(cpu_label, s.cpu_status.c_str());
    SetWindowTextW(gpu_label, s.gpu_status.c_str());
    SetWindowTextW(pause_button, s.paused ? L"恢复刷新" : L"暂停刷新");
    if (s.brightness > 0)
        restore_light = s.brightness;
    if (s.brightness >= 0 && !GetCapture()) {
        SendMessageW(slider, TBM_SETPOS, TRUE, s.brightness);
        SetWindowTextW(light_label, (L"屏幕亮度：" + std::to_wstring(s.brightness)).c_str());
    }
    if (ListView_GetItemCount(listview) != (int)s.slots.size()) {
        ListView_DeleteAllItems(listview);
        for (size_t i = 0; i < s.slots.size(); ++i) {
            LVITEMW v{};
            v.mask = LVIF_TEXT;
            v.iItem = (int)i;
            v.pszText = (LPWSTR)L"";
            ListView_InsertItem(listview, &v);
        }
    }
    for (int i = 0; i < (int)s.slots.size(); ++i) {
        auto &a = s.slots[i];
        std::wstring values[] = {wide(a.title), wide(a.value), wide(a.unit), wide(a.inner),
                                 !a.enabled                    ? L"未启用"
                                 : a.metric == Metric::Unknown ? L"未支持此指标"
                                 : a.value == "--"             ? L"等待数据 / 不可用"
                                                               : L"正常"};
        for (int col = 0; col < 5; ++col)
            ListView_SetItemText(listview, i, col, values[col].data());
    }
}
bool ui_ready = false;
void create_ui(HWND h) {
    if (ui_ready)
        return;
    NONCLIENTMETRICSW nc{};
    nc.cbSize = sizeof(nc);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nc), &nc, 0);
    font = CreateFontIndirectW(&nc.lfMessageFont);
    status_label = control(h, L"STATIC", L"正在读取原版设置", 0, 12, 12, 740, 44);
    listview = control(h, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER | WS_BORDER, 12,
                       60, 740, 160, ID_LIST);
    ListView_SetExtendedListViewStyle(listview, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    const wchar_t *names[] = {L"屏幕项目", L"当前值", L"单位", L"原版选择的指标", L"数据状态"};
    int widths[] = {110, 65, 55, 290, 190};
    for (int i = 0; i < 5; ++i) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (LPWSTR)names[i];
        c.cx = px(widths[i]);
        ListView_InsertColumn(listview, i, &c);
    }
    cpu_label = control(h, L"STATIC", L"", 0, 12, 230, 740, 38);
    gpu_label = control(h, L"STATIC", L"", 0, 12, 270, 740, 24);
    light_label = control(h, L"STATIC", L"屏幕亮度", 0, 12, 306, 125, 24);
    slider = control(h, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_AUTOTICKS, 140, 300, 350, 35, ID_SLIDER);
    SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(slider, TBM_SETTICFREQ, 10, 0);
    control(h, L"BUTTON", L"息屏", BS_PUSHBUTTON, 505, 301, 75, 28, ID_ZERO);
    control(h, L"BUTTON", L"恢复亮度", BS_PUSHBUTTON, 590, 301, 95, 28, ID_RESTORE);
    control(h, L"BUTTON", L"重新读取原版设置", BS_PUSHBUTTON, 12, 350, 175, 30, ID_RELOAD);
    pause_button = control(h, L"BUTTON", L"暂停刷新", BS_PUSHBUTTON, 200, 350, 110, 30, ID_PAUSE);
    control(h, L"BUTTON", L"CPU 驱动说明", BS_PUSHBUTTON, 325, 350, 130, 30, ID_DRIVER);
    control(h, L"STATIC", L"关闭窗口后保留托盘。原版运行时自动待机，退出后接管。", 0, 12, 394, 740, 24);
    control(h, L"BUTTON", L"开机自启（管理员）", BS_AUTOCHECKBOX | WS_TABSTOP, 480, 350, 270, 30,
            ID_AUTOSTART);
    ui_ready = true;
    refresh_startup(h);
}
void show(HWND h) {
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    refresh(h);
}
void menu(HWND h) {
    refresh_startup(h);
    auto s = engine->snapshot();
    HMENU m = CreatePopupMenu(), b = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 200, L"状态与亮度…");
    AppendMenuW(m, MF_STRING, ID_RELOAD, L"重新读取原版设置");
    AppendMenuW(m, MF_STRING | (s.paused ? MF_CHECKED : 0), ID_PAUSE, L"暂停参数刷新");
    AppendMenuW(b, MF_STRING, ID_ZERO, L"0 · 息屏");
    AppendMenuW(b, MF_STRING, ID_RESTORE, L"恢复此前亮度");
    for (int v : {10, 25, 50, 75, 100})
        AppendMenuW(b, MF_STRING | (s.brightness == v ? MF_CHECKED : 0), 300 + v,
                    (std::to_wstring(v) + L"%").c_str());
    AppendMenuW(m, MF_POPUP | (s.official ? MF_GRAYED : 0), (UINT_PTR)b, L"屏幕亮度");
    AppendMenuW(m, MF_STRING | (boot.enabled && boot.matches ? MF_CHECKED : 0), ID_AUTOSTART,
                L"开机自启（管理员）");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 201, L"退出 MythCool-mini");
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(h);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr);
    PostMessageW(h, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd)
        SendMessageW(h, WM_COMMAND, cmd, 0);
}
LRESULT CALLBACK wnd(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == taskbar_created) {
        icon(h, NIM_ADD);
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        icon(h, NIM_ADD);
        SetTimer(h, 1, 1000, nullptr);
        return 0;
    case WM_SHOWWINDOW:
        if (w)
            create_ui(h);
        break;
    case WM_ACTIVATE:
        if (LOWORD(w) != WA_INACTIVE && ui_ready)
            refresh_startup(h);
        break;
    case WM_TIMER:
        refresh(h);
        return 0;
    case WM_HSCROLL:
        if ((HWND)l == slider) {
            int v = (int)SendMessageW(slider, TBM_GETPOS, 0, 0);
            SetWindowTextW(light_label, (L"屏幕亮度：" + std::to_wstring(v)).c_str());
            if (LOWORD(w) == TB_ENDTRACK)
                engine->light(v);
        }
        return 0;
    case tray_message:
        if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU)
            menu(h);
        else if (l == WM_LBUTTONDBLCLK)
            show(h);
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_AUTOSTART)
            toggle_startup(h);
        else if (id == ID_RELOAD)
            engine->reload();
        else if (id == ID_PAUSE)
            engine->pause(!engine->snapshot().paused);
        else if (id == ID_ZERO) {
            auto s = engine->snapshot();
            if (s.brightness > 0)
                restore_light = s.brightness;
            engine->light(0);
        } else if (id == ID_RESTORE)
            engine->light(restore_light);
        else if (id >= 300 && id <= 400)
            engine->light(id - 300);
        else if (id == 200)
            show(h);
        else if (id == 201)
            DestroyWindow(h);
        else if (id == ID_DRIVER) {
            MessageBoxW(h,
                        L"CPU 温度和功耗需要已签名的 PawnIO 硬件读取驱动及访问权限。GPU 和 CPU "
                        L"占用不依赖此驱动。\n\n本程序现在默认申请管理员权限。驱动已安装时无需重复安装。将打"
                        L"开 PawnIO 官方下载页面。",
                        L"CPU 监控驱动", MB_OK | MB_ICONINFORMATION);
            ShellExecuteW(h, L"open", L"https://pawnio.eu/", nullptr, nullptr, SW_SHOWNORMAL);
        }
        refresh(h);
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        return 0;
    case exit_message:
        DestroyWindow(h);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (w)
            DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        icon(h, NIM_DELETE);
        engine.reset();
        if (font)
            DeleteObject(font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> a;
    for (int i = 1; i < argc; ++i)
        a.emplace_back(argv[i]);
    LocalFree(argv);
    auto has = [&](const wchar_t *s) { return std::find(a.begin(), a.end(), s) != a.end(); };
    fs::path out;
    for (size_t i = 0; i + 1 < a.size(); ++i)
        if (a[i] == L"--output")
            out = a[i + 1];
    try {
        if (has(L"--quit")) {
            if (HWND h = FindWindowW(cls, nullptr))
                PostMessageW(h, exit_message, 0, 0);
            return 0;
        }
        if (has(L"--diagnose") || has(L"--import-only") || has(L"--probe-screen")) {
            output(diagnostics(!has(L"--import-only"), has(L"--probe-screen")), out);
            return 0;
        }
        if (has(L"--autostart")) {
            std::wstring operation;
            for (size_t i = 0; i + 1 < a.size(); ++i)
                if (a[i] == L"--autostart")
                    operation = a[i + 1];
            if (operation != L"on" && operation != L"off" && operation != L"status") {
                output(Json{{"error", "Use --autostart on|off|status"}}, out);
                return 2;
            }
            auto state = operation == L"status" ? startup_state() : set_startup(operation == L"on");
            bool active = state.enabled && state.matches && state.error.empty();
            output(Json{{"administrator", is_elevated()},
                        {"registered", state.registered},
                        {"enabled", active},
                        {"path_matches", state.matches},
                        {"task_path", utf8(state.task_path)},
                        {"executable", utf8(state.executable)},
                        {"error", utf8(state.error)}},
                   out);
            return state.error.empty() ? (operation == L"status" && !active ? 3 : 0) : 1;
        }
        HANDLE singleton = CreateMutexW(nullptr, FALSE, LR"(Local\MythCool-mini-0.1)");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            if (has(L"--run-for") || has(L"--test-brightness"))
                throw std::runtime_error("Exit the running mini instance before hardware tests");
            if (HWND h = FindWindowW(cls, nullptr))
                show(h);
            CloseHandle(singleton);
            return 0;
        }
        if (has(L"--test-brightness")) {
            auto j = test_brightness();
            output(j, out);
            CloseHandle(singleton);
            return j["screen_off_verified"] == true && j["restore_verified"] == true &&
                           j["play_mode_unchanged"] == true
                       ? 0
                       : 1;
        }
        if (has(L"--run-for")) {
            int seconds = 0;
            for (size_t i = 0; i + 1 < a.size(); ++i)
                if (a[i] == L"--run-for")
                    seconds = std::stoi(a[i + 1]);
            auto j = bounded_run(seconds);
            output(j, out);
            CloseHandle(singleton);
            return j["updates"].get<int>() > 0 ? 0 : 1;
        }
        dpi = GetDpiForSystem();
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
        InitCommonControlsEx(&controls);
        large_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_MINI), IMAGE_ICON,
                                       GetSystemMetricsForDpi(SM_CXICON, dpi),
                                       GetSystemMetricsForDpi(SM_CYICON, dpi), LR_SHARED);
        small_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_MINI), IMAGE_ICON,
                                       GetSystemMetricsForDpi(SM_CXSMICON, dpi),
                                       GetSystemMetricsForDpi(SM_CYSMICON, dpi), LR_SHARED);
        taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
        WNDCLASSEXW c{};
        c.cbSize = sizeof(c);
        c.lpfnWndProc = wnd;
        c.hInstance = instance;
        c.hIcon = large_icon;
        c.hIconSm = small_icon;
        c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        c.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        c.lpszClassName = cls;
        RegisterClassExW(&c);
        engine = std::make_unique<Engine>();
        HWND h = CreateWindowExW(0, cls, L"MythCool-mini · N360",
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                                 CW_USEDEFAULT, px(780), px(470), nullptr, nullptr, instance, nullptr);
        if (!h)
            throw std::runtime_error("Cannot create native window");
        if (has(L"--show"))
            show(h);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        CloseHandle(singleton);
        return 0;
    } catch (const std::exception &e) {
        try {
            if (!out.empty())
                output(Json{{"error", e.what()}}, out);
            else
                MessageBoxW(nullptr, wide(e.what()).c_str(), L"MythCool-mini", MB_OK | MB_ICONERROR);
        } catch (...) {
        }
        return 1;
    }
}
