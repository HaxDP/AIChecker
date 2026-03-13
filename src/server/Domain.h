#pragma once

#include <string>
#include <vector>

namespace backend
{
   struct GoogleAccount
   {
      std::string email;
      std::string name;
   };

   struct ClassItem
   {
      std::string id;
      std::string name;
   };

   struct TaskItem
   {
      std::string id;
      std::string classId;
      std::string title;
   };

   struct SubmissionItem
   {
      std::string id;
      std::string taskId;
      std::string studentName;
      std::string studentEmail;
      std::string repositoryUrl;
      std::string status;
      double aiScore = 0.0;
      double plagiarismScore = 0.0;
      int grade = 0;
      bool approved = false;
      bool sent = false;
      std::string feedback = "";
      std::string aiReport = "";
   };

   struct SessionInfo
   {
      std::string sessionId;
      std::string email;
      std::string name;
      std::string accessToken;
   };
}
