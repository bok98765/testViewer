// tv/Core.h - Public API of the TestViewer core library.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tv {

// ---------------------------------------------------------------------------
// Image model
// ---------------------------------------------------------------------------

struct Size {
    uint32_t width = 0;
    uint32_t height = 0;
};

// Fully decoded image: 32bpp BGRA, top-down, tightly packed per `stride`.
// EXIF orientation is already applied to the pixels by the decoders;
// the original value is kept in `exifOrientation` for the info overlay.
struct DecodedImage {
    std::filesystem::path path;
    Size size{};
    uint32_t stride = 0;  // bytes per row (== width * 4)
    std::vector<uint8_t> pixels;

    int64_t frameDurationMs = 0;   // first frame delay, 0 = unknown/static
    uint32_t exifOrientation = 1;  // original EXIF orientation (1..8)
    uint64_t fileSize = 0;
    FILETIME mtime{};
    bool animated = false;         // multi-frame source (GIF/APNG/TIFF)

    size_t byteSize() const { return pixels.size(); }
};

using ImagePtr = std::shared_ptr<const DecodedImage>;

struct DecodeSettings {
    // Down-scale so the image fits inside this box (0 = no limit).
    uint32_t maxWidth = 0;
    uint32_t maxHeight = 0;
};

// ---------------------------------------------------------------------------
// Decoders
// ---------------------------------------------------------------------------

class IDecoder {
public:
    virtual ~IDecoder() = default;
    virtual const wchar_t* name() const = 0;
    // Magic-number sniffing (first bytes of the file).
    virtual bool matchesMagic(const uint8_t* data, size_t len) const = 0;
    virtual std::optional<DecodedImage> decode(
        const std::filesystem::path& path, const DecodeSettings& settings) const = 0;
};

class DecoderRegistry {
public:
    static DecoderRegistry& instance();

    void add(std::unique_ptr<IDecoder> decoder);
    // Sniffs magic bytes, then dispatches to the matching decoder.
    std::optional<DecodedImage> decode(
        const std::filesystem::path& path, const DecodeSettings& settings = {}) const;
    bool isSupported(const std::filesystem::path& path) const;
    // Supported extensions, lowercase, with leading dot.
    const std::vector<std::wstring>& extensions() const { return extensions_; }
    void addExtension(std::wstring ext);

private:
    std::vector<std::unique_ptr<IDecoder>> decoders_;
    std::vector<std::wstring> extensions_;
};

// Registers the built-in WIC + WebP decoders. Idempotent.
void registerDefaultDecoders();

// ---------------------------------------------------------------------------
// LRU image cache (byte-budgeted, thread safe)
// ---------------------------------------------------------------------------

class ImageCache {
public:
    explicit ImageCache(size_t maxBytes = 512ull << 20);

    ImagePtr get(const std::filesystem::path& path) const;
    void put(const std::filesystem::path& path, ImagePtr img);
    // Drop entries whose source file mtime is older than `t` (file changed).
    void evictOlderThan(FILETIME t);
    void clear();

    size_t byteSize() const;
    size_t maxBytes() const;
    void setMaxBytes(size_t bytes);
    size_t count() const;

private:
    using Key = std::filesystem::path;
    struct Entry { ImagePtr img; size_t bytes; };

    void evictLocked();

    mutable std::mutex mu_;
    size_t maxBytes_;
    size_t curBytes_ = 0;
    mutable std::list<std::pair<Key, Entry>> lru_;  // front = most recently used
    std::unordered_map<Key, std::list<std::pair<Key, Entry>>::iterator> map_;
};

// ---------------------------------------------------------------------------
// Prefetch scheduler (background decode, nearest-first priority)
// ---------------------------------------------------------------------------

class PrefetchScheduler {
public:
    explicit PrefetchScheduler(ImageCache& cache);
    ~PrefetchScheduler();

    // Ordered queue of image paths + index of the currently shown image.
    void setQueue(std::vector<std::filesystem::path> queue, size_t anchor);

    // Returns the image from cache, decoding it (on this thread if the
    // worker has not claimed it) when necessary. nullptr on failure.
    ImagePtr ensureDecoded(const std::filesystem::path& path,
                           const DecodeSettings& settings = {});

    // Stop all pending background work (called by destructor).
    void cancelPending();

    ImageCache& cache() { return cache_; }

private:
    void workerLoop(std::stop_token st);
    bool nextPendingLocked(std::filesystem::path& out) const;

    ImageCache& cache_;
    std::jthread worker_;

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::vector<std::filesystem::path> queue_;
    size_t anchor_ = 0;
    std::unordered_set<std::filesystem::path> inProgress_;
    std::unordered_set<std::filesystem::path> attempted_;
    DecodeSettings settings_{};
};

// ---------------------------------------------------------------------------
// Folder listing + natural sort (StrCmpLogicalW)
// ---------------------------------------------------------------------------

bool naturalLess(const std::wstring& a, const std::wstring& b);

// Lists supported images in `dir`, naturally sorted.
std::vector<std::filesystem::path> listImages(const std::filesystem::path& dir);

// ---------------------------------------------------------------------------
// Settings (simple key=value file in %LOCALAPPDATA%\TestViewer\settings.ini)
// ---------------------------------------------------------------------------

struct AppSettings {
    int cacheMB = 512;
    int slideshowSec = 3;
    bool fitOnOpen = true;
    bool showInfo = true;
    bool showFilmstrip = true;
    bool darkTitlebar = true;
    std::filesystem::path lastFolder;
    // Window placement (physical pixels).
    int winX = CW_USEDEFAULT, winY = CW_USEDEFAULT;
    int winW = CW_USEDEFAULT, winH = CW_USEDEFAULT;

    static AppSettings load();
    void save() const;
    static std::filesystem::path filePath();
    // Explicit-path variants (used by unit tests).
    static AppSettings loadFrom(const std::filesystem::path& file);
    bool saveTo(const std::filesystem::path& file) const;
};

// ---------------------------------------------------------------------------
// File association (HKCU, no elevation)
// ---------------------------------------------------------------------------

bool isFileAssociated(const std::wstring& ext);          // ext like L".jpg"
bool registerFileAssociation(const std::wstring& ext);
bool unregisterFileAssociation(const std::wstring& ext);

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

std::wstring utf8ToWide(std::string_view s);
std::string wideToUtf8(std::wstring_view s);
std::wstring formatFileSize(uint64_t bytes);
std::wstring formatDuration(int64_t ms);
std::wstring formatTime(const FILETIME& ft);

}  // namespace tv
