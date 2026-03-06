#pragma once

#include "integration/TextUtils.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace integration {

class ITokenProvider {
public:
    virtual ~ITokenProvider() = default;
    virtual std::optional<std::string> LoadAccessToken() const = 0;
};

class FileTokenProvider final : public ITokenProvider {
public:
    explicit FileTokenProvider(std::filesystem::path tokenPath = std::filesystem::path("settings") / "google" / "access_token.txt")
        : tokenPath_(std::move(tokenPath)) {}

    std::optional<std::string> LoadAccessToken() const override {
        std::ifstream tokenFile(tokenPath_);
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

private:
    std::filesystem::path tokenPath_;
};

} // namespace integration
