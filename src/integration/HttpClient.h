#pragma once

#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>
#endif

namespace integration {

struct HttpResponse {
    long statusCode = 0;
    std::string body;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual bool Get(const std::string& host,
                     const std::string& path,
                     const std::string& bearerToken,
                     HttpResponse& response,
                     std::string& error) const = 0;
};

class WinHttpClient final : public IHttpClient {
public:
    bool Get(const std::string& host,
             const std::string& path,
             const std::string& bearerToken,
             HttpResponse& response,
             std::string& error) const override {
#ifndef _WIN32
        (void)host;
        (void)path;
        (void)bearerToken;
        (void)response;
        error = "WinHTTP is available only on Windows builds.";
        return false;
#else
        HINTERNET hSession = WinHttpOpen(L"AIChecker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession == nullptr) {
            error = "WinHttpOpen failed";
            return false;
        }

        HINTERNET hConnect = WinHttpConnect(hSession, ToWide(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (hConnect == nullptr) {
            error = "WinHttpConnect failed";
            WinHttpCloseHandle(hSession);
            return false;
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", ToWide(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (hRequest == nullptr) {
            error = "WinHttpOpenRequest failed";
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        std::wstring authHeader = L"Authorization: Bearer " + ToWide(bearerToken) + L"\r\n";
        if (!WinHttpAddRequestHeaders(hRequest, authHeader.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD)) {
            error = "WinHttpAddRequestHeaders failed";
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        bool ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (ok) {
            ok = WinHttpReceiveResponse(hRequest, nullptr);
        }
        if (!ok) {
            error = "WinHTTP send/receive failed";
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        response.statusCode = static_cast<long>(statusCode);

        response.body.clear();
        DWORD bytesAvailable = 0;
        do {
            if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable)) {
                error = "WinHttpQueryDataAvailable failed";
                break;
            }

            if (bytesAvailable == 0) {
                break;
            }

            std::string buffer(bytesAvailable, '\0');
            DWORD bytesRead = 0;
            if (!WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
                error = "WinHttpReadData failed";
                break;
            }

            buffer.resize(bytesRead);
            response.body += buffer;
        } while (bytesAvailable > 0);

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return error.empty();
#endif
    }

private:
#ifdef _WIN32
    static std::wstring ToWide(const std::string& text) {
        if (text.empty()) {
            return L"";
        }

        const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (size <= 0) {
            return L"";
        }

        std::wstring result(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
        if (!result.empty() && result.back() == L'\0') {
            result.pop_back();
        }
        return result;
    }
#endif
};

}