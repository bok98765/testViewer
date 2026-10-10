#include "Decoders.h"

#include <algorithm>
#include <fstream>

namespace tv {

namespace {

// Reads the first bytes of a file for magic-number sniffing.
bool readFileHeader(const std::filesystem::path& path, uint8_t* buf, size_t n, size_t& got) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(n));
    got = static_cast<size_t>(f.gcount());
    return got > 0;
}

}  // namespace

DecoderRegistry& DecoderRegistry::instance() {
    static DecoderRegistry reg;
    return reg;
}

void DecoderRegistry::add(std::unique_ptr<IDecoder> decoder) {
    decoders_.push_back(std::move(decoder));
}

void DecoderRegistry::addExtension(std::wstring ext) {
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (std::find(extensions_.begin(), extensions_.end(), ext) == extensions_.end())
        extensions_.push_back(std::move(ext));
}

std::optional<DecodedImage> DecoderRegistry::decode(
    const std::filesystem::path& path, const DecodeSettings& settings) const {
    uint8_t header[32] = {};
    size_t got = 0;
    if (!readFileHeader(path, header, sizeof(header), got))
        return std::nullopt;

    for (const auto& d : decoders_) {
        if (d->matchesMagic(header, got)) {
            if (auto img = d->decode(path, settings))
                return img;
            // Decoder matched the magic but failed: don't try others blindly.
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool DecoderRegistry::isSupported(const std::filesystem::path& path) const {
    auto ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return std::find(extensions_.begin(), extensions_.end(), ext) != extensions_.end();
}

void registerDefaultDecoders() {
    static bool once = [] {
        auto& reg = DecoderRegistry::instance();
        reg.add(std::make_unique<WebpDecoder>());  // RIFF/WEBP first
        reg.add(std::make_unique<WicDecoder>());
        for (const wchar_t* e : {L".jpg", L".jpeg", L".jpe", L".png", L".gif",
                                 L".bmp", L".dib", L".tif", L".tiff", L".ico",
                                 L".webp", L".jfif", L".heic", L".heif", L".avif"})
            reg.addExtension(e);
        return true;
    }();
    (void)once;
}

}  // namespace tv
