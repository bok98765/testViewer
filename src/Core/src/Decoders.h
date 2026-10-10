// Internal decoder implementations.
#pragma once

#include "tv/Core.h"

namespace tv {

// WIC-based decoder: JPEG, PNG, GIF, BMP, TIFF, ICO, HEIF/AVIF (if the
// codec is installed on the machine). Applies EXIF orientation to pixels.
class WicDecoder : public IDecoder {
public:
    const wchar_t* name() const override { return L"WIC"; }
    bool matchesMagic(const uint8_t* data, size_t len) const override;
    std::optional<DecodedImage> decode(
        const std::filesystem::path& path, const DecodeSettings& settings) const override;
};

// libwebp-based decoder: WebP (still images; animated uses first frame).
class WebpDecoder : public IDecoder {
public:
    const wchar_t* name() const override { return L"WebP"; }
    bool matchesMagic(const uint8_t* data, size_t len) const override;
    std::optional<DecodedImage> decode(
        const std::filesystem::path& path, const DecodeSettings& settings) const override;
};

// Applies EXIF orientation (2..8) to a BGRA buffer in place / by copy.
// Returns the resulting size, or {0,0} when orientation is 1 (no-op).
Size applyExifOrientation(DecodedImage& img, uint32_t orientation);

}  // namespace tv
