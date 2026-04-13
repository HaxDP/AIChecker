#include "server/Stores.h"

#include "server/Utils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <unordered_map>

namespace backend
{
   std::string LocalSessionStore::Create(const GoogleAccount& account, const std::string& accessToken)
   {
      const std::string sid = RandomId();

      nlohmann::json data;
      data["sessionId"] = sid;
      data["email"] = account.email;
      data["name"] = account.name;
      data["accessToken"] = accessToken;
      data["createdAt"] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

      const std::string path = (std::filesystem::path(SessionDir()) / (sid + ".json")).string();
      WriteFileText(path, data.dump(2));
      return sid;
   }

   std::optional<SessionInfo> LocalSessionStore::Get(const std::string& sid)
   {
      const std::string path = (std::filesystem::path(SessionDir()) / (sid + ".json")).string();
      const std::string raw = ReadFileText(path);

      if (raw.empty())
      {
         return std::nullopt;
      }

      nlohmann::json data;

      try
      {
         data = nlohmann::json::parse(raw);
      }
      catch (...)
      {
         return std::nullopt;
      }

      SessionInfo info;
      info.sessionId = data.value("sessionId", "");
      info.email = data.value("email", "");
      info.name = data.value("name", "");
      info.accessToken = data.value("accessToken", "");

      if (info.sessionId.empty())
      {
         return std::nullopt;
      }

      return info;
   }

   void LocalSessionStore::Remove(const std::string& sid)
   {
      const std::filesystem::path p = std::filesystem::path(SessionDir()) / (sid + ".json");

      if (std::filesystem::exists(p))
      {
         std::filesystem::remove(p);
      }
   }

   void LocalCacheStore::Set(const std::string& key, const nlohmann::json& value)
   {
      const std::filesystem::path p = std::filesystem::path(CacheDir()) / (key + ".json");
      WriteFileText(p.string(), value.dump(2));
   }

   DataStore::DataStore()
   {
   }

   const std::vector<ClassItem>& DataStore::Classes() const
   {
      return classes_;
   }

   std::vector<TaskItem> DataStore::TasksByClass(const std::string& classId) const
   {
      std::vector<TaskItem> out;

      for (const auto& t : tasks_)
      {
         if (t.classId == classId)
         {
            out.push_back(t);
         }
      }

      return out;
   }

   std::vector<SubmissionItem*> DataStore::SubmissionsByTask(const std::string& taskId)
   {
      std::vector<SubmissionItem*> out;

      for (auto& s : submissions_)
      {
         if (s.taskId == taskId)
         {
            out.push_back(&s);
         }
      }

      return out;
   }

   SubmissionItem* DataStore::SubmissionById(const std::string& id)
   {
      for (auto& s : submissions_)
      {
         if (s.id == id)
         {
            return &s;
         }
      }

      return nullptr;
   }

   nlohmann::json DataStore::TaskStats(const std::string& taskId)
   {
      int missing = 0;
      int turnedIn = 0;
      int turnedInLate = 0;
      int inReview = 0;

      for (auto* s : SubmissionsByTask(taskId))
      {
         if (s->approved)
         {
            inReview++;
            continue;
         }

         if (s->status == "turned_in" && s->late)
         {
            turnedInLate++;
         }
         else if (s->status == "turned_in")
         {
            turnedIn++;
         }
         else
         {
            missing++;
         }
      }

      nlohmann::json result;
      result["taskId"] = taskId;
      result["working"] = missing;
      result["completed"] = turnedIn + turnedInLate;
      result["missing"] = missing;
      result["turnedIn"] = turnedIn;
      result["turnedInLate"] = turnedInLate;
      result["inReview"] = inReview;
      return result;
   }

   void DataStore::ReplaceClasses(const std::vector<ClassItem>& classes)
   {
      classes_ = classes;
   }

   void DataStore::ReplaceTasksForClass(const std::string& classId, const std::vector<TaskItem>& tasks)
   {
      tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [&](const TaskItem& item)
      {
         return item.classId == classId;
      }), tasks_.end());

      tasks_.insert(tasks_.end(), tasks.begin(), tasks.end());
   }

   void DataStore::ReplaceSubmissionsForTask(const std::string& taskId, const std::vector<SubmissionItem>& submissions)
   {
      std::unordered_map<std::string, SubmissionItem> oldById;

      for (const auto& item : submissions_)
      {
         if (item.taskId == taskId)
         {
            oldById[item.id] = item;
         }
      }

      submissions_.erase(std::remove_if(submissions_.begin(), submissions_.end(), [&](const SubmissionItem& item)
      {
         return item.taskId == taskId;
      }), submissions_.end());

      for (auto item : submissions)
      {
         const auto existing = oldById.find(item.id);

         if (existing != oldById.end())
         {
            item.aiScore = existing->second.aiScore;
            item.plagiarismScore = existing->second.plagiarismScore;
            item.grade = existing->second.grade;
            item.approved = existing->second.approved;
            item.sent = existing->second.sent;
            item.feedback = existing->second.feedback;
            item.teacherComment = existing->second.teacherComment;
            item.detailedDescription = existing->second.detailedDescription;
            item.aiReport = existing->second.aiReport;
         }

         submissions_.push_back(item);
      }
   }
}