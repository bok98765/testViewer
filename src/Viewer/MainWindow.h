// MainWindow: Win32 + Direct2D viewer window.
#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite_3.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <filesystem>
#include <vector>

#include "Thumbnails.h"
#include "Viewport.h"
#include "tv/Core.h"

class MainWindow {
public:
    static MainWindow* Create(HINSTANCE hinst, const tv::AppSettings& settings);

    // Opens a folder; selects `select` when given, otherwise the first image.
    void openFolder(const std::filesystem::path& dir,
                    const std::filesystem::path* select = nullptr);
    void openFile(const std::filesystem::path& file);

    HWND handle() const { return hwnd_; }

    ~MainWindow();

private:
    // --- lifecycle / window proc -------------------------------------------
    MainWindow(HINSTANCE hinst, const tv::AppSettings& settings);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT onMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool createDeviceResources();
    void releaseDeviceResources();
    void resizeSwapChain();
    void render();

    // --- navigation ----------------------------------------------------------
    void loadImage(size_t index);
    void setCurrentBitmap();
    void next();
    void prev();
    void first();
    void last();
    void deleteCurrent();
    void showOpenFileDialog();
    void showOpenFolderDialog();
    void updateTitle();

    // --- view ----------------------------------------------------------------
    void toggleFullscreen();
    void toggleSlideshow();
    void toggleInfo() { showInfo_ = !showInfo_; settings_.showInfo = showInfo_; InvalidateRect(hwnd_, nullptr, FALSE); }
    void toggleFilmstrip() {
        showFilmstrip_ = !showFilmstrip_;
        settings_.showFilmstrip = showFilmstrip_;
        const float half = kFilmStripDip / 2.0f;
        vp_.panBy({0.0f, showFilmstrip_ ? half : -half});
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void setFit() { vp_.fit(viewportSize()); InvalidateRect(hwnd_, nullptr, FALSE); }
    void setActualSize() { vp_.fit(viewportSize(), true); InvalidateRect(hwnd_, nullptr, FALSE); }
    void showContextMenu(POINT screenPt);

    // --- overlays --------------------------------------------------------------
    void drawInfo();
    void drawFilmstrip();
    int filmstripIndexAt(D2D1_POINT_2F pt) const;
    float filmstripHeightDip() const { return showFilmstrip_ ? kFilmStripDip : 0.0f; }

    struct FilmGeom {
        float top = 0.0f;     // strip top edge in DIPs
        float thumb = 0.0f;   // cell side
        float cell = 0.0f;    // cell pitch
        float centerX = 0.0f; // x of the cell for the current index
    };
    FilmGeom filmGeom() const;

    // --- helpers ---------------------------------------------------------------
    D2D1_SIZE_F clientSize() const;   // client rect in DIPs
    D2D1_SIZE_F viewportSize() const; // client area left for the image (DIPs)
    D2D1_POINT_2F clientPointDip(LPARAM lParam) const;
    float dpi() const;
    void saveSettings();
    void associateCurrent(bool add);
    void showAbout();

    static constexpr const wchar_t* kWndClass = L"TestViewerWindow";
    enum {
        IDM_FILE_OPEN_FILE = 101, IDM_FILE_OPEN_FOLDER, IDM_FILE_DELETE, IDM_FILE_EXIT,
        IDM_VIEW_FIT = 121, IDM_VIEW_ACTUAL, IDM_VIEW_ROTATE_CW, IDM_VIEW_ROTATE_CCW,
        IDM_VIEW_FLIP_H, IDM_VIEW_FLIP_V, IDM_VIEW_FULLSCREEN, IDM_VIEW_SLIDESHOW,
        IDM_VIEW_INFO, IDM_VIEW_FILMSTRIP,
        IDM_TOOLS_ASSOC_ADD = 141, IDM_TOOLS_ASSOC_REMOVE,
        IDM_HELP_ABOUT = 161,
    };
    static constexpr UINT_PTR kSlideshowTimer = 1;
    static constexpr UINT kMsgDragCompleted = WM_APP + 1;
    static constexpr UINT kMsgThumbReady = WM_APP + 2;
    static constexpr float kFilmThumbDip = 96.0f;
    static constexpr float kFilmGapDip = 8.0f;
    static constexpr float kFilmStripDip = 118.0f;

    // --- state -----------------------------------------------------------------
    HWND hwnd_ = nullptr;
    HINSTANCE hinst_ = nullptr;
    tv::AppSettings settings_;
    tv::ImageCache cache_;
    tv::PrefetchScheduler scheduler_;

    std::vector<std::filesystem::path> files_;
    size_t index_ = 0;
    tv::ImagePtr img_;

    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory_;
    Microsoft::WRL::ComPtr<ID3D11Device> d3d_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain_;
    Microsoft::WRL::ComPtr<ID2D1Device> device_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> target_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> imageBitmap_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> stripBgBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> stripCellBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> stripSelBrush_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> boldFormat_;

    tv::Viewport vp_;
    tv::ThumbnailCache thumbs_;
    bool deviceLost_ = false;

    bool fullscreen_ = false;
    LONG savedStyle_ = 0, savedExStyle_ = 0;
    RECT savedRect_{};
    bool slideshow_ = false;
    bool showInfo_ = true;
    bool showFilmstrip_ = true;

    bool panning_ = false;
    POINT lastPt_{};
    bool mouseInWindow_ = false;
    UINT dragDoneMsg_ = 0;  // TaskbarDragCompleted registered message
    std::filesystem::path folder_;
};
