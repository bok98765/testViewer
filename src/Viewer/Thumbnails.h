// Filmstrip thumbnails: shell-provided thumbnails decoded on a worker thread.
#pragma once

#include <d2d1.h>
#include <d2d1_1.h>
#include <wrl/client.h>

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tv {

class ThumbnailCache {
public:
    ThumbnailCache();
    ~ThumbnailCache();

    // UI thread only. Returns the bitmap when ready; kicks off an async
    // shell-thumbnail load otherwise (returns nullptr until ready).
    ID2D1Bitmap* getOrRequest(ID2D1DeviceContext* dc, const std::filesystem::path& path);

    // Call when the D2D device/rendertarget is recreated.
    void clearBitmaps();

private:
    struct Raw {
        std::vector<uint8_t> bgra;
        uint32_t width = 0, height = 0, stride = 0;
        bool ready = false;
    };
    struct Entry {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bmp;
        Raw raw;
        bool requested = false;
    };

    void workerLoop(std::stop_token st);
    bool loadRaw(const std::filesystem::path& path, Raw& out) const;

    std::jthread worker_;
    mutable std::mutex mu_;
    std::condition_variable_any cv_;
    std::deque<std::filesystem::path> queue_;
    std::unordered_map<std::filesystem::path, Entry> entries_;
};

}  // namespace tv
