#include "tv/Core.h"

#include <algorithm>

namespace tv {

PrefetchScheduler::PrefetchScheduler(ImageCache& cache) : cache_(cache) {
    worker_ = std::jthread([this](std::stop_token st) { workerLoop(st); });
}

PrefetchScheduler::~PrefetchScheduler() {
    worker_.request_stop();
    cv_.notify_all();
}

void PrefetchScheduler::setQueue(std::vector<std::filesystem::path> queue, size_t anchor) {
    {
        std::lock_guard lock(mu_);
        queue_ = std::move(queue);
        anchor_ = anchor < queue_.size() ? anchor : 0;
        // Forget paths that left the queue so they can be re-decoded later.
        for (auto it = attempted_.begin(); it != attempted_.end();) {
            if (std::find(queue_.begin(), queue_.end(), *it) == queue_.end())
                it = attempted_.erase(it);
            else
                ++it;
        }
    }
    cv_.notify_all();
}

ImagePtr PrefetchScheduler::ensureDecoded(const std::filesystem::path& path,
                                          const DecodeSettings& settings) {
    if (auto img = cache_.get(path))
        return img;

    {
        std::unique_lock lock(mu_);
        settings_ = settings;
        if (inProgress_.count(path)) {
            // Worker owns this decode: wait for it to finish.
            cv_.wait(lock, [&] {
                return !inProgress_.count(path) || cache_.get(path) != nullptr;
            });
            if (auto img = cache_.get(path))
                return img;
            if (worker_.get_id() == std::this_thread::get_id())
                return nullptr;
            // Worker failed; fall through and try on this thread.
        }
        inProgress_.insert(path);
    }

    // Decode on the calling thread.
    auto opt = DecoderRegistry::instance().decode(path, settings);
    ImagePtr result;
    {
        std::lock_guard lock(mu_);
        inProgress_.erase(path);
        attempted_.insert(path);
        if (opt) {
            result = std::make_shared<const DecodedImage>(std::move(*opt));
            cache_.put(path, result);
        }
    }
    cv_.notify_all();
    return result;
}

void PrefetchScheduler::cancelPending() {
    std::lock_guard lock(mu_);
    queue_.clear();
    attempted_.clear();
}

bool PrefetchScheduler::nextPendingLocked(std::filesystem::path& out) const {
    // Nearest-first around the anchor: +1, -1, +2, -2, ...
    const size_t n = queue_.size();
    for (size_t d = 1; d <= n; ++d) {
        // alternate forward/backward
        for (int sign : {+1, -1}) {
            if (sign > 0) {
                if (anchor_ + d < n) {
                    const auto& p = queue_[anchor_ + d];
                    if (!attempted_.count(p) && !inProgress_.count(p) && !cache_.get(p)) {
                        out = p;
                        return true;
                    }
                }
            } else {
                if (d <= anchor_) {
                    const auto& p = queue_[anchor_ - d];
                    if (!attempted_.count(p) && !inProgress_.count(p) && !cache_.get(p)) {
                        out = p;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void PrefetchScheduler::workerLoop(std::stop_token st) {
    for (;;) {
        std::filesystem::path path;
        DecodeSettings settings;
        {
            std::unique_lock lock(mu_);
            cv_.wait(lock, st, [&] { return nextPendingLocked(path); });
            if (st.stop_requested()) return;
            inProgress_.insert(path);
            settings = settings_;
        }

        auto opt = DecoderRegistry::instance().decode(path, settings);

        {
            std::lock_guard lock(mu_);
            inProgress_.erase(path);
            attempted_.insert(path);
            if (opt)
                cache_.put(path, std::make_shared<const DecodedImage>(std::move(*opt)));
        }
        cv_.notify_all();
        if (st.stop_requested()) return;
    }
}

}  // namespace tv
