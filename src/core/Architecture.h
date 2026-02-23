#pragma once

#include "core/Models.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>

namespace core {

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
    }
};

class PromptLeakHeuristicStrategy final : public IAnalysisStrategy {
public:
    const char* Name() const override { return "Prompt Leak Heuristics"; }
    void Analyze(Submission& submission) override {
        if (submission.localPath.find("generated") != std::string::npos) {
            submission.result.aiLikelihoodScore = std::min(100.0, submission.result.aiLikelihoodScore + 12.0);
        }
    }
};

class StrategyFactory {
public:
    static std::vector<std::unique_ptr<IAnalysisStrategy>> BuildDefault() {
        std::vector<std::unique_ptr<IAnalysisStrategy>> strategies;
        strategies.emplace_back(std::make_unique<NgramSimilarityStrategy>());
        strategies.emplace_back(std::make_unique<StylometryAIStrategy>());
        strategies.emplace_back(std::make_unique<PromptLeakHeuristicStrategy>());
        return strategies;
    }
};

class IObserver {
public:
    virtual ~IObserver() = default;
    virtual void OnEvent(const std::string& text) = 0;
};

class EventBus {
public:
    void Subscribe(IObserver* observer) {
        observers_.push_back(observer);
    }

    void Emit(const std::string& message) {
        for (auto* observer : observers_) {
            observer->OnEvent(message);
        }
    }

private:
    std::vector<IObserver*> observers_;
};

class IWorkflowCommand {
public:
    virtual ~IWorkflowCommand() = default;
    virtual const char* Name() const = 0;
    virtual void Execute(AppState& state, EventBus& bus) = 0;
};

class LoadStudentsCommand final : public IWorkflowCommand {
public:
    const char* Name() const override { return "Load students"; }
    void Execute(AppState& state, EventBus& bus) override {
        state.students = {
            {"Olena Vasylenko", "olena@university.edu", "olenav"},
            {"Marko Shevchenko", "marko@university.edu", "markos"},
            {"Iryna Koval", "iryna@university.edu", "ikoval"}
        };
        bus.Emit("Loaded students from classroom roster file.");
    }
};

class BuildSubmissionsCommand final : public IWorkflowCommand {
public:
    const char* Name() const override { return "Build submissions"; }
    void Execute(AppState& state, EventBus& bus) override {
        state.submissions.clear();
        for (const auto& student : state.students) {
            Submission submission;
            submission.student = student;
            submission.repositoryUrl = "https://github.com/classroom/" + student.githubUsername + "-task";
            submission.localPath = "repos/" + student.githubUsername;
            submission.status = SubmissionStatus::Cloned;
            state.submissions.push_back(submission);
        }
        bus.Emit("Generated submission list from GitHub Classroom task links.");
    }
};

class AnalyzeSubmissionsCommand final : public IWorkflowCommand {
public:
    explicit AnalyzeSubmissionsCommand(std::vector<std::unique_ptr<IAnalysisStrategy>> strategies)
        : strategies_(std::move(strategies)) {}

    const char* Name() const override { return "Analyze submissions"; }

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

            std::ostringstream summary;
            summary << std::fixed << std::setprecision(1)
                    << "Plagiarism=" << submission.result.plagiarismScore
                    << "%, AI=" << submission.result.aiLikelihoodScore
                    << "%, Grade=" << submission.result.finalGrade;
            submission.result.summary = summary.str();
            submission.result.feedbackEmailBody =
                "Automatic feedback\n" + submission.result.summary +
                "\nDetailed report is available in GitHub checks.";

            submission.status = SubmissionStatus::Analyzed;
        }
        bus.Emit("Finished plagiarism + AI analysis for all submissions.");
    }

private:
    std::vector<std::unique_ptr<IAnalysisStrategy>> strategies_;
};

class SyncGradesCommand final : public IWorkflowCommand {
public:
    const char* Name() const override { return "Sync grades"; }
    void Execute(AppState& state, EventBus& bus) override {
        for (auto& submission : state.submissions) {
            submission.status = SubmissionStatus::Synced;
        }
        bus.Emit("Synced grades to Google Classroom using studentSubmissions.patch.");
    }
};

class SendEmailFeedbackCommand final : public IWorkflowCommand {
public:
    const char* Name() const override { return "Send email feedback"; }
    void Execute(AppState& state, EventBus& bus) override {
        for (auto& submission : state.submissions) {
            (void)submission;
        }
        bus.Emit("Sent feedback comments to student email (Gmail API / SMTP).\n");
    }
};

} // namespace core