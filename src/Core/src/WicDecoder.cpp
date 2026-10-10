#include "Decoders.h"

#include <ole2.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace tv {

namespace {

// RAII: make sure COM is initialized on the calling thread (decoders run on
// both UI and worker threads).
struct ComScope {
    HRESULT hr = CO_E_NOTINITIALIZED;
    ComScope() {
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    }
    ~ComScope() {
        if (hr == S_OK) CoUninitialize();
    }
    bool ok() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

bool startsWith(const uint8_t* d, size_t len, const char* sig) {
    size_t n = std::strlen(sig);
    return len >= n && std::memcmp(d, sig, n) == 0;
}

bool readUint(const ComPtr<IWICMetadataQueryReader>& qr, const wchar_t* name, uint32_t& out) {
    PROPVARIANT var;
    PropVariantInit(&var);
    if (FAILED(qr->GetMetadataByName(name, &var)))
        return false;
    bool ok = true;
    switch (var.vt) {
        case VT_I2:  out = static_cast<uint32_t>(static_cast<int32_t>(var.iVal)); break;
        case VT_UI2: out = var.uiVal; break;
        case VT_I4:  out = static_cast<uint32_t>(static_cast<int32_t>(var.lVal)); break;
        case VT_UI4: out = var.ulVal; break;
        default: ok = false; break;
    }
    PropVariantClear(&var);
    return ok && out >= 1 && out <= 8;
}

bool readDouble(const ComPtr<IWICMetadataQueryReader>& qr, const wchar_t* name, double& out) {
    PROPVARIANT var;
    PropVariantInit(&var);
    if (FAILED(qr->GetMetadataByName(name, &var)))
        return false;
    const bool ok = var.vt == VT_R8;
    if (ok) out = var.dblVal;
    PropVariantClear(&var);
    return ok;
}

}  // namespace

// ---------------------------------------------------------------------------
// Magic numbers
// ---------------------------------------------------------------------------

bool WicDecoder::matchesMagic(const uint8_t* d, size_t len) const {
    if (len >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return true;   // JPEG
    if (len >= 8 && startsWith(d, len, "\x89PNG\r\n\x1a\n")) return true;        // PNG
    if (len >= 6 && startsWith(d, len, "GIF8")) return true;                     // GIF
    if (len >= 2 && startsWith(d, len, "BM")) return true;                       // BMP
    if (len >= 4 && (std::memcmp(d, "II*\0", 4) == 0 ||                     // TIFF LE
                     std::memcmp(d, "MM\0*", 4) == 0)) return true;              // TIFF BE
    if (len >= 4 && d[0] == 0 && d[1] == 0 && d[2] == 1 && d[3] == 0) return true;  // ICO
    if (len >= 3 && startsWith(d, len, "RIC")) return true;                      // JPEG XR
    // HEIF / AVIF: ISO base media "ftyp" box with a known brand.
    if (len >= 12 && std::memcmp(d + 4, "ftyp", 4) == 0) {
        static const char* brands[] = {"heic", "heix", "heim", "hevc", "hevx",
                                       "mif1", "msf1", "avif", "avis"};
        for (const char* b : brands)
            if (std::memcmp(d + 8, b, 4) == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Decode
// ---------------------------------------------------------------------------

std::optional<DecodedImage> WicDecoder::decode(
    const std::filesystem::path& path, const DecodeSettings& settings) const {
    ComScope com;
    if (!com.ok()) return std::nullopt;

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return std::nullopt;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)))
        return std::nullopt;

    UINT frameCount = 0;
    decoder->GetFrameCount(&frameCount);

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)))
        return std::nullopt;

    DecodedImage img;
    img.path = path;

    // --- metadata -----------------------------------------------------------
    ComPtr<IWICMetadataQueryReader> frameQr;
    if (SUCCEEDED(frame->GetMetadataQueryReader(&frameQr)) && frameQr) {
        uint32_t orient = 1;
        if (readUint(frameQr, L"System.Photo.Orientation", orient))
            img.exifOrientation = orient;

        double delaySec = 0;
        if (readDouble(frameQr, L"/FrameDelay", delaySec) && delaySec > 0)
            img.frameDurationMs = static_cast<int64_t>(delaySec * 1000.0);
    }
    img.animated = frameCount > 1;
    if (img.animated && img.frameDurationMs == 0)
        img.frameDurationMs = 100;  // sane default for animation hints

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        img.fileSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        img.mtime = fad.ftLastWriteTime;
    }

    // --- size + optional down-scale -----------------------------------------
    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0)
        return std::nullopt;

    ComPtr<IWICBitmapSource> source = frame;
    if (settings.maxWidth > 0 && settings.maxHeight > 0 &&
        (w > settings.maxWidth || h > settings.maxHeight)) {
        double scale = std::min(settings.maxWidth / static_cast<double>(w),
                                settings.maxHeight / static_cast<double>(h));
        UINT nw = static_cast<UINT>(w * scale), nh = static_cast<UINT>(h * scale);
        if (nw == 0) nw = 1;
        if (nh == 0) nh = 1;
        ComPtr<IWICBitmapScaler> scaler;
        if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(frame.Get(), nw, nh,
                                         WICBitmapInterpolationModeHighQualityCubic))) {
            source = scaler;
            w = nw;
            h = nh;
        }
    }

    // --- convert to BGRA ------------------------------------------------------
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(factory->CreateFormatConverter(&conv)))
        return std::nullopt;
    if (FAILED(conv->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0f,
                                WICBitmapPaletteTypeCustom)))
        return std::nullopt;

    img.size = {w, h};
    img.stride = w * 4;
    img.pixels.resize(static_cast<size_t>(img.stride) * h);
    if (FAILED(conv->CopyPixels(nullptr, img.stride,
                                static_cast<UINT>(img.pixels.size()), img.pixels.data())))
        return std::nullopt;

    // --- EXIF orientation applied to pixels -----------------------------------
    applyExifOrientation(img, img.exifOrientation);

    return img;
}

// ---------------------------------------------------------------------------
// EXIF orientation transforms (BGRA)
// ---------------------------------------------------------------------------

Size applyExifOrientation(DecodedImage& img, uint32_t o) {
    if (o <= 1 || o > 8 || img.pixels.empty()) return img.size;

    const uint32_t w = img.size.width, h = img.size.height;
    const uint32_t sp = img.stride;
    const uint8_t* src = img.pixels.data();

    auto px = [&](uint32_t x, uint32_t y) -> const uint8_t* { return src + y * sp + x * 4; };

    std::vector<uint8_t> dst;
    uint32_t dw = w, dh = h;
    const bool swap = (o >= 5);  // 90-degree rotations swap dimensions
    if (swap) { dw = h; dh = w; }
    dst.resize(static_cast<size_t>(dw) * 4 * dh);
    const uint32_t dp = dw * 4;

    for (uint32_t y = 0; y < dh; ++y) {
        uint8_t* drow = dst.data() + static_cast<size_t>(y) * dp;
        for (uint32_t x = 0; x < dw; ++x) {
            const uint8_t* s = nullptr;
            switch (o) {
                case 2: s = px(w - 1 - x, y); break;              // mirror H
                case 3: s = px(w - 1 - x, h - 1 - y); break;      // rotate 180
                case 4: s = px(x, h - 1 - y); break;              // mirror V
                case 5: s = px(y, x); break;                      // transpose
                case 6: s = px(y, h - 1 - x); break;              // rotate 90 CW
                case 7: s = px(w - 1 - y, h - 1 - x); break;      // transverse
                case 8: s = px(w - 1 - y, x); break;              // rotate 270 CW
                default: s = px(x, y); break;
            }
            std::memcpy(drow + x * 4, s, 4);
        }
    }

    img.pixels = std::move(dst);
    img.size = {dw, dh};
    img.stride = dp;
    return img.size;
}

}  // namespace tv
