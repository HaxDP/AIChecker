#pragma once

#include "core/Models.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>
#endif

namespace core {

namespace detail {

inline double ClampScore(double value) {
    return std::clamp(value, 0.0, 100.0);
}

inline std::optional<double> ExtractFirstNumber(const std::string& text) {
    static const std::regex numberRegex(R"((-?\d+(?:\.\d+)?))");
    std::smatch match;
    if (!std::regex_search(text, match, numberRegex)) {
        return std::nullopt;
    }

    try {
        return std::stod(match[1].str());
    } catch (...) {
        return std::nullopt;
    }
}

inline std::optional<double> ExtractScoreNumber(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }

    static const std::regex labeledScoreRegex(
        R"((score|likelihood|ai|risk)\s*[:=\-]?\s*(-?\d+(?:\.\d+)?))",
        std::regex::icase);
    std::smatch labeledMatch;
    if (std::regex_search(text, labeledMatch, labeledScoreRegex)) {
        try {
            return std::stod(labeledMatch[2].str());
        } catch (...) {
        }
    }

    static const std::regex percentRegex(R"((-?\d+(?:\.\d+)?)\s*%)");
    std::sregex_iterator it(text.begin(), text.end(), percentRegex);
    std::sregex_iterator end;
    if (it != end) {
        std::optional<double> lastPercent;
        for (; it != end; ++it) {
            try {
                lastPercent = std::stod((*it)[1].str());
            } catch (...) {
            }
        }
        if (lastPercent.has_value()) {
            return lastPercent;
        }
    }

    static const std::regex anyNumberRegex(R"((-?\d+(?:\.\d+)?))");
    std::sregex_iterator anyIt(text.begin(), text.end(), anyNumberRegex);
    if (anyIt == end) {
        return std::nullopt;
    }

    std::optional<double> lastNumber;
    for (; anyIt != end; ++anyIt) {
        try {
            lastNumber = std::stod((*anyIt)[1].str());
        } catch (...) {
        }
    }

    return lastNumber;
}

inline std::string Shorten(const std::string& text, std::size_t maxLength = 220) {
    if (text.size() <= maxLength) {
        return text;
    }
    return text.substr(0, maxLength) + "...";
}

struct ParsedUrl {
    bool secure = true;
    int port = 443;
    std::string host;
    std::string path;
};

inline std::optional<ParsedUrl> ParseUrl(const std::string& url) {
    static const std::regex urlRegex(R"(^(https?)://([^/:]+)(?::(\d+))?(.*)$)", std::regex::icase);
    std::smatch match;
    if (!std::regex_match(url, match, urlRegex)) {
        return std::nullopt;
    }

    std::string scheme = match[1].str();
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    ParsedUrl parsed;
    parsed.secure = scheme == "https";
    parsed.port = parsed.secure ? 443 : 80;
    parsed.host = match[2].str();
    parsed.path = match[4].str().empty() ? "/" : match[4].str();

    if (match[3].matched) {
        try {
            parsed.port = std::stoi(match[3].str());
        } catch (...) {
            return std::nullopt;
        }
    }

    return parsed;
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

inline bool PostJson(const std::string& url,
                     const nlohmann::json& request,
                     std::string& responseBody,
                     long& statusCode,
                     std::string& error) {
    const auto parsed = ParseUrl(url);
    if (!parsed.has_value()) {
        error = "Invalid URL: " + url;
        return false;
    }

    HINTERNET hSession = WinHttpOpen(L"AIChecker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession == nullptr) {
        error = "WinHttpOpen failed";
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, ToWide(parsed->host).c_str(), static_cast<INTERNET_PORT>(parsed->port), 0);
    if (hConnect == nullptr) {
        error = "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return false;
    }

    const DWORD flags = parsed->secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", ToWide(parsed->path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (hRequest == nullptr) {
        error = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::string body = request.dump();
    std::wstring contentType = L"Content-Type: application/json\r\n";
    bool ok = WinHttpSendRequest(
        hRequest,
        contentType.c_str(),
        static_cast<DWORD>(-1),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : body.data(),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
        0);
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

    DWORD nativeStatusCode = 0;
    DWORD statusSize = sizeof(nativeStatusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &nativeStatusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    statusCode = static_cast<long>(nativeStatusCode);

    responseBody.clear();
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
        responseBody += buffer;
    } while (bytesAvailable > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return error.empty();
}
#endif

} // namespace detail

class IAnalysisStrategy {
public:
    virtual ~IAnalysisStrategy() = default;
    virtual const char* Name() const = 0;
    virtual void Analyze(Submission& submission) = 0;
};

class NgramSimilarityStrategy final : public IAnalysisStrategy {
public:
    const char* Name() const override { return "N-gram Similarity"; }
    void Analyze(Submission& submission) override {
        std::hash<std::string> hasher;
        const double seed = static_cast<double>(hasher(submission.repositoryUrl) % 100);
        submission.result.plagiarismScore = 40.0 + (seed / 100.0) * 55.0;
    }
};

class StylometryAIStrategy final : public IAnalysisStrategy {
public:
    const char* Name() const override { return "Stylometry AI Classifier"; }
    void Analyze(Submission& submission) override {
        std::hash<std::string> hasher;
        const double seed = static_cast<double>(hasher(submission.student.githubUsername) % 100);
        submission.result.aiLikelihoodScore = 10.0 + (seed / 100.0) * 80.0;
        submission.result.aiThinking = "Fallback-аналіз: стилометрична евристика (локально, без Ollama-відповіді).";
        submission.result.aiIndicators = "Ознаки: повторюваний стиль за профілем автора, стабільний seed по GitHub username.";
    }
};

class OllamaAIStrategy final : public IAnalysisStrategy {
public:
    OllamaAIStrategy(
        std::string baseUrl,
        std::string model,
        std::unique_ptr<IAnalysisStrategy> fallbackStrategy = std::make_unique<StylometryAIStrategy>())
        : baseUrl_(std::move(baseUrl)),
          model_(std::move(model)),
          fallbackStrategy_(std::move(fallbackStrategy)) {}

    const char* Name() const override { return "Ollama AI Detector"; }

    void Analyze(Submission& submission) override {
        const std::string cacheKey = submission.repositoryUrl + "|" + submission.localPath;
        const bool hasConfiguration = !baseUrl_.empty() && !model_.empty();
        if (!hasConfiguration) {
            fallbackStrategy_->Analyze(submission);
            submission.result.aiThinking += " Використано fallback, бо Ollama не налаштовано.";
            return;
        }

#ifndef _WIN32
        fallbackStrategy_->Analyze(submission);
#else
        nlohmann::json payload;
        payload["model"] = model_;
        payload["stream"] = false;
        payload["options"] = {
            {"temperature", 0.0},
            {"top_p", 1.0},
            {"seed", 42},
            {"num_predict", 8}
        };
        payload["prompt"] =
            "Estimate AI-generated likelihood from 0 to 100. "
            "Return ONLY one number in this exact format: score:NN where NN is 0..100. "
            "Student: " + submission.student.fullName +
            ", Repository: " + submission.repositoryUrl +
            ", LocalPath: " + submission.localPath;

        std::string responseBody;
        std::string error;
        long statusCode = 0;
        const std::string endpoint = baseUrl_ + "/api/generate";
        if (!detail::PostJson(endpoint, payload, responseBody, statusCode, error) || statusCode < 200 || statusCode >= 300) {
            const auto cacheIt = cachedScores_.find(cacheKey);
            if (cacheIt != cachedScores_.end()) {
                submission.result.aiLikelihoodScore = cacheIt->second;
                submission.result.aiThinking = "Ollama тимчасово недоступний, використано кеш попереднього AI-результату.";
                submission.result.aiIndicators = "Ознаки: мережевий збій/HTTP помилка, повторне використання стабільного кешу.";
                return;
            }
            fallbackStrategy_->Analyze(submission);
            submission.result.aiThinking += " Ollama недоступний, оцінку отримано з fallback-стратегії.";
            return;
        }

        try {
            const auto responseJson = nlohmann::json::parse(responseBody);
            std::string responseText = responseJson.value("response", "");
            if (responseText.empty() && responseJson.contains("message") && responseJson["message"].is_object()) {
                responseText = responseJson["message"].value("content", "");
            }

            const auto maybeScore = detail::ExtractScoreNumber(responseText);
            if (!maybeScore.has_value()) {
                const auto cacheIt = cachedScores_.find(cacheKey);
                if (cacheIt != cachedScores_.end()) {
                    submission.result.aiLikelihoodScore = cacheIt->second;
                    submission.result.aiThinking = "Не вдалося витягти score з Ollama-відповіді, використано кеш попереднього результату.";
                    submission.result.aiIndicators = "Ознаки: відсутнє валідне поле score у відповіді, fallback до кешу.";
                    return;
                }
                fallbackStrategy_->Analyze(submission);
                submission.result.aiThinking += " Не вдалося прочитати score з Ollama, тому використано fallback.";
                return;
            }

            submission.result.aiLikelihoodScore = detail::ClampScore(maybeScore.value());
            cachedScores_[cacheKey] = submission.result.aiLikelihoodScore;
            submission.result.aiThinking = "AI-оцінку отримано з Ollama (детермінований запит).";
            submission.result.aiIndicators = "Ознаки з відповіді моделі: " + detail::Shorten(responseText);
        } catch (...) {
            const auto cacheIt = cachedScores_.find(cacheKey);
            if (cacheIt != cachedScores_.end()) {
                submission.result.aiLikelihoodScore = cacheIt->second;
                submission.result.aiThinking = "Помилка обробки відповіді Ollama, використано кеш попереднього результату.";
                submission.result.aiIndicators = "Ознаки: JSON/парсинг помилка, fallback до кешу.";
                return;
            }
            fallbackStrategy_->Analyze(submission);
            submission.result.aiThinking += " Сталась помилка парсингу Ollama, використано fallback.";
        }
#endif

        if (submission.result.aiLikelihoodScore >= 70.0) {
            submission.result.aiConclusion = "Ймовірно згенеровано AI";
        } else if (submission.result.aiLikelihoodScore >= 40.0) {
            submission.result.aiConclusion = "Потрібна ручна перевірка";
        } else {
            submission.result.aiConclusion = "Ознак AI мало";
        }
    }

private:
    std::string baseUrl_;
    std::string model_;
    std::unique_ptr<IAnalysisStrategy> fallbackStrategy_;
    std::unordered_map<std::string, double> cachedScores_;
};

class PlagiarismServiceStrategy final : public IAnalysisStrategy {
public:
    PlagiarismServiceStrategy(
        std::string serviceUrl,
        std::unique_ptr<IAnalysisStrategy> fallbackStrategy = std::make_unique<NgramSimilarityStrategy>())
        : serviceUrl_(std::move(serviceUrl)),
          fallbackStrategy_(std::move(fallbackStrategy)) {}

    const char* Name() const override { return "Plagiarism Service"; }

    void Analyze(Submission& submission) override {
        if (serviceUrl_.empty()) {
            fallbackStrategy_->Analyze(submission);
            return;
        }

#ifndef _WIN32
        fallbackStrategy_->Analyze(submission);
#else
        nlohmann::json payload;
        payload["repositoryUrl"] = submission.repositoryUrl;
        payload["studentName"] = submission.student.fullName;
        payload["studentEmail"] = submission.student.email;

        std::string responseBody;
        std::string error;
        long statusCode = 0;
        if (!detail::PostJson(serviceUrl_, payload, responseBody, statusCode, error) || statusCode < 200 || statusCode >= 300) {
            fallbackStrategy_->Analyze(submission);
            return;
        }

        auto readScore = [](const nlohmann::json& object, const std::vector<std::string>& keys) -> std::optional<double> {
            for (const auto& key : keys) {
                if (!object.contains(key)) {
                    continue;
                }

                const auto& value = object[key];
                if (value.is_number()) {
                    return value.get<double>();
                }
                if (value.is_string()) {
                    const auto parsed = detail::ExtractFirstNumber(value.get<std::string>());
                    if (parsed.has_value()) {
                        return parsed;
                    }
                }
            }
            return std::nullopt;
        };

        try {
            const auto responseJson = nlohmann::json::parse(responseBody);
            std::optional<double> score = readScore(responseJson, {"plagiarismScore", "score", "plagiarism", "risk"});
            if (!score.has_value() && responseJson.contains("data") && responseJson["data"].is_object()) {
                score = readScore(responseJson["data"], {"plagiarismScore", "score", "plagiarism", "risk"});
            }

            if (!score.has_value()) {
                fallbackStrategy_->Analyze(submission);
                return;
            }

            submission.result.plagiarismScore = detail::ClampScore(score.value());
        } catch (...) {
            fallbackStrategy_->Analyze(submission);
        }
#endif
    }

private:
    std::string serviceUrl_;
    std::unique_ptr<IAnalysisStrategy> fallbackStrategy_;
};

class PromptLeakHeuristicStrategy final : public IAnalysisStrategy {
public:
    const char* Name() const override { return "Prompt Leak Heuristics"; }
    void Analyze(Submission& submission) override {
        if (submission.localPath.find("generated") != std::string::npos) {
            submission.result.aiLikelihoodScore = std::min(100.0, submission.result.aiLikelihoodScore + 12.0);
            if (!submission.result.aiIndicators.empty()) {
                submission.result.aiIndicators += " ";
            }
            submission.result.aiIndicators += "Локальна евристика: шлях містить 'generated' (додано +12%).";
        }
    }
};

class StrategyFactory {
public:
    static std::vector<std::unique_ptr<IAnalysisStrategy>> BuildDefault(
        const std::string& ollamaBaseUrl,
        const std::string& ollamaModel,
        const std::string& plagiarismServiceUrl) {
        std::vector<std::unique_ptr<IAnalysisStrategy>> strategies;
        strategies.emplace_back(std::make_unique<PlagiarismServiceStrategy>(
            plagiarismServiceUrl,
            std::make_unique<NgramSimilarityStrategy>()));
        strategies.emplace_back(std::make_unique<OllamaAIStrategy>(
            ollamaBaseUrl,
            ollamaModel,
            std::make_unique<StylometryAIStrategy>()));
        strategies.emplace_back(std::make_unique<PromptLeakHeuristicStrategy>());
        return strategies;
    }

    static std::vector<std::unique_ptr<IAnalysisStrategy>> BuildDefault() {
        return BuildDefault("", "", "");
    }
};

} // namespace core
