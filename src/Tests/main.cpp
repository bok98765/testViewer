// TestViewer unit tests (console app).
// Lightweight CHECK-based harness: prints failures, exits non-zero if any occur.
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <wincodec.h>
#include <propsys.h>

#include <webp/encode.h>

#include "tv/Core.h"
#include "../Core/src/Decoders.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(cond)) {                                                  \
            ++g_failures;                                               \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!((a) == (b))) {                                                   \
            ++g_failures;                                                      \
            std::printf("FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b); \
        }                                                                      \
    } while (0)

// ---------------------------------------------------------------- natural sort

void testNaturalSort() {
    std::vector<std::wstring> names = {
        L"img10.png", L"img2.png", L"img1.png", L"IMG3.png",
        L"photo20.jpg", L"photo3.jpg", L"z.bmp", L"a.gif",
    };
    std::sort(names.begin(), names.end(), tv::naturalLess);
    CHECK_EQ(names[0], L"a.gif");
    CHECK_EQ(names[1], L"img1.png");
    CHECK_EQ(names[2], L"img2.png");
    CHECK_EQ(names[4], L"img10.png");
    CHECK_EQ(names[5], L"photo3.jpg");
    CHECK_EQ(names[6], L"photo20.jpg");
    CHECK_EQ(names[7], L"z.bmp");

    // Pure numbers sort numerically, not lexically.
    std::vector<std::wstring> nums = {L"100", L"20", L"3", L"1"};
    std::sort(nums.begin(), nums.end(), tv::naturalLess);
    CHECK_EQ(nums[0], L"1");
    CHECK_EQ(nums[3], L"100");
}

// --------------------------------------------------------------------- cache

tv::ImagePtr makeImage(int w, int h, uint8_t v) {
    auto img = std::make_shared<tv::DecodedImage>();
    img->size.width = static_cast<uint32_t>(w);
    img->size.height = static_cast<uint32_t>(h);
    img->stride = static_cast<uint32_t>(w) * 4;
    img->pixels.assign(static_cast<size_t>(w) * h * 4, v);
    return img;
}

void testImageCache() {
    tv::ImageCache cache(1000);  // 1000 bytes budget

    cache.put(L"a.png", makeImage(10, 10, 1));  // 400 B
    cache.put(L"b.png", makeImage(10, 10, 2));  // 400 B
    CHECK(cache.get(L"a.png") != nullptr);
    CHECK(cache.get(L"b.png") != nullptr);
    CHECK_EQ(cache.byteSize(), 800u);

    // Touch 'b' so 'a' becomes LRU, then insert 400 B -> 'a' must be evicted.
    CHECK(cache.get(L"b.png") != nullptr);
    cache.put(L"c.png", makeImage(10, 10, 3));
    CHECK(cache.get(L"a.png") == nullptr);
    CHECK(cache.get(L"b.png") != nullptr);
    CHECK(cache.get(L"c.png") != nullptr);

    // Oversized image evicts itself (budget exceeded -> LRU tail dropped).
    cache.put(L"big.png", makeImage(100, 100, 0));  // 40000 B > budget
    CHECK(cache.get(L"big.png") == nullptr);

    // Replacing an existing key keeps a single entry.
    cache.put(L"small.png", makeImage(5, 5, 1));
    cache.put(L"small.png", makeImage(5, 5, 2));
    CHECK_EQ(cache.count(), 1u);

    cache.clear();
    CHECK_EQ(cache.byteSize(), 0u);
    CHECK(cache.get(L"c.png") == nullptr);
}

// ---------------------------------------------------------------- prefetch

void testPrefetch() {
    tv::ImageCache cache(16ull << 20);
    tv::PrefetchScheduler sched(cache);

    // ensureDecoded on a bogus path must fail cleanly, not crash.
    CHECK(sched.ensureDecoded(L"Z:\\definitely\\not\\here.png") == nullptr);
}

// -------------------------------------------------------------------- settings

void testSettingsRoundTrip() {
    tv::AppSettings s;
    s.cacheMB = 256;
    s.slideshowSec = 7;
    s.fitOnOpen = false;
    s.showInfo = false;
    s.showFilmstrip = true;
    s.darkTitlebar = false;
    s.lastFolder = L"C:\\Windows\\Media";

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / L"tv_settings_test.ini";
    CHECK(s.saveTo(tmp));

    tv::AppSettings r = tv::AppSettings::loadFrom(tmp);
    CHECK_EQ(r.cacheMB, s.cacheMB);
    CHECK_EQ(r.slideshowSec, s.slideshowSec);
    CHECK_EQ(r.fitOnOpen, s.fitOnOpen);
    CHECK_EQ(r.showInfo, s.showInfo);
    CHECK_EQ(r.showFilmstrip, s.showFilmstrip);
    CHECK_EQ(r.darkTitlebar, s.darkTitlebar);
    CHECK_EQ(r.lastFolder, s.lastFolder);

    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

// ------------------------------------------------- WIC encode -> decode round trip

void testWicRoundTrip() {
    struct Case {
        const wchar_t* ext;
        GUID codec;
    } cases[] = {
        {L".png", GUID_ContainerFormatPng},
        {L".bmp", GUID_ContainerFormatBmp},
        {L".jpg", GUID_ContainerFormatJpeg},
        {L".tif", GUID_ContainerFormatTiff},
    };

    const int w = 8, h = 6;
    std::vector<uint8_t> src(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &src[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = static_cast<uint8_t>(x * 30);
            p[1] = static_cast<uint8_t>(y * 40);
            p[2] = 0x80;
            p[3] = 0xFF;
        }
    }

    const std::filesystem::path tmp = std::filesystem::temp_directory_path();
    for (const auto& c : cases) {
        const std::filesystem::path file = tmp / (std::wstring(L"tv_roundtrip") + c.ext);

        // --- encode with WIC
        {
            IWICImagingFactory* factory = nullptr;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
                std::printf("FAIL: WIC factory unavailable\n");
                ++g_failures;
                continue;
            }
            IWICStream* stream = nullptr;
            IWICBitmapEncoder* encoder = nullptr;
            IWICBitmapFrameEncode* frame = nullptr;
            IPropertyBag2* props = nullptr;
            bool ok = SUCCEEDED(factory->CreateStream(&stream)) &&
                      SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE)) &&
                      SUCCEEDED(factory->CreateEncoder(c.codec, nullptr, &encoder)) &&
                      SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
                      SUCCEEDED(encoder->CreateNewFrame(&frame, &props));
            if (ok) ok = SUCCEEDED(frame->Initialize(props));
            if (ok) {
                frame->SetSize(w, h);
                WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
                frame->SetPixelFormat(&fmt);
                frame->WritePixels(h, w * 4, static_cast<UINT>(src.size()), src.data());
                frame->Commit();
                encoder->Commit();
            }
            if (props) props->Release();
            if (frame) frame->Release();
            if (encoder) encoder->Release();
            if (stream) stream->Release();
            factory->Release();
            if (!ok) {
                std::printf("FAIL: encode %ls\n", c.ext);
                ++g_failures;
                continue;
            }
        }

        // --- decode through the registry (magic-number sniffing)
        auto img = tv::DecoderRegistry::instance().decode(file);
        CHECK(img.has_value());
        if (!img) continue;
        CHECK_EQ(img->size.width, static_cast<uint32_t>(w));
        CHECK_EQ(img->size.height, static_cast<uint32_t>(h));
        CHECK_EQ(img->pixels.size(), static_cast<size_t>(w) * h * 4);

        // Lossy codecs (JPEG) shift pixels; only check exact match for lossless.
        const bool lossless = c.codec == GUID_ContainerFormatPng ||
                              c.codec == GUID_ContainerFormatBmp ||
                              c.codec == GUID_ContainerFormatTiff;
        if (lossless) {
            CHECK_EQ(img->pixels[0], src[0]);
            CHECK_EQ(img->pixels[1], src[1]);
            CHECK_EQ(img->pixels[3], 0xFF);
        }

        std::error_code ec;
        std::filesystem::remove(file, ec);
    }
}

// ------------------------------------------------------- WebP sniff + round trip

void testWebpRoundTrip() {
    // Encode a small image with libwebp, write it to disk, then decode it back
    // through the registry (magic sniffing must pick the WebP decoder).
    const int w = 16, h = 12;
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = static_cast<uint8_t>(x * 16);
            p[1] = static_cast<uint8_t>(y * 20);
            p[2] = 0x40;
            p[3] = 0xFF;
        }

    uint8_t* encoded = nullptr;
    const size_t encodedSize = WebPEncodeRGBA(rgba.data(), w, h, w * 4, 80.0f, &encoded);
    if (encodedSize == 0 || !encoded) {
        std::printf("FAIL: WebPEncodeRGBA failed\n");
        ++g_failures;
        return;
    }

    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / L"tv_roundtrip.webp";
    {
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(encoded),
                  static_cast<std::streamsize>(encodedSize));
    }
    WebPFree(encoded);

    auto img = tv::DecoderRegistry::instance().decode(file);
    CHECK(img.has_value());
    if (img) {
        CHECK_EQ(img->size.width, static_cast<uint32_t>(w));
        CHECK_EQ(img->size.height, static_cast<uint32_t>(h));
        CHECK_EQ(img->pixels.size(), static_cast<size_t>(w) * h * 4);
    }

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

// ---------------------------------------------------------------- EXIF orientation

void testExifOrientation() {
    // 3x2 image where every pixel encodes its own (col, row).
    const uint32_t w = 3, h = 2;
    tv::DecodedImage base;
    base.size = {w, h};
    base.stride = w * 4;
    base.pixels.resize(static_cast<size_t>(base.stride) * h);
    for (uint32_t r = 0; r < h; ++r) {
        for (uint32_t c = 0; c < w; ++c) {
            uint8_t* p = base.pixels.data() + r * base.stride + c * 4;
            p[0] = static_cast<uint8_t>(c);
            p[1] = static_cast<uint8_t>(r);
            p[3] = 0xFF;
        }
    }

    for (uint32_t o = 1; o <= 8; ++o) {
        tv::DecodedImage img = base;
        const tv::Size out = tv::applyExifOrientation(img, o);
        const bool swap = (o >= 5);
        const uint32_t dw = swap ? h : w, dh = swap ? w : h;
        CHECK_EQ(out.width, dw);
        CHECK_EQ(out.height, dh);
        CHECK_EQ(img.stride, dw * 4);
        CHECK_EQ(img.pixels.size(), static_cast<size_t>(dw) * dh * 4);

        bool ok = true;
        for (uint32_t y = 0; y < dh && ok; ++y) {
            for (uint32_t x = 0; x < dw; ++x) {
                uint32_t sc = 0, sr = 0;
                switch (o) {
                    case 2: sc = w - 1 - x; sr = y; break;
                    case 3: sc = w - 1 - x; sr = h - 1 - y; break;
                    case 4: sc = x; sr = h - 1 - y; break;
                    case 5: sc = y; sr = x; break;
                    case 6: sc = y; sr = h - 1 - x; break;
                    case 7: sc = w - 1 - y; sr = h - 1 - x; break;
                    case 8: sc = w - 1 - y; sr = x; break;
                    default: sc = x; sr = y; break;
                }
                const uint8_t* p = img.pixels.data() + y * img.stride + x * 4;
                if (p[0] != sc || p[1] != sr) {
                    std::printf("FAIL orientation %u at (%u,%u): got (%u,%u) want (%u,%u)\n",
                                o, x, y, p[0], p[1], sc, sr);
                    ok = false;
                }
            }
        }
        CHECK(ok);
    }
}

}  // namespace

int wmain(int, wchar_t**) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    tv::registerDefaultDecoders();

    testNaturalSort();
    testImageCache();
    testPrefetch();
    testSettingsRoundTrip();
    testWicRoundTrip();
    testWebpRoundTrip();
    testExifOrientation();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    CoUninitialize();
    return g_failures == 0 ? 0 : 1;
}
