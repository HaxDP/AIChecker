#pragma once

#include <cstring>
#include <filesystem>
#include <string>

namespace ui {

struct IntegrationStatus {
    std::string classroom;
    std::string ollama;
    std::string plagiarism;
    std::string github;
};

inline IntegrationStatus ProbeIntegrationStatus(
    const char* classroomTokenPath,
    const char* githubTokenPath,
    const char* ollamaUrl,
    const char* ollamaModel,
    const char* plagiarismServiceUrl) {
    const bool hasClassroomToken = std::filesystem::exists(classroomTokenPath);
    const bool hasGithubToken = std::filesystem::exists(githubTokenPath);
    const bool ollamaConfigured = std::strlen(ollamaUrl) > 0 && std::strlen(ollamaModel) > 0;
    const bool plagiarismConfigured = std::strlen(plagiarismServiceUrl) > 0;

    IntegrationStatus status;
    status.classroom = hasClassroomToken ? "Готово" : "Немає токена";
    status.github = hasGithubToken ? "Готово" : "Немає токена";
    status.ollama = ollamaConfigured ? "Налаштовано" : "Не налаштовано";
    status.plagiarism = plagiarismConfigured ? "Налаштовано" : "Не налаштовано";
    return status;
}

} // namespace ui
