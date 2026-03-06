#pragma once

#include "app/AppController.h"

#include <memory>
#include <utility>

namespace app {

template <typename T, typename Factory>
inline void EnsureDependency(std::unique_ptr<T>& dependency, Factory&& factory) {
    if (!dependency) {
        dependency = std::forward<Factory>(factory)();
    }
}

inline AppControllerDependencies EnsureDefaults(AppControllerDependencies dependencies) {
    EnsureDependency(dependencies.loadStudentsCommand, [] { return std::make_unique<core::LoadStudentsCommand>(); });
    EnsureDependency(dependencies.buildSubmissionsCommand, [] { return std::make_unique<core::BuildSubmissionsCommand>(); });
    EnsureDependency(dependencies.analyzeCommand, [] {
        return std::make_unique<core::AnalyzeSubmissionsCommand>(core::StrategyFactory::BuildDefault());
    });
    EnsureDependency(dependencies.aiCheckCommand, [] { return std::make_unique<core::AnalyzeAiOnlyCommand>(); });
    EnsureDependency(dependencies.plagiarismCheckCommand, [] { return std::make_unique<core::AnalyzePlagiarismOnlyCommand>(); });
    EnsureDependency(dependencies.syncCommand, [] { return std::make_unique<core::SyncGradesCommand>(); });
    EnsureDependency(dependencies.emailCommand, [] { return std::make_unique<core::SendEmailFeedbackCommand>(); });
    EnsureDependency(dependencies.statusLabeler, [] { return std::make_unique<core::DefaultSubmissionStatusLabeler>(); });
    EnsureDependency(dependencies.classroomGateway, [] { return std::make_unique<integration::ClassroomGatewayStub>(); });
    EnsureDependency(dependencies.emailGateway, [] { return std::make_unique<integration::EmailGatewayStub>(); });
    return dependencies;
}

inline AppControllerDependencies BuildDefaultDependencies() {
    return EnsureDefaults(AppControllerDependencies{});
}

} // namespace app
