#include "server/Utils.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace backend
{
   std::string GetAppDataRoot()
   {
      const char* appData = std::getenv("APPDATA");

      if (appData != nullptr)
      {
         std::filesystem::path root = std::filesystem::path(appData) / "AIChecker";
         std::filesystem::create_directories(root);
         return root.string();
      }

      std::filesystem::path root = std::filesystem::current_path() / "runtime_data";
      std::filesystem::create_directories(root);
      return root.string();
   }

   std::string SessionDir()
   {
      std::filesystem::path p = std::filesystem::path(GetAppDataRoot()) / "sessions";
      std::filesystem::create_directories(p);
      return p.string();
   }

   std::string CacheDir()
   {
      std::filesystem::path p = std::filesystem::path(GetAppDataRoot()) / "cache";
      std::filesystem::create_directories(p);
      return p.string();
   }

   std::string RepoCacheDir()
   {
      std::filesystem::path p = std::filesystem::path(CacheDir()) / "repos";
      std::filesystem::create_directories(p);
      return p.string();
   }

   std::string OutboxDir()
   {
      std::filesystem::path p = std::filesystem::path(GetAppDataRoot()) / "outbox";
      std::filesystem::create_directories(p);
      return p.string();
   }

   std::string RandomId()
   {
      static std::mt19937_64 rng(std::random_device{}());
      static std::uniform_int_distribution<unsigned long long> dist;
      std::ostringstream oss;
      oss << std::hex << dist(rng);
      return oss.str();
   }

   std::string ReadFileText(const std::string& path)
   {
      std::ifstream input(path, std::ios::binary);

      if (!input.is_open())
      {
         return {};
      }

      std::ostringstream buffer;
      buffer << input.rdbuf();
      return buffer.str();
   }

   bool WriteFileText(const std::string& path, const std::string& text)
   {
      std::filesystem::path p(path);

      if (p.has_parent_path())
      {
         std::filesystem::create_directories(p.parent_path());
      }

      std::ofstream output(path, std::ios::binary | std::ios::trunc);

      if (!output.is_open())
      {
         return false;
      }

      output << text;
      return true;
   }

   std::optional<std::string> GetSessionIdFromCookie(const httplib::Request& req)
   {
      const auto cookie = req.get_header_value("Cookie");

      if (cookie.empty())
      {
         return std::nullopt;
      }

      const std::string key = "session_id=";
      const auto pos = cookie.find(key);

      if (pos == std::string::npos)
      {
         return std::nullopt;
      }

      auto end = cookie.find(';', pos);

      if (end == std::string::npos)
      {
         end = cookie.size();
      }

      const std::string value = cookie.substr(pos + key.size(), end - (pos + key.size()));

      if (value.empty())
      {
         return std::nullopt;
      }

      return value;
   }

   nlohmann::json ToJson(const ClassItem& item)
   {
      nlohmann::json out;
      out["id"] = item.id;
      out["name"] = item.name;
      return out;
   }

   nlohmann::json ToJson(const TaskItem& item)
   {
      nlohmann::json out;
      out["id"] = item.id;
      out["classId"] = item.classId;
      out["title"] = item.title;
      return out;
   }

   nlohmann::json ToJson(const SubmissionItem& item)
   {
      nlohmann::json out;
      out["id"] = item.id;
      out["taskId"] = item.taskId;
      out["studentName"] = item.studentName;
      out["studentEmail"] = item.studentEmail.empty() ? "Немає (потрібен scope classroom.profile.emails)" : item.studentEmail;
      out["repositoryUrl"] = item.repositoryUrl;
      out["status"] = item.status;
      out["aiScore"] = item.aiScore;
      out["plagiarismScore"] = item.plagiarismScore;
      out["grade"] = item.grade;
      out["approved"] = item.approved;
      out["sent"] = item.sent;
      out["feedback"] = item.feedback;
      out["aiReport"] = item.aiReport;
      return out;
   }

   void JsonResponse(httplib::Response& res, const nlohmann::json& data, int code)
   {
      res.status = code;
      res.set_header("Content-Type", "application/json; charset=utf-8");
      res.set_content(data.dump(), "application/json; charset=utf-8");
   }
}
