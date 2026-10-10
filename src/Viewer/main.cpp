// TestViewer entry point (Win32 GUI app).
#include <windows.h>

#include <shellapi.h>
#include <string>
#include <vector>

#include "MainWindow.h"
#include "tv/Core.h"

#pragma comment(lib, "shell32.lib")

namespace {

// Splits the command line into filesystem paths (files or folders).
std::vector<std::filesystem::path> parseCommandLine() {
    std::vector<std::filesystem::path> out;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return out;
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path p(argv[i]);
        if (!p.empty() && p.wstring()[0] != L'/')  // ignore classic-style switches
            out.push_back(p);
    }
    LocalFree(argv);
    return out;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR, int nShow) {
    // Per-monitor DPI v2 must be set before any window is created.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        return 1;

    tv::registerDefaultDecoders();

    tv::AppSettings settings = tv::AppSettings::load();

    MainWindow* window = MainWindow::Create(hinst, settings);
    if (!window) {
        MessageBoxW(nullptr, L"Failed to create the main window.", L"TestViewer", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // Decide what to open: command-line paths first, then the last used folder.
    const auto args = parseCommandLine();
    std::error_code ec;
    if (!args.empty()) {
        if (std::filesystem::is_directory(args[0], ec))
            window->openFolder(args[0]);
        else
            window->openFile(args[0]);
    } else if (!settings.lastFolder.empty() &&
               std::filesystem::is_directory(settings.lastFolder, ec)) {
        window->openFolder(settings.lastFolder);
    }

    ShowWindow(window->handle(), nShow == SW_HIDE ? SW_SHOWDEFAULT : nShow);
    UpdateWindow(window->handle());

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    delete window;
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
