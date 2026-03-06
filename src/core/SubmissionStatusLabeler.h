#pragma once

#include "core/Models.h"

#include <array>
#include <utility>

namespace core {

class ISubmissionStatusLabeler {
public:
    virtual ~ISubmissionStatusLabeler() = default;
    virtual const char* Label(SubmissionStatus status) const = 0;
};

class DefaultSubmissionStatusLabeler final : public ISubmissionStatusLabeler {
public:
    const char* Label(SubmissionStatus status) const override {
        for (const auto& entry : labels_) {
            if (entry.first == status) {
                return entry.second;
            }
        }
        return "Невідомо";
    }

private:
    static constexpr std::array<std::pair<SubmissionStatus, const char*>, 4> labels_ = {{
        {SubmissionStatus::NotLoaded, "Не завантажено"},
        {SubmissionStatus::Cloned, "Отримано"},
        {SubmissionStatus::Analyzed, "Перевірено"},
        {SubmissionStatus::Synced, "Синхронізовано"}
    }};
};

} // namespace core
