#pragma once

#include "core/Models.h"
#include "integration/HttpClient.h"
#include "integration/StudentFilter.h"
#include "integration/TokenProvider.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace integration {

class IClassroomStudentExporter {
public:
    virtual ~IClassroomStudentExporter() = default;
    virtual bool FetchStudents(const std::string& courseId,
                               const std::string& courseWorkId,
                               const std::string& filter,
                               std::vector<core::Student>& outStudents,
                               std::string& outMessage) = 0;
};

class GoogleClassroomStudentExporter final : public IClassroomStudentExporter {
public:
    GoogleClassroomStudentExporter(
        std::unique_ptr<ITokenProvider> tokenProvider = std::make_unique<FileTokenProvider>(),
        std::unique_ptr<IHttpClient> httpClient = std::make_unique<WinHttpClient>())
        : tokenProvider_(std::move(tokenProvider)),
          httpClient_(std::move(httpClient)) {}

    bool FetchStudents(const std::string& courseId,
                   const std::string& courseWorkId,
                       const std::string& filter,
                       std::vector<core::Student>& outStudents,
                       std::string& outMessage) override {
#ifndef _WIN32
        (void)courseId;
        (void)courseWorkId;
        (void)filter;
        (void)outStudents;
        outMessage = "Live Classroom API наразі реалізовано лише для Windows-збірки.";
        return false;
#else
        const auto token = tokenProvider_->LoadAccessToken();
        if (!token.has_value()) {
            outMessage = "Не знайдено access token у settings/google/access_token.txt";
            return false;
        }

        HttpResponse response;
        std::string error;
        const std::string host = "classroom.googleapis.com";
        const std::string path = "/v1/courses/" + courseId + "/students?pageSize=500";
        if (!httpClient_->Get(host, path, token.value(), response, error)) {
            outMessage = "Помилка запиту до Classroom API: " + error;
            return false;
        }

        if (response.statusCode < 200 || response.statusCode >= 300) {
            outMessage = "Classroom API HTTP статус=" + std::to_string(response.statusCode);
            return false;
        }

        nlohmann::json payload;
        try {
            payload = nlohmann::json::parse(response.body);
        } catch (...) {
            outMessage = "Не вдалося розібрати JSON відповідь Classroom API.";
            return false;
        }

        outStudents.clear();
        std::unordered_map<std::string, size_t> userIdToIndex;
        if (!payload.contains("students") || !payload["students"].is_array()) {
            outMessage = "Classroom API не повернув масив students.";
            return false;
        }

        for (const auto& item : payload["students"]) {
            core::Student student;
            const std::string userId = item.value("userId", "");
            student.fullName = item.value("profile", nlohmann::json::object())
                                   .value("name", nlohmann::json::object())
                                   .value("fullName", "");
            student.email = item.value("profile", nlohmann::json::object())
                                .value("emailAddress", "");
            student.githubUsername = "";

            if (!MatchesStudentFilter(student, "", filter)) {
                continue;
            }

            outStudents.push_back(student);
            if (!userId.empty()) {
                userIdToIndex[userId] = outStudents.size() - 1;
            }
        }

        if (!courseWorkId.empty() && !userIdToIndex.empty()) {
            HttpResponse submissionsResponse;
            const std::string submissionsPath = "/v1/courses/" + courseId + "/courseWork/" + courseWorkId + "/studentSubmissions?pageSize=500";
            std::string submissionsError;
            if (httpClient_->Get(host, submissionsPath, token.value(), submissionsResponse, submissionsError) &&
                submissionsResponse.statusCode >= 200 && submissionsResponse.statusCode < 300) {
                nlohmann::json submissionsPayload;
                try {
                    submissionsPayload = nlohmann::json::parse(submissionsResponse.body);
                } catch (...) {
                    submissionsPayload = nlohmann::json::object();
                }

                auto findGitHubUrl = [&](const nlohmann::json& node, const auto& self) -> std::string {
                    if (node.is_string()) {
                        const std::string value = node.get<std::string>();
                        const auto pos = value.find("github.com/");
                        if (pos != std::string::npos) {
                            if (pos >= 8 && value.substr(pos - 8, 8) == "https://") {
                                return value.substr(pos - 8);
                            }
                            if (pos >= 7 && value.substr(pos - 7, 7) == "http://") {
                                return value.substr(pos - 7);
                            }
                            return "https://" + value.substr(pos);
                        }
                        return {};
                    }

                    if (node.is_array()) {
                        for (const auto& item : node) {
                            const std::string found = self(item, self);
                            if (!found.empty()) {
                                return found;
                            }
                        }
                        return {};
                    }

                    if (node.is_object()) {
                        for (const auto& [_, value] : node.items()) {
                            const std::string found = self(value, self);
                            if (!found.empty()) {
                                return found;
                            }
                        }
                    }

                    return {};
                };

                if (submissionsPayload.contains("studentSubmissions") && submissionsPayload["studentSubmissions"].is_array()) {
                    for (const auto& submission : submissionsPayload["studentSubmissions"]) {
                        const std::string userId = submission.value("userId", "");
                        if (userId.empty()) {
                            continue;
                        }

                        const auto studentIt = userIdToIndex.find(userId);
                        if (studentIt == userIdToIndex.end()) {
                            continue;
                        }

                        const std::string githubUrl = findGitHubUrl(submission, findGitHubUrl);
                        if (!githubUrl.empty()) {
                            outStudents[studentIt->second].githubUsername = githubUrl;
                        }
                    }
                }
            }
        }

        outMessage = "[Classroom API] Імпортовано студентів: " + std::to_string(outStudents.size()) + " (live).";
        return true;
#endif
    }

private:
    std::unique_ptr<ITokenProvider> tokenProvider_;
    std::unique_ptr<IHttpClient> httpClient_;
};

}