#include "tv/Core.h"

namespace tv {

ImageCache::ImageCache(size_t maxBytes) : maxBytes_(maxBytes) {}

ImagePtr ImageCache::get(const std::filesystem::path& path) const {
    std::lock_guard lock(mu_);
    auto it = map_.find(path);
    if (it == map_.end()) return nullptr;
    // Move to front (most recently used).
    lru_.splice(lru_.begin(), lru_, it->second);
    return it->second->second.img;
}

void ImageCache::put(const std::filesystem::path& path, ImagePtr img) {
    if (!img) return;
    std::lock_guard lock(mu_);
    auto it = map_.find(path);
    if (it != map_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second);
        it->second->second = Entry{std::move(img), 0};
        it->second->second.bytes = it->second->second.img->byteSize();
    } else {
        size_t bytes = img->byteSize();
        lru_.emplace_front(path, Entry{std::move(img), bytes});
        map_[path] = lru_.begin();
        curBytes_ += bytes;
    }
    evictLocked();
}

void ImageCache::evictOlderThan(FILETIME t) {
    std::lock_guard lock(mu_);
    for (auto it = lru_.begin(); it != lru_.end();) {
        const FILETIME& mt = it->second.img->mtime;
        if (CompareFileTime(&mt, &t) < 0) {
            curBytes_ -= it->second.bytes;
            map_.erase(it->first);
            it = lru_.erase(it);
        } else {
            ++it;
        }
    }
}

void ImageCache::clear() {
    std::lock_guard lock(mu_);
    lru_.clear();
    map_.clear();
    curBytes_ = 0;
}

size_t ImageCache::byteSize() const {
    std::lock_guard lock(mu_);
    return curBytes_;
}

size_t ImageCache::maxBytes() const {
    std::lock_guard lock(mu_);
    return maxBytes_;
}

void ImageCache::setMaxBytes(size_t bytes) {
    std::lock_guard lock(mu_);
    maxBytes_ = bytes;
    evictLocked();
}

size_t ImageCache::count() const {
    std::lock_guard lock(mu_);
    return map_.size();
}

void ImageCache::evictLocked() {
    while (curBytes_ > maxBytes_ && !lru_.empty()) {
        curBytes_ -= lru_.back().second.bytes;
        map_.erase(lru_.back().first);
        lru_.pop_back();
    }
}

}  // namespace tv
