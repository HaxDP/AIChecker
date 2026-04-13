#pragma once

#include "server/Domain.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace backend
{
   std::string GetAppDataRoot();
   std::string SessionDir();
   std::string CacheDir();
   std::string RepoCacheDir();
   std::string OutboxDir();

   std::string RandomId();
   std::string ReadFileText(const std::string& path);
   bool WriteFileText(const std::string& path, const std::string& text);

   std::optional<std::string> GetSessionIdFromCookie(const httplib::Request& req);

   nlohmann::json ToJson(const ClassItem& item);
   nlohmann::json ToJson(const TaskItem& item);
   nlohmann::json ToJson(const SubmissionItem& item);

   void JsonResponse(httplib::Response& res, const nlohmann::json& data, int code = 200);
}