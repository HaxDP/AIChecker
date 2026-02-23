#pragma once

#include "core/Models.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>
#endif

namespace integration {

namespace detail {

inline std::string Trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

inline std::string ToLowerAscii(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}

inline bool MatchesFilter(const core::Student& student, const std::string& group, const std::string& filter) {
    const std::string trimmed = Trim(filter);
    const std::string filterLower = ToLowerAscii(trimmed);
    const bool noFilter = trimmed.empty() || filterLower == "all";
    if (noFilter) {
        return true;
    }

    const bool groupMatch = group == trimmed;
    const bool exactNameMatch = student.fullName == trimmed;
    const bool emailMatch = ToLowerAscii(student.email) == filterLower;
    const bool githubMatch = ToLowerAscii(student.githubUsername) == filterLower;
    return groupMatch || exactNameMatch || emailMatch || githubMatch;
}

inline std::optional<std::string> LoadAccessToken() {
    const std::filesystem::path tokenPath = std::filesystem::path("settings") / "google" / "access_token.txt";
    std::ifstream tokenFile(tokenPath);
    if (!tokenFile.is_open()) {
        return std::nullopt;
    }

    std::string token;
    std::getline(tokenFile, token);
    token = Trim(token);
    if (token.empty()) {
        return std::nullopt;
    }

    return token;
}

#ifdef _WIN32
inline std::wstring ToWide(const std::string& text) {
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

inline bool HttpGet(const std::wstring& host,
                    const std::wstring& path,
                    const std::string& bearerToken,
                    std::string& body,
                    DWORD& statusCode,
                    std::string& error) {
    HINTERNET hSession = WinHttpOpen(L"AIChecker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession == nullptr) {
        error = "WinHttpOpen failed";
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hConnect == nullptr) {
        error = "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
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

    DWORD size = sizeof(statusCode);
    statusCode = 0;
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);

    body.clear();
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
        body += buffer;
    } while (bytesAvailable > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return error.empty();
}
#endif

inline bool TryFetchStudentsFromGoogleClassroom(const std::string& courseId,
                                                const std::string& filter,
                                                std::vector<core::Student>& outStudents,
                                                std::string& outMessage) {
#ifndef _WIN32
    (void)courseId;
    (void)filter;
    (void)outStudents;
    outMessage = "Live Classroom API is currently implemented for Windows build only.";
    return false;
#else
    const auto token = LoadAccessToken();
    if (!token.has_value()) {
        outMessage = "No access token found at settings/google/access_token.txt";
        return false;
    }

    const std::wstring host = L"classroom.googleapis.com";
    const std::wstring path = ToWide("/v1/courses/" + courseId + "/students?pageSize=500");

    std::string response;
    DWORD statusCode = 0;
    std::string error;
    if (!HttpGet(host, path, token.value(), response, statusCode, error)) {
        outMessage = "Classroom API request failed: " + error;
        return false;
    }

    if (statusCode < 200 || statusCode >= 300) {
        outMessage = "Classroom API HTTP status=" + std::to_string(statusCode);
        return false;
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(response);
    } catch (...) {
        outMessage = "Failed to parse Classroom API JSON.";
        return false;
    }

    outStudents.clear();
    if (!payload.contains("students") || !payload["students"].is_array()) {
        outMessage = "Classroom API returned no students array.";
        return false;
    }

    for (const auto& item : payload["students"]) {
        core::Student student;
        student.fullName = item.value("profile", nlohmann::json::object())
                                .value("name", nlohmann::json::object())
                                .value("fullName", "");
        student.email = item.value("profile", nlohmann::json::object())
                            .value("emailAddress", "");
        student.githubUsername = "";

        if (!MatchesFilter(student, "", filter)) {
            continue;
        }

        outStudents.push_back(student);
    }

    outMessage = "[Classroom API] exported " + std::to_string(outStudents.size()) + " students (live).";
    return true;
#endif
}

} // namespace detail

class IClassroomGateway {
public:
    virtual ~IClassroomGateway() = default;
    virtual bool FetchStudents(const std::string& courseId,
                               const std::string& courseWorkId,
                               const std::string& studentGroup,
                               std::vector<core::Student>& outStudents,
                               std::string& outMessage) = 0;
    virtual bool PushGrade(const core::Submission& submission, const core::Assignment& assignment, std::string& outMessage) = 0;
};

class IEmailGateway {
public:
    virtual ~IEmailGateway() = default;
    virtual bool SendFeedback(const core::Submission& submission, std::string& outMessage) = 0;
};

class ClassroomGatewayStub final : public IClassroomGateway {
public:
    bool FetchStudents(const std::string& courseId,
                       const std::string& courseWorkId,
                       const std::string& studentGroup,
                       std::vector<core::Student>& outStudents,
                       std::string& outMessage) override {
        (void)courseWorkId;
        (void)studentGroup;

        outStudents.clear();
        const bool ok = detail::TryFetchStudentsFromGoogleClassroom(courseId, studentGroup, outStudents, outMessage);
        if (!ok && outMessage.empty()) {
            outMessage = "[Classroom API] student export failed.";
        }
        return ok;
    }

    bool PushGrade(const core::Submission& submission, const core::Assignment& assignment, std::string& outMessage) override {
        const int maxGrade = static_cast<int>(assignment.maxPoints);
        outMessage = "[Stub Classroom] grade=" + std::to_string(submission.result.finalGrade)
            + " / max=" + std::to_string(maxGrade)
            + " for " + submission.student.email;
        return true;
    }
};

class EmailGatewayStub final : public IEmailGateway {
public:
    bool SendFeedback(const core::Submission& submission, std::string& outMessage) override {
        outMessage = "[Stub Email] sent feedback to " + submission.student.email;
        return true;
    }
};

} // namespace integration
