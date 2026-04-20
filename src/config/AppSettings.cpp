#include "config/AppSettings.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>

namespace {

std::string Trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

using Reader = std::function<void(config::AppSettings&, const std::string&)>;
using Writer = std::function<std::string(const config::AppSettings&)>;

const std::unordered_map<std::string, Reader>& BuildReaders() {
    static const std::unordered_map<std::string, Reader> readers = {
        {"ollama.baseUrl", [](config::AppSettings& settings, const std::string& value) {
            settings.ollamaBaseUrl = value;
        }},
        {"ollama.model", [](config::AppSettings& settings, const std::string& value) {
            settings.ollamaModel = value;
        }},
        {"plagiarism.serviceUrl", [](config::AppSettings& settings, const std::string& value) {
            settings.plagiarismServiceUrl = value;
        }},
        {"github.tokenPath", [](config::AppSettings& settings, const std::string& value) {
            settings.githubTokenPath = value;
        }},
        {"classroom.tokenPath", [](config::AppSettings& settings, const std::string& value) {
            settings.classroomTokenPath = value;
        }},
        {"export.directory", [](config::AppSettings& settings, const std::string& value) {
            settings.exportDirectory = value;
        }}
    };
    return readers;
}

const std::vector<std::pair<std::string, Writer>>& BuildWriters() {
    static const std::vector<std::pair<std::string, Writer>> writers = {
        {"ollama.baseUrl", [](const config::AppSettings& settings) {
            return settings.ollamaBaseUrl;
        }},
        {"ollama.model", [](const config::AppSettings& settings) {
            return settings.ollamaModel;
        }},
        {"plagiarism.serviceUrl", [](const config::AppSettings& settings) {
            return settings.plagiarismServiceUrl;
        }},
        {"github.tokenPath", [](const config::AppSettings& settings) {
            return settings.githubTokenPath;
        }},
        {"classroom.tokenPath", [](const config::AppSettings& settings) {
            return settings.classroomTokenPath;
        }},
        {"export.directory", [](const config::AppSettings& settings) {
            return settings.exportDirectory;
        }}
    };
    return writers;
}

}

namespace config {

bool LoadAppSettings(const std::string& filePath, AppSettings& settings) {
    std::ifstream input(filePath);
    if (!input.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        const auto equalsPos = trimmed.find('=');
        if (equalsPos == std::string::npos) {
            continue;
        }

        const std::string key = Trim(trimmed.substr(0, equalsPos));
        const std::string value = Trim(trimmed.substr(equalsPos + 1));

        const auto& readers = BuildReaders();
        const auto readerIt = readers.find(key);
        if (readerIt != readers.end()) {
            readerIt->second(settings, value);
        }
    }

    return true;
}

bool SaveAppSettings(const std::string& filePath, const AppSettings& settings) {
    std::filesystem::path path(filePath);
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        (void)ec;
    }

    std::ofstream output(filePath, std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output << "# AIChecker settings\n";
    const auto& writers = BuildWriters();
    for (const auto& writer : writers) {
        output << writer.first << '=' << writer.second(settings) << "\n";
    }

    return true;
}

}