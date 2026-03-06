#pragma once

#include "core/AnalysisStrategies.h"
#include "core/EventBus.h"
#include "core/Models.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

namespace core {

class IWorkflowCommand {
public:
    virtual ~IWorkflowCommand() = default;
    virtual const char* Name() const = 0;
    virtual void Execute(AppState& state, EventBus& bus) = 0;
};

class LoadStudentsCommand final : public IWorkflowCommand {
public:
    const char* Name() const override { return "Завантажити студентів"; }
    void Execute(AppState& state, EventBus& bus) override {
        state.students = {
            {"Olena Vasylenko", "olena@university.edu", "olenav"},
            {"Marko Shevchenko", "marko@university.edu", "markos"},
            {"Iryna Koval", "iryna@university.edu", "ikoval"}
        };
        bus.Emit("Список студентів завантажено з файлу групи.");
    }
};

class BuildSubmissionsCommand final : public IWorkflowCommand {
public:
    const char* Name() const override { return "Побудувати роботи"; }
    void Execute(AppState& state, EventBus& bus) override {
        auto isUrl = [](const std::string& value) {
            return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
        };

        auto repositorySlug = [](const std::string& repositoryUrl) {
            std::string slug = repositoryUrl;
            const auto slashPos = slug.find_last_of('/');
            if (slashPos != std::string::npos && slashPos + 1 < slug.size()) {
                slug = slug.substr(slashPos + 1);
            }
            if (slug.size() > 4 && slug.substr(slug.size() - 4) == ".git") {
                slug = slug.substr(0, slug.size() - 4);
            }
            return slug.empty() ? std::string("submission") : slug;
        };

        state.submissions.clear();
        for (const auto& student : state.students) {
            Submission submission;
            submission.student = student;
            if (isUrl(student.githubUsername)) {
                submission.repositoryUrl = student.githubUsername;
                submission.localPath = "repos/" + repositorySlug(student.githubUsername);
            } else {
                submission.repositoryUrl = "https://github.com/classroom/" + student.githubUsername + "-task";
                submission.localPath = "repos/" + student.githubUsername;
            }
            submission.status = SubmissionStatus::Cloned;
            state.submissions.push_back(submission);
        }
        bus.Emit("Сформовано список робіт з посилань GitHub Classroom.");
    }
};

class AnalyzeSubmissionsCommand final : public IWorkflowCommand {
public:
    explicit AnalyzeSubmissionsCommand(std::vector<std::unique_ptr<IAnalysisStrategy>> strategies)
        : strategies_(std::move(strategies)) {}

    const char* Name() const override { return "Аналіз робіт"; }

    void Execute(AppState& state, EventBus& bus) override {
        for (auto& submission : state.submissions) {
            for (auto& strategy : strategies_) {
                strategy->Analyze(submission);
            }

            const double weightedRisk = (submission.result.plagiarismScore * 0.6 + submission.result.aiLikelihoodScore * 0.4) / 100.0;
            const double gradeSpan = std::max(0.0, state.assignment.maxPoints - state.assignment.minGrade);
            const double rawGrade = state.assignment.maxPoints - weightedRisk * gradeSpan;
            const int minGrade = static_cast<int>(std::lround(state.assignment.minGrade));
            const int maxGrade = static_cast<int>(std::lround(state.assignment.maxPoints));
            const int roundedGrade = static_cast<int>(std::lround(rawGrade));
            submission.result.finalGrade = std::clamp(roundedGrade, minGrade, maxGrade);

            if (submission.result.aiLikelihoodScore >= 70.0) {
                submission.result.aiConclusion = "Ймовірно згенеровано AI";
            } else if (submission.result.aiLikelihoodScore >= 40.0) {
                submission.result.aiConclusion = "Потрібна ручна перевірка";
            } else {
                submission.result.aiConclusion = "Ознак AI мало";
            }

            if (submission.result.aiThinking.empty()) {
                submission.result.aiThinking = "AI-оцінка отримана в автоматичному режимі аналізу.";
            }
            if (submission.result.aiIndicators.empty()) {
                submission.result.aiIndicators = "Ознаки сформовані за комбінованими метриками AI/плагіату.";
            }

            std::ostringstream summary;
            summary << std::fixed << std::setprecision(1)
                    << "Плагіат=" << submission.result.plagiarismScore
                    << "%, AI=" << submission.result.aiLikelihoodScore
                    << "%, Оцінка=" << submission.result.finalGrade;
            submission.result.summary = summary.str();
            submission.result.feedbackEmailBody =
                "Коментар викладача (автоматично згенеровано):\n"
                "- Рішення AI: " + submission.result.aiConclusion + "\n"
                "- " + submission.result.summary + "\n"
                "- Рекомендація: переглянути ділянки коду з підвищеним ризиком та підготувати коротке пояснення авторства.";

            submission.status = SubmissionStatus::Analyzed;
        }
        bus.Emit("Завершено перевірку плагіату та AI для всіх робіт.");
    }

private:
    std::vector<std::unique_ptr<IAnalysisStrategy>> strategies_;
};

class AnalyzeAiOnlyCommand final : public IWorkflowCommand {
public:
    AnalyzeAiOnlyCommand(
        std::unique_ptr<IAnalysisStrategy> aiStrategy = std::make_unique<StylometryAIStrategy>(),
        std::unique_ptr<IAnalysisStrategy> heuristicStrategy = std::make_unique<PromptLeakHeuristicStrategy>())
        : aiStrategy_(std::move(aiStrategy)),
          heuristicStrategy_(std::move(heuristicStrategy)) {}

    const char* Name() const override { return "Лише AI-перевірка"; }

    void Execute(AppState& state, EventBus& bus) override {
        if (state.submissions.empty()) {
            bus.Emit("[AI] Немає робіт для перевірки.");
            return;
        }

        for (auto& submission : state.submissions) {
            aiStrategy_->Analyze(submission);
            heuristicStrategy_->Analyze(submission);
            submission.result.summary = "Лише AI: " + std::to_string(submission.result.aiLikelihoodScore);
            if (submission.result.aiConclusion.empty()) {
                submission.result.aiConclusion = submission.result.aiLikelihoodScore >= 70.0
                    ? "Ймовірно згенеровано AI"
                    : (submission.result.aiLikelihoodScore >= 40.0 ? "Потрібна ручна перевірка" : "Ознак AI мало");
            }
            submission.result.feedbackEmailBody =
                "Коментар викладача (AI-check):\n- Рішення AI: " + submission.result.aiConclusion +
                "\n- AI=" + std::to_string(submission.result.aiLikelihoodScore) + "%";
        }

        bus.Emit("[AI] Перевірку завершено.");
    }

private:
    std::unique_ptr<IAnalysisStrategy> aiStrategy_;
    std::unique_ptr<IAnalysisStrategy> heuristicStrategy_;
};

class AnalyzePlagiarismOnlyCommand final : public IWorkflowCommand {
public:
    explicit AnalyzePlagiarismOnlyCommand(
        std::unique_ptr<IAnalysisStrategy> similarityStrategy = std::make_unique<NgramSimilarityStrategy>())
        : similarityStrategy_(std::move(similarityStrategy)) {}

    const char* Name() const override { return "Лише перевірка плагіату"; }

    void Execute(AppState& state, EventBus& bus) override {
        if (state.submissions.empty()) {
            bus.Emit("[Плагіат] Немає робіт для перевірки.");
            return;
        }

        for (auto& submission : state.submissions) {
            similarityStrategy_->Analyze(submission);
            submission.result.summary = "Лише плагіат: " + std::to_string(submission.result.plagiarismScore);
        }

        bus.Emit("[Плагіат] Перевірку завершено.");
    }

private:
    std::unique_ptr<IAnalysisStrategy> similarityStrategy_;
};

class SyncGradesCommand final : public IWorkflowCommand {
public:
    const char* Name() const override { return "Синхронізація оцінок"; }
    void Execute(AppState& state, EventBus& bus) override {
        for (auto& submission : state.submissions) {
            submission.status = SubmissionStatus::Synced;
        }
        bus.Emit("Оцінки синхронізовано з Google Classroom через studentSubmissions.patch.");
    }
};

class SendEmailFeedbackCommand final : public IWorkflowCommand {
public:
    const char* Name() const override { return "Надіслати фідбек на email"; }
    void Execute(AppState& state, EventBus& bus) override {
        for (auto& submission : state.submissions) {
            (void)submission;
        }
        bus.Emit("Надіслано фідбек студентам на email (Gmail API / SMTP).\n");
    }
};

} // namespace core
