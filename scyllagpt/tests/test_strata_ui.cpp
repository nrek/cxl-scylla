#include "scyllagpt/strata_settings_ui.h"
#include "scyllagpt/ui_kit.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace scyllagpt;
static void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
static bool shown(HWND h) { return (GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE) != 0; }
static std::wstring value(HWND h) {
    std::wstring s(GetWindowTextLengthW(h) + 1, L'\0');
    GetWindowTextW(h, s.data(), static_cast<int>(s.size()));
    s.resize(wcslen(s.c_str()));
    return s;
}
static void settle(StrataSettingsUi &ui, HWND parent) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    do {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        ui.poll();
        if (IsWindowEnabled(GetDlgItem(parent, Id_StrataClose)))
            return;
        Sleep(20);
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("STRATA operation did not finish");
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 2) {
        std::cerr << "Pass a fixture workspace root\n";
        return 2;
    }
    try {
        auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = instance;
        wc.lpszClassName = L"StrataUiFixture";
        RegisterClassW(&wc);
        HWND parent = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 1000, 1000, nullptr,
                                    nullptr, instance, nullptr);
        require(parent != nullptr, "create parent");
        {
            StrataSettingsUi ui;
            require(ui.create(parent, instance, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))),
                    "create UI");
            ui.set_workspace_path(argv[1]);
            ui.set_project_binding(L"nonexistent-editor-project");
            ui.set_visible(true);
            ui.layout({0, 0, 950, 950});
            ui.refresh(nullptr, nullptr);
            settle(ui, parent);
            auto results = GetDlgItem(parent, Id_StrataResults);
            if (SendMessageW(results, LB_GETCOUNT, 0, 0) != 2) {
                std::wcerr << value(GetDlgItem(parent, Id_StrataDiag)) << L'\n';
                throw std::runtime_error("Expected both projects on initial load");
            }
            auto command = [&](int id) {
                ui.handle_command(id, BN_CLICKED, nullptr, nullptr, "", L"", parent);
                settle(ui, parent);
            };
            command(Id_StrataOpen);
            require(SendMessageW(results, LB_GETCOUNT, 0, 0) == 1, "project drill-down");
            SetWindowTextW(GetDlgItem(parent, Id_StrataSearch), L"searchneedle");
            command(Id_StrataSearchButton);
            auto selection = SendMessageW(results, LB_GETCURSEL, 0, 0);
            auto top = SendMessageW(results, LB_GETTOPINDEX, 0, 0);
            command(Id_StrataOpen);
            auto doc = GetDlgItem(parent, Id_StrataDetail);
            require(shown(doc) && !shown(results), "document replaces results");
            require(value(doc).find(L"searchneedle") != std::wstring::npos, "full archived body loaded");
            require((GetWindowLongPtrW(doc, GWL_STYLE) & ES_READONLY) != 0, "document read-only");
            command(Id_StrataClose);
            require(shown(results) && !shown(doc), "close restores results");
            require(SendMessageW(results, LB_GETCURSEL, 0, 0) == selection, "selection preserved");
            require(SendMessageW(results, LB_GETTOPINDEX, 0, 0) == top, "scroll preserved");
            require(value(GetDlgItem(parent, Id_StrataSearch)) == L"searchneedle", "query preserved");
            ui.set_visible(false);
            ui.set_visible(true);
            ui.refresh(nullptr, nullptr);
            settle(ui, parent);
            require(SendMessageW(results, LB_GETCOUNT, 0, 0) == 1, "reentry retains drill-down");
        }
        DestroyWindow(parent);
        std::cout << "STRATA UI tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
