#include "Decoders.h"

#include <webp/decode.h>

#include <algorithm>
#include <fstream>
#include <vector>

namespace tv {

bool WebpDecoder::matchesMagic(const uint8_t* d, size_t len) const {
    return len >= 12 && std::memcmp(d, "RIFF", 4) == 0 && std::memcmp(d + 8, "WEBP", 4) == 0;
}

std::optional<DecodedImage> WebpDecoder::decode(
    const std::filesystem::path& path, const DecodeSettings& settings) const {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return std::nullopt;
    const auto fileSize = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> file(fileSize);
    if (!f.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(fileSize)))
        return std::nullopt;

    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config)) return std::nullopt;

    // Down-scale during decode when requested.
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(file.data(), file.size(), &features) != VP8_STATUS_OK)
        return std::nullopt;

    uint32_t w = static_cast<uint32_t>(features.width);
    uint32_t h = static_cast<uint32_t>(features.height);
    if (w == 0 || h == 0) return std::nullopt;

    if (settings.maxWidth > 0 && settings.maxHeight > 0 &&
        (w > settings.maxWidth || h > settings.maxHeight)) {
        double scale = std::min(settings.maxWidth / static_cast<double>(w),
                                settings.maxHeight / static_cast<double>(h));
        config.options.use_scaling = 1;
        config.options.scaled_width = std::max(1u, static_cast<uint32_t>(w * scale));
        config.options.scaled_height = std::max(1u, static_cast<uint32_t>(h * scale));
        w = config.options.scaled_width;
        h = config.options.scaled_height;
    }

    config.output.colorspace = MODE_BGRA;
    config.output.is_external_memory = 1;

    DecodedImage img;
    img.path = path;
    img.size = {w, h};
    img.stride = w * 4;
    img.pixels.resize(static_cast<size_t>(img.stride) * h);
    img.animated = features.has_animation != 0;
    img.fileSize = fileSize;

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        img.fileSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        img.mtime = fad.ftLastWriteTime;
    }

    // Decode straight into the external BGRA buffer.
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = img.pixels.data();
    config.output.u.RGBA.stride = static_cast<int>(img.stride);
    config.output.u.RGBA.size = img.pixels.size();

    if (WebPDecode(file.data(), file.size(), &config) != VP8_STATUS_OK)
        return std::nullopt;

    return img;
}

}  // namespace tv
