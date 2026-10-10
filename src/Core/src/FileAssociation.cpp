#include "tv/Core.h"

#include <shlwapi.h>

#include <algorithm>

#pragma comment(lib, "shlwapi.lib")

namespace tv {

namespace {

// Executable path used for the association.
std::wstring exePath() {
    wchar_t buf[32768];
    DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
    return (n > 0 && n < ARRAYSIZE(buf)) ? std::wstring(buf, n) : std::wstring();
}

std::wstring progIdFor(const std::wstring& ext) {
    std::wstring e = ext;
    std::transform(e.begin(), e.end(), e.begin(), ::towlower);
    if (!e.empty() && e[0] == L'.') e.erase(0, 1);
    return L"TestViewer." + e;
}

bool setKey(HKEY root, const std::wstring& sub, const std::wstring& valueName,
            const std::wstring& value) {
    HKEY k = nullptr;
    LSTATUS s = RegCreateKeyExW(root, sub.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                KEY_WRITE, nullptr, &k, nullptr);
    if (s != ERROR_SUCCESS) return false;
    s = RegSetValueExW(k, valueName.empty() ? nullptr : valueName.c_str(), 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return s == ERROR_SUCCESS;
}

bool deleteKey(HKEY root, const std::wstring& sub) {
    return RegDeleteTreeW(root, sub.c_str()) == ERROR_SUCCESS;
}

}  // namespace

bool isFileAssociated(const std::wstring& ext) {
    std::wstring e = ext;
    std::transform(e.begin(), e.end(), e.begin(), ::towlower);
    wchar_t buf[512];
    DWORD n = ARRAYSIZE(buf);
    LSTATUS s = RegGetValueW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + e).c_str(),
                             L"", RRF_RT_REG_SZ, nullptr, buf, &n);
    if (s != ERROR_SUCCESS) return false;
    std::wstring cur(buf, n > 1 ? n / sizeof(wchar_t) - 1 : 0);
    return cur == progIdFor(e);
}

bool registerFileAssociation(const std::wstring& ext) {
    const std::wstring exe = exePath();
    if (exe.empty()) return false;
    std::wstring e = ext;
    std::transform(e.begin(), e.end(), e.begin(), ::towlower);
    const std::wstring progId = progIdFor(e);

    bool ok = true;
    ok &= setKey(HKEY_CURRENT_USER, L"Software\\Classes\\" + e, L"", progId);
    ok &= setKey(HKEY_CURRENT_USER, L"Software\\Classes\\" + progId, L"",
                 L"TestViewer Image");
    ok &= setKey(HKEY_CURRENT_USER, L"Software\\Classes\\" + progId + L"\\DefaultIcon",
                 L"", exe + L",0");
    ok &= setKey(HKEY_CURRENT_USER,
                 L"Software\\Classes\\" + progId + L"\\shell\\open\\command", L"",
                 L"\"" + exe + L"\" \"%1\"");

    // Make the new association visible immediately.
    std::wstring full = e;
    if (!full.empty() && full[0] != L'.') full = L"." + full;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, full.c_str(), nullptr);
    return ok;
}

bool unregisterFileAssociation(const std::wstring& ext) {
    std::wstring e = ext;
    std::transform(e.begin(), e.end(), e.begin(), ::towlower);
    const std::wstring progId = progIdFor(e);

    // Only remove the extension pointer if it currently points at us.
    if (isFileAssociated(e)) {
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + e).c_str(),
                          0, KEY_WRITE, &k) == ERROR_SUCCESS) {
            RegDeleteValueW(k, L"");
            RegCloseKey(k);
        }
    }
    deleteKey(HKEY_CURRENT_USER, L"Software\\Classes\\" + progId);

    std::wstring full = e;
    if (!full.empty() && full[0] != L'.') full = L"." + full;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, full.c_str(), nullptr);
    return true;
}

}  // namespace tv
