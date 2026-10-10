#include "tv/Core.h"

#include <objbase.h>
#include <shlobj.h>

#include <fstream>

namespace tv {

std::filesystem::path AppSettings::filePath() {
    wchar_t* local = nullptr;
    std::filesystem::path base = L".";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        base = local;
        CoTaskMemFree(local);
    }
    return base / L"TestViewer" / L"settings.ini";
}

namespace {

struct KV {
    std::wstring key;
    std::wstring value;
};

std::wstring trim(const std::wstring& s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return L"";
    size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

}  // namespace

AppSettings AppSettings::load() {
    return loadFrom(filePath());
}

AppSettings AppSettings::loadFrom(const std::filesystem::path& file) {
    AppSettings s;
    std::ifstream f(file, std::ios::binary);
    if (!f) return s;

    std::vector<KV> kvs;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        kvs.push_back({utf8ToWide(line.substr(0, eq)), utf8ToWide(line.substr(eq + 1))});
    }

    auto getStr = [&](const wchar_t* k, std::wstring& out) {
        for (auto& kv : kvs)
            if (kv.key == k) { out = trim(kv.value); return; }
    };
    auto getInt = [&](const wchar_t* k, int& out) {
        for (auto& kv : kvs)
            if (kv.key == k) { out = _wtoi(trim(kv.value).c_str()); return; }
    };
    auto getBool = [&](const wchar_t* k, bool& out) {
        for (auto& kv : kvs)
            if (kv.key == k) { out = trim(kv.value) == L"1"; return; }
    };

    getInt(L"cacheMB", s.cacheMB);
    getInt(L"slideshowSec", s.slideshowSec);
    getBool(L"fitOnOpen", s.fitOnOpen);
    getBool(L"showInfo", s.showInfo);
    getBool(L"showFilmstrip", s.showFilmstrip);
    getBool(L"darkTitlebar", s.darkTitlebar);
    std::wstring folder;
    getStr(L"lastFolder", folder);
    if (!folder.empty()) s.lastFolder = folder;
    getInt(L"winX", s.winX);
    getInt(L"winY", s.winY);
    getInt(L"winW", s.winW);
    getInt(L"winH", s.winH);
    if (s.cacheMB < 64) s.cacheMB = 64;
    if (s.slideshowSec < 1) s.slideshowSec = 1;
    return s;
}

void AppSettings::save() const {
    std::error_code ec;
    std::filesystem::create_directories(filePath().parent_path(), ec);
    saveTo(filePath());
}

bool AppSettings::saveTo(const std::filesystem::path& file) const {
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    auto put = [&](const wchar_t* k, const std::string& v) { f << wideToUtf8(k) << '=' << v << '\n'; };
    put(L"cacheMB", std::to_string(cacheMB));
    put(L"slideshowSec", std::to_string(slideshowSec));
    put(L"fitOnOpen", fitOnOpen ? "1" : "0");
    put(L"showInfo", showInfo ? "1" : "0");
    put(L"showFilmstrip", showFilmstrip ? "1" : "0");
    put(L"darkTitlebar", darkTitlebar ? "1" : "0");
    put(L"lastFolder", wideToUtf8(lastFolder.wstring()));
    put(L"winX", std::to_string(winX));
    put(L"winY", std::to_string(winY));
    put(L"winW", std::to_string(winW));
    put(L"winH", std::to_string(winH));
    return f.good();
}

}  // namespace tv
