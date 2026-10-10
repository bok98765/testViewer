#include "Thumbnails.h"

#include <shellapi.h>
#include <shellscalingapi.h>
#include <shobjidl.h>

#include <algorithm>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace tv {

namespace {
constexpr uint32_t kMaxThumbSide = 512;
}

ThumbnailCache::ThumbnailCache() {
    worker_ = std::jthread([this](std::stop_token st) { workerLoop(st); });
}

ThumbnailCache::~ThumbnailCache() {
    worker_.request_stop();
    cv_.notify_all();
}

void ThumbnailCache::setNotifyWindow(HWND hwnd, UINT msg) {
    std::lock_guard lock(mu_);
    notify_ = hwnd;
    notifyMsg_ = msg;
}

void ThumbnailCache::setRequestedSizePx(uint32_t px) {
    sizePx_.store(std::clamp(px, 32u, kMaxThumbSide));
}

ID2D1Bitmap* ThumbnailCache::getOrRequest(ID2D1DeviceContext* dc,
                                          const std::filesystem::path& path) {
    if (!dc) return nullptr;
    std::lock_guard lock(mu_);

    auto it = entries_.find(path);
    if (it == entries_.end()) {
        it = entries_.emplace(path, Entry{}).first;
        order_.push_back(path);
        trimLocked();
    }
    Entry& e = it->second;

    if (e.bmp) return e.bmp.Get();

    if (e.raw.ready) {
        // Raw pixels survive bitmap creation so thumbnails can be rebuilt after
        // a device reset without hitting the shell again.
        D2D1_BITMAP_PROPERTIES props{
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f, 96.0f};
        ComPtr<ID2D1Bitmap> bmp;
        if (SUCCEEDED(dc->CreateBitmap(D2D1_SIZE_U{e.raw.width, e.raw.height},
                                       e.raw.bgra.data(), e.raw.stride, props, &bmp))) {
            e.bmp = bmp;
            return e.bmp.Get();
        }
    } else if (!e.requested) {
        e.requested = true;
        queue_.push_back(path);
        cv_.notify_all();
    }
    return nullptr;
}

void ThumbnailCache::clearBitmaps() {
    std::lock_guard lock(mu_);
    for (auto& [path, e] : entries_) e.bmp.Reset();
}

void ThumbnailCache::clear() {
    std::lock_guard lock(mu_);
    queue_.clear();
    order_.clear();
    entries_.clear();
}

void ThumbnailCache::trimLocked() {
    while (entries_.size() > kMaxEntries && !order_.empty()) {
        const std::filesystem::path victim = order_.front();
        order_.pop_front();
        entries_.erase(victim);
    }
}

void ThumbnailCache::workerLoop(std::stop_token st) {
    // The shell item APIs require COM on the calling thread; the UI thread is
    // already initialized, this one is not.
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comOwned = SUCCEEDED(hrInit);

    while (!st.stop_requested()) {
        std::filesystem::path path;
        {
            std::unique_lock lock(mu_);
            cv_.wait(lock, st, [&] { return !queue_.empty(); });
            if (st.stop_requested()) break;
            path = queue_.front();
            queue_.pop_front();
        }

        Raw raw;
        bool stored = false;
        if (loadRaw(path, raw)) {
            std::lock_guard lock(mu_);
            if (auto it = entries_.find(path); it != entries_.end()) {
                it->second.raw = std::move(raw);
                stored = true;
            }
        }
        if (stored) {
            HWND hwnd;
            UINT msg;
            {
                std::lock_guard lock(mu_);
                hwnd = notify_;
                msg = notifyMsg_;
            }
            if (hwnd && msg) PostMessageW(hwnd, msg, 0, 0);
        }
    }

    if (comOwned) CoUninitialize();
}

bool ThumbnailCache::loadRaw(const std::filesystem::path& path, Raw& out) const {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr,
                                           IID_PPV_ARGS(&item))))
        return false;

    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(item.As(&factory))) return false;

    const UINT px = sizePx_.load();
    HBITMAP hbmp = nullptr;
    const HRESULT hr = factory->GetImage(SIZE{static_cast<INT>(px), static_cast<INT>(px)},
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
    if (w <= 0 || h <= 0 || static_cast<uint32_t>(w) > kMaxThumbSide ||
        static_cast<uint32_t>(h) > kMaxThumbSide) {
        DeleteObject(hbmp);
        return false;
    }

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
    out.bgra.assign(static_cast<size_t>(out.stride) * h, 0);

    // GetDIBits needs a real DC; NULL fails on a memory bitmap.
    HDC hdc = GetDC(nullptr);
    const BOOL ok = hdc && GetDIBits(hdc, hbmp, 0, static_cast<UINT>(h), out.bgra.data(),
                                     &bi, DIB_RGB_COLORS);
    if (hdc) ReleaseDC(nullptr, hdc);
    DeleteObject(hbmp);
    if (!ok) return false;

    // Shell DIBs carry an undefined alpha channel; treat them as opaque.
    for (size_t i = 3; i < out.bgra.size(); i += 4) out.bgra[i] = 0xFF;

    out.ready = true;
    return true;
}

}  // namespace tv
