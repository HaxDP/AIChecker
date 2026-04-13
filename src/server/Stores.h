#pragma once

#include "server/Domain.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace backend
{
   class LocalSessionStore
   {
   public:
      std::string Create(const GoogleAccount& account, const std::string& accessToken = "");
      std::optional<SessionInfo> Get(const std::string& sid);
      void Remove(const std::string& sid);
   };

   class LocalCacheStore
   {
   public:
      void Set(const std::string& key, const nlohmann::json& value);
   };

   class DataStore
   {
   public:
      DataStore();

      const std::vector<ClassItem>& Classes() const;
      std::vector<TaskItem> TasksByClass(const std::string& classId) const;
      std::vector<SubmissionItem*> SubmissionsByTask(const std::string& taskId);
      SubmissionItem* SubmissionById(const std::string& id);
      nlohmann::json TaskStats(const std::string& taskId);
      void ReplaceClasses(const std::vector<ClassItem>& classes);
      void ReplaceTasksForClass(const std::string& classId, const std::vector<TaskItem>& tasks);
      void ReplaceSubmissionsForTask(const std::string& taskId, const std::vector<SubmissionItem>& submissions);

   private:
      std::vector<ClassItem> classes_;
      std::vector<TaskItem> tasks_;
      std::vector<SubmissionItem> submissions_;
   };
}