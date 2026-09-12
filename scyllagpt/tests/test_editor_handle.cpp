#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const auto module = LoadLibraryW(argv[1]);
    if (!module) return 3;
    const auto create = reinterpret_cast<void* (*)(void*, int)>(GetProcAddress(module, "scylla_editor_create"));
    const auto destroy = reinterpret_cast<void (*)(void*)>(GetProcAddress(module, "scylla_editor_destroy"));
    const auto move = reinterpret_cast<void (*)(void*, int, int, int, int)>(GetProcAddress(module, "scylla_editor_move"));
    if (!create || !destroy || !move || !GetProcAddress(module, "scylla_session_set_project_roots")) return 4;
    const auto parent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 160, 120,
                                        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!parent) return 5;
    destroy(parent);
    if (!IsWindow(parent)) return 6;
    RECT before{}, after{};
    GetWindowRect(parent, &before);
    move(parent, 20, 20, 90, 90);
    GetWindowRect(parent, &after);
    if (!EqualRect(&before, &after)) return 7;
    const auto editor = static_cast<HWND>(create(parent, 42));
    if (!editor || !IsWindow(editor)) return 8;
    HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    if (!(contrast.dwFlags & HCF_HIGHCONTRASTON) &&
        !GetPropW(editor, L"ScyllaOwnsScrollbars")) return 12;
    if (GetParent(editor) != parent || reinterpret_cast<HMODULE>(GetWindowLongPtrW(editor, GWLP_HINSTANCE)) != module) return 11;
    move(editor, 0, 0, 140, 100);
    GetClientRect(editor, &after);
    if (after.right != 140 || after.bottom != 100) return 9;
    std::string lines;
    for (int i = 0; i < 200; ++i) lines += "Scrollbar regression line\n";
    SendMessageA(editor, 2181 /* SCI_SETTEXT */, 0, reinterpret_cast<LPARAM>(lines.c_str()));
    SendMessageW(editor, WM_VSCROLL, SB_PAGEDOWN, 0);
    if (SendMessageW(editor, 2152 /* SCI_GETFIRSTVISIBLELINE */, 0, 0) <= 0) return 13;
    SendMessageW(editor, WM_VSCROLL, SB_TOP, 0);
    if (SendMessageW(editor, 2152 /* SCI_GETFIRSTVISIBLELINE */, 0, 0) != 0) return 14;
    destroy(editor);
    if (IsWindow(editor) || !IsWindow(parent)) return 10;
    destroy(editor); // Repeated destruction of a dead HWND must also be harmless.
    destroy(nullptr);
    DestroyWindow(parent);
    FreeLibrary(module);
    std::cout << "editor HWND lifecycle, scrollbar skin/scrolling and foreign-window guards passed\n";
    return 0;
}
