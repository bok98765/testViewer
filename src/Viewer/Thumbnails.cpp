#include "Thumbnails.h"

#include <shellapi.h>
#include <shellscalingapi.h>
#include <shobjidl.h>
#include <windows.h>

#include <algorithm>

#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::ComPtr;

namespace tv {

namespace {
constexpr UINT kThumbPx = 128;
}

ThumbnailCache::ThumbnailCache() {
    worker_ = std::jthread([this](std::stop_token st) { workerLoop(st); });
}

ThumbnailCache::~ThumbnailCache() {
    worker_.request_stop();
    cv_.notify_all();
}

ID2D1Bitmap* ThumbnailCache::getOrRequest(ID2D1DeviceContext* dc,
                                          const std::filesystem::path& path) {
    if (!dc) return nullptr;
    Raw raw;
    bool haveRaw = false;
    {
        std::lock_guard lock(mu_);
        auto& e = entries_[path];
        if (e.bmp) return e.bmp.Get();
        if (e.raw.ready) {
            raw = std::move(e.raw);
            e.raw.bgra.clear();
            haveRaw = true;
        } else if (!e.requested) {
            e.requested = true;
            queue_.push_back(path);
            cv_.notify_all();
        }
    }
    if (haveRaw && !raw.bgra.empty()) {
        D2D1_BITMAP_PROPERTIES props{
            D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE},
            96.0f, 96.0f};
        ComPtr<ID2D1Bitmap> bmp;
        if (SUCCEEDED(dc->CreateBitmap(D2D1_SIZE_U{raw.width, raw.height}, raw.bgra.data(),
                                       raw.stride, props, &bmp))) {
            std::lock_guard lock(mu_);
            entries_[path].bmp = bmp;
            return entries_[path].bmp.Get();
        }
    }
    return nullptr;
}

void ThumbnailCache::clearBitmaps() {
    std::lock_guard lock(mu_);
    for (auto& [path, e] : entries_) e.bmp.Reset();
}

void ThumbnailCache::workerLoop(std::stop_token st) {
    while (!st.stop_requested()) {
        std::filesystem::path path;
        {
            std::unique_lock lock(mu_);
            cv_.wait(lock, st, [&] { return !queue_.empty(); });
            if (st.stop_requested()) return;
            path = queue_.front();
            queue_.pop_front();
        }
        Raw raw;
        if (loadRaw(path, raw)) {
            std::lock_guard lock(mu_);
            entries_[path].raw = std::move(raw);
        }
    }
}

bool ThumbnailCache::loadRaw(const std::filesystem::path& path, Raw& out) const {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr,
                                           IID_PPV_ARGS(&item))))
        return false;

    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(item.As(&factory))) return false;

    HBITMAP hbmp = nullptr;
    const HRESULT hr = factory->GetImage(SIZE{kThumbPx, kThumbPx},
                                         SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK, &hbmp);
    if (FAILED(hr) || !hbmp) return false;

    BITMAP bmpInfo{};
    if (!GetObjectW(hbmp, sizeof(bmpInfo), &bmpInfo) || bmpInfo.bmWidth <= 0 ||
        bmpInfo.bmHeight == 0) {
        DeleteObject(hbmp);
        return false;
    }

    const int w = bmpInfo.bmWidth;
    const int h = std::abs(bmpInfo.bmHeight);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    out.width = static_cast<uint32_t>(w);
    out.height = static_cast<uint32_t>(h);
    out.stride = static_cast<uint32_t>(w) * 4;
    out.bgra.resize(static_cast<size_t>(out.stride) * h);

    const BOOL ok = GetDIBits(nullptr, hbmp, 0, static_cast<UINT>(h), out.bgra.data(), &bi,
                              DIB_RGB_COLORS);
    DeleteObject(hbmp);
    if (!ok) return false;
    out.ready = true;
    return true;
}

}  // namespace tv
