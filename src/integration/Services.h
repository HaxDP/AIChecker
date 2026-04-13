#pragma once

#include "integration/ClassroomStudentExporter.h"
#include "core/Models.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace integration {

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
    explicit ClassroomGatewayStub(
        std::unique_ptr<IClassroomStudentExporter> studentExporter = std::make_unique<GoogleClassroomStudentExporter>())
        : studentExporter_(std::move(studentExporter)) {}

    bool FetchStudents(const std::string& courseId,
                       const std::string& courseWorkId,
                       const std::string& studentGroup,
                       std::vector<core::Student>& outStudents,
                       std::string& outMessage) override {
        outStudents.clear();
        const bool ok = studentExporter_->FetchStudents(courseId, courseWorkId, studentGroup, outStudents, outMessage);
        if (!ok && outMessage.empty()) {
            outMessage = "[Classroom API] Не вдалося імпортувати студентів.";
        }
        return ok;
    }

    bool PushGrade(const core::Submission& submission, const core::Assignment& assignment, std::string& outMessage) override {
        const int maxGrade = static_cast<int>(assignment.maxPoints);
        outMessage = "[Stub Classroom] оцінка=" + std::to_string(submission.result.finalGrade)
            + " / макс=" + std::to_string(maxGrade)
            + " для " + submission.student.email;
        return true;
    }

private:
    std::unique_ptr<IClassroomStudentExporter> studentExporter_;
};

class EmailGatewayStub final : public IEmailGateway {
public:
    bool SendFeedback(const core::Submission& submission, std::string& outMessage) override {
        outMessage = "[Stub Email] надіслано фідбек: " + submission.student.email;
        return true;
    }
};

}