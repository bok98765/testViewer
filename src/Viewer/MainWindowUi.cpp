// MainWindow - dialogs, fullscreen/slideshow, delete, associations, overlays.
#include "MainWindow.h"

#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <sstream>

#include "tv/Core.h"

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Opening folders / files
// ---------------------------------------------------------------------------

void MainWindow::openFolder(const std::filesystem::path& dir,
                            const std::filesystem::path* select) {
    folder_ = dir;
    files_ = tv::listImages(dir);
    thumbs_.clear();
    if (files_.empty()) {
        img_.reset();
        imageBitmap_.Reset();
        scheduler_.setQueue({}, 0);
        updateTitle();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    size_t index = 0;
    if (select) {
        auto it = std::find(files_.begin(), files_.end(), *select);
        if (it != files_.end()) index = static_cast<size_t>(it - files_.begin());
    }
    loadImage(index);
}

void MainWindow::openFile(const std::filesystem::path& file) {
    std::error_code ec;
    const auto dir = file.parent_path();
    if (!std::filesystem::is_directory(dir, ec)) return;
    openFolder(dir, &file);
}

void MainWindow::updateTitle() {
    std::wstring title = L"TestViewer";
    if (!files_.empty() && index_ < files_.size()) {
        title += L" \u2014 ";
        title += files_[index_].filename().wstring();
        title += L"  (" + std::to_wstring(index_ + 1) + L"/" +
                 std::to_wstring(files_.size()) + L")";
    }
    if (slideshow_) title += L"  [Slideshow]";
    SetWindowTextW(hwnd_, title.c_str());
}

// ---------------------------------------------------------------------------
// Fullscreen / slideshow
// ---------------------------------------------------------------------------

void MainWindow::toggleFullscreen() {
    if (!fullscreen_) {
        savedStyle_ = GetWindowLongW(hwnd_, GWL_STYLE);
        savedExStyle_ = GetWindowLongW(hwnd_, GWL_EXSTYLE);
        GetWindowRect(hwnd_, &savedRect_);

        const HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        if (!GetMonitorInfoW(mon, &mi)) return;

        SetWindowLongW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongW(hwnd_, GWL_EXSTYLE, savedExStyle_ & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE));
        SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fullscreen_ = true;
    } else {
        SetWindowLongW(hwnd_, GWL_STYLE, savedStyle_);
        SetWindowLongW(hwnd_, GWL_EXSTYLE, savedExStyle_);
        SetWindowPos(hwnd_, nullptr, savedRect_.left, savedRect_.top,
                     savedRect_.right - savedRect_.left,
                     savedRect_.bottom - savedRect_.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
        fullscreen_ = false;
    }
    updateTitle();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::toggleSlideshow() {
    slideshow_ = !slideshow_;
    if (slideshow_)
        SetTimer(hwnd_, kSlideshowTimer,
                 static_cast<UINT>(settings_.slideshowSec) * 1000u, nullptr);
    else
        KillTimer(hwnd_, kSlideshowTimer);
    updateTitle();
}

// ---------------------------------------------------------------------------
// Right-click context menu
// ---------------------------------------------------------------------------

void MainWindow::showContextMenu(POINT screenPt) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, IDM_VIEW_ROTATE_CW, L"Rotate clockwise\tR");
    AppendMenuW(menu, MF_STRING, IDM_VIEW_ROTATE_CCW, L"Rotate counterclockwise\tShift+R");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_VIEW_FIT, L"Fit to window\t0");
    AppendMenuW(menu, MF_STRING, IDM_VIEW_ACTUAL, L"Actual size\t1");

    // The window must be foreground so the menu dismisses on outside clicks and
    // the resulting WM_COMMAND reaches this window.
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_TOPALIGN | TPM_LEFTALIGN, screenPt.x, screenPt.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

// ---------------------------------------------------------------------------
// Delete (recycle bin)
// ---------------------------------------------------------------------------

void MainWindow::deleteCurrent() {
    if (files_.empty() || index_ >= files_.size()) return;
    const auto path = files_[index_];

    const int answer = MessageBoxW(
        hwnd_,
        (L"Move this file to the Recycle Bin?\n\n" + path.filename().wstring()).c_str(),
        L"Delete image", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (answer != IDYES) return;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // no-op if already init
    ComPtr<IFileOperation> op;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&op)))) {
        MessageBoxW(hwnd_, L"Failed to delete file.", L"TestViewer", MB_ICONERROR);
        return;
    }
    op->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI);

    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        MessageBoxW(hwnd_, L"Failed to delete file.", L"TestViewer", MB_ICONERROR);
        return;
    }
    op->DeleteItem(item.Get(), nullptr);
    if (FAILED(op->PerformOperations())) {
        MessageBoxW(hwnd_, L"Failed to delete file.", L"TestViewer", MB_ICONERROR);
        return;
    }

    cache_.evictOlderThan({0, 0});  // drop the deleted entry on next put
    const size_t nextIndex = index_;
    files_.erase(files_.begin() + static_cast<long>(index_));
    if (files_.empty()) {
        img_.reset();
        imageBitmap_.Reset();
        scheduler_.setQueue({}, 0);
        updateTitle();
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else {
        loadImage(std::min(nextIndex, files_.size() - 1));
    }
}

// ---------------------------------------------------------------------------
// Open dialogs
// ---------------------------------------------------------------------------

namespace {

std::wstring buildImageFilter() {
    std::wstring pattern;
    for (const auto& ext : tv::DecoderRegistry::instance().extensions()) {
        if (!pattern.empty()) pattern += L';';
        pattern += L'*' + ext;
    }
    return pattern;  // "*.jpg;*.jpeg;..."
}

}  // namespace

void MainWindow::showOpenFileDialog() {
    tv::registerDefaultDecoders();
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return;
    dlg->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);

    const std::wstring pattern = buildImageFilter();
    const COMDLG_FILTERSPEC specs[] = {
        {L"Supported Images", pattern.c_str()},
        {L"All Files", L"*.*"},
    };
    dlg->SetFileTypes(ARRAYSIZE(specs), specs);
    dlg->SetFileTypeIndex(1);

    if (SUCCEEDED(dlg->Show(hwnd_))) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            wchar_t* raw = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) {
                openFile(std::filesystem::path(raw));
                CoTaskMemFree(raw);
            }
        }
    }
}

void MainWindow::showOpenFolderDialog() {
    tv::registerDefaultDecoders();
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return;
    dlg->SetOptions(FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS);
    if (SUCCEEDED(dlg->Show(hwnd_))) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            wchar_t* raw = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) {
                openFolder(std::filesystem::path(raw));
                CoTaskMemFree(raw);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// File association menu actions
// ---------------------------------------------------------------------------

void MainWindow::associateCurrent(bool add) {
    tv::registerDefaultDecoders();
    std::wstring ext;
    if (!files_.empty() && index_ < files_.size())
        ext = files_[index_].extension().wstring();
    if (ext.empty()) {
        MessageBoxW(hwnd_, L"No image is open.", L"TestViewer", MB_ICONINFORMATION);
        return;
    }
    const bool ok = add ? tv::registerFileAssociation(ext)
                        : tv::unregisterFileAssociation(ext);
    std::wstring msg = (add ? L"Associated " : L"Removed association for ") + ext;
    if (!ok) msg += L" (failed)";
    MessageBoxW(hwnd_, msg.c_str(), L"File association",
                ok ? MB_ICONINFORMATION : MB_ICONERROR);
}

void MainWindow::showAbout() {
    MessageBoxW(hwnd_,
                L"TestViewer\nWin32 + Direct2D/WIC image viewer\n"
                L"Windows 10 22H2+ / Windows 11, x64\n\n"
                L"Keys: arrows=prev/next  wheel=zoom  drag=pan  right-click=fit\n"
                L"space=fit/100%  r/R=rotate  h/v=flip  f=fullscreen  F5=slideshow\n"
                L"i=info  t=filmstrip  o=open file  p=open folder  Del=delete",
                L"About TestViewer", MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------

void MainWindow::drawInfo() {
    if (!dc_ || !img_) return;

    std::wostringstream line2, line3;
    line2 << (index_ + 1) << L" / " << files_.size() << L"    "
          << img_->size.width << L" x " << img_->size.height << L"    "
          << static_cast<int>(vp_.zoom * 100.0f + 0.5f) << L"%    "
          << tv::formatFileSize(img_->fileSize);

    auto ext = files_[index_].extension().wstring();
    if (!ext.empty() && ext[0] == L'.') ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    line3 << ext << L"    " << tv::formatTime(img_->mtime);
    if (img_->exifOrientation != 1) line3 << L"    EXIF rotated";
    if (img_->animated) line3 << L"    animated (first frame)";

    const D2D1_SIZE_F client = clientSize();
    const FLOAT pad = 12.0f, w = 560.0f, h = 92.0f;
    const D2D1_RECT_F panel{pad, pad, pad + w, pad + h};

    auto fill = [&](D2D1_COLOR_F c) {
        dc_->CreateSolidColorBrush(c, &brush_);
    };
    auto text = [&](IDWriteTextFormat* fmt, const std::wstring& s, D2D1_RECT_F r, D2D1_COLOR_F c) {
        dc_->CreateSolidColorBrush(c, &brush_);
        dc_->DrawText(s.c_str(), static_cast<UINT>(s.size()), fmt, r, brush_.Get());
    };

    fill(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f));
    dc_->FillRectangle(panel, brush_.Get());

    const std::wstring name = files_[index_].filename().wstring();
    text(boldFormat_.Get(), name,
         D2D1_RECT_F{panel.left + 10, panel.top + 6, panel.right - 10, panel.top + 34},
         D2D1::ColorF(D2D1::ColorF::White));
    const std::wstring l2 = line2.str(), l3 = line3.str();
    text(textFormat_.Get(), l2,
         D2D1_RECT_F{panel.left + 10, panel.top + 36, panel.right - 10, panel.top + 60},
         D2D1::ColorF(0.85f, 0.85f, 0.85f));
    text(smallFormat_.Get(), l3,
         D2D1_RECT_F{panel.left + 10, panel.top + 62, panel.right - 10, panel.bottom - 6},
         D2D1::ColorF(0.65f, 0.65f, 0.65f));
    (void)client;
}

void MainWindow::drawFilmstrip() {
    if (!dc_ || files_.empty()) return;

    const D2D1_SIZE_F client = clientSize();
    const FilmGeom g = filmGeom();
    const int n = static_cast<int>(files_.size());
    const int cur = static_cast<int>(index_);

    if (!stripBgBrush_ &&
        FAILED(dc_->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.72f),
                                          &stripBgBrush_)))
        return;
    if (!stripCellBrush_ &&
        FAILED(dc_->CreateSolidColorBrush(D2D1::ColorF(0.16f, 0.16f, 0.16f),
                                          &stripCellBrush_)))
        return;
    if (!stripSelBrush_ &&
        FAILED(dc_->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.65f, 1.0f),
                                          &stripSelBrush_)))
        return;

    dc_->FillRectangle(D2D1_RECT_F{0.0f, g.top, client.width, client.height},
                       stripBgBrush_.Get());

    // Requests are issued nearest-first so the visible area fills in before the
    // edges of the strip.
    const auto drawCell = [&](int i) {
        if (i < 0 || i >= n) return;
        const float x = g.centerX + (i - cur) * g.cell;
        if (x + g.thumb < 0.0f || x > client.width) return;

        const float boxTop = g.top + 11.0f;
        const D2D1_RECT_F box{x, boxTop, x + g.thumb, boxTop + g.thumb};
        ID2D1Bitmap* bmp = thumbs_.getOrRequest(dc_.Get(), files_[static_cast<size_t>(i)]);
        if (bmp) {
            const D2D1_SIZE_F s = bmp->GetSize();
            if (s.width > 0.0f && s.height > 0.0f) {
                const float sc = std::min(g.thumb / s.width, g.thumb / s.height);
                const float w = s.width * sc, h = s.height * sc;
                const D2D1_RECT_F dst{x + (g.thumb - w) * 0.5f,
                                      boxTop + (g.thumb - h) * 0.5f,
                                      x + (g.thumb + w) * 0.5f,
                                      boxTop + (g.thumb + h) * 0.5f};
                dc_->DrawBitmap(bmp, &dst, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
            }
        } else {
            dc_->FillRectangle(box, stripCellBrush_.Get());
        }
        if (i == cur) dc_->DrawRectangle(box, stripSelBrush_.Get(), 2.0f);
    };

    drawCell(cur);
    const int span = static_cast<int>(client.width / g.cell) + 2;
    for (int d = 1; d <= span; ++d) {
        drawCell(cur - d);
        drawCell(cur + d);
    }
}

int MainWindow::filmstripIndexAt(D2D1_POINT_2F pt) const {
    if (files_.empty()) return -1;
    const FilmGeom g = filmGeom();
    if (pt.y < g.top) return -1;
    const long idx = static_cast<long>(index_) +
                     static_cast<long>(std::lround((pt.x - g.centerX) / g.cell));
    if (idx < 0 || idx >= static_cast<long>(files_.size())) return -1;
    return static_cast<int>(idx);
}
