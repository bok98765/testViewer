#include "tv/Core.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace tv {

std::wstring utf8ToWide(std::string_view s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string wideToUtf8(std::wstring_view s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string u(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        u.data(), n, nullptr, nullptr);
    return u;
}

std::wstring formatFileSize(uint64_t bytes) {
    static const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    std::wostringstream os;
    os << std::fixed << std::setprecision(u == 0 ? 0 : 1) << v << L' ' << units[u];
    return os.str();
}

std::wstring formatDuration(int64_t ms) {
    if (ms <= 0) return L"-";
    std::wostringstream os;
    os << (ms / 1000) << L'.' << std::setw(3) << std::setfill(L'0') << (ms % 1000) << L"s";
    return os.str();
}

std::wstring formatTime(const FILETIME& ft) {
    FILETIME local{};
    if (!FileTimeToLocalFileTime(&ft, &local)) return L"-";
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&local, &st)) return L"-";
    std::wostringstream os;
    os << std::setw(4) << std::setfill(L'0') << st.wYear << L'-'
       << std::setw(2) << st.wMonth << L'-'
       << std::setw(2) << st.wDay << L' '
       << std::setw(2) << st.wHour << L':'
       << std::setw(2) << st.wMinute;
    return os.str();
}

}  // namespace tv
