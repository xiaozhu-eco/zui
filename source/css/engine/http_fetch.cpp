#include "css/engine/http_fetch.h"

#include <mutex>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

#include "log/log.h"

namespace zui {
namespace css {
namespace {

std::mutex g_mu;
std::unordered_map<std::string, std::string> g_cache;

bool ascii_ieq_prefix(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        const char a = s[i];
        const char b = prefix[i];
        const char al = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
        const char bl = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
        if (al != bl) {
            return false;
        }
    }
    return true;
}

#if defined(_WIN32)
std::wstring utf8_to_wide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int n =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

bool winhttp_get(std::string_view url, std::string& out) {
    const std::wstring wide = utf8_to_wide(url);
    if (wide.empty()) {
        return false;
    }
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) {
        return false;
    }
    const bool https = parts.nScheme == INTERNET_SCHEME_HTTPS;
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0 && parts.lpszExtraInfo != nullptr) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    if (path.empty()) {
        path = L"/";
    }
    const INTERNET_PORT port =
        parts.nPort != 0 ? parts.nPort
                         : (https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);

    HINTERNET session = WinHttpOpen(L"zui-css/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        return false;
    }
    // Keep fixture/local servers snappy; fail soft on a hung host.
    WinHttpSetTimeouts(session, 2000, 2000, 5000, 5000);

    HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
    if (connect == nullptr) {
        WinHttpCloseHandle(session);
        return false;
    }
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (request == nullptr) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    bool ok = false;
    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                           0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD status_size = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                                WINHTTP_NO_HEADER_INDEX) &&
            status >= 200 && status < 300) {
            std::string body;
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(request, &avail)) {
                    body.clear();
                    break;
                }
                if (avail == 0) {
                    ok = true;
                    break;
                }
                const size_t at = body.size();
                body.resize(at + avail);
                DWORD read = 0;
                if (!WinHttpReadData(request, body.data() + at, avail, &read)) {
                    body.clear();
                    break;
                }
                body.resize(at + read);
            }
            if (ok) {
                out = std::move(body);
            }
        }
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}
#endif

}  // namespace

void cache_url_for_test(std::string_view url, std::string_view body) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_cache[std::string(url)] = std::string(body);
}

bool fetch_url(std::string_view url, std::string& out) {
    out.clear();
    if (!ascii_ieq_prefix(url, "http://") && !ascii_ieq_prefix(url, "https://")) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        const auto it = g_cache.find(std::string(url));
        if (it != g_cache.end()) {
            out = it->second;
            return true;
        }
    }
#if defined(_WIN32)
    std::string body;
    if (!winhttp_get(url, body)) {
        ZUI_ERROR("zui::css: remote stylesheet fetch failed: {}", url);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_cache[std::string(url)] = body;
    }
    out = std::move(body);
    return true;
#else
    (void)url;
    ZUI_ERROR("zui::css: remote stylesheet fetch unsupported on this platform");
    return false;
#endif
}

}  // namespace css
}  // namespace zui
