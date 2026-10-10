#include "tv/Core.h"

#include <algorithm>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

namespace tv {

bool naturalLess(const std::wstring& a, const std::wstring& b) {
    // Explorer-style natural ordering (numbers compared numerically).
    return StrCmpLogicalW(a.c_str(), b.c_str()) < 0;
}

std::vector<std::filesystem::path> listImages(const std::filesystem::path& dir) {
    registerDefaultDecoders();
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (DecoderRegistry::instance().isSupported(e.path()))
            out.push_back(e.path());
    }
    std::sort(out.begin(), out.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  return naturalLess(a.filename().wstring(), b.filename().wstring());
              });
    return out;
}

}  // namespace tv
