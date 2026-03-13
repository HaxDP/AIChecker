#include "server/Controllers.h"

#include "integration/HttpClient.h"
#include "server/Utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <ctime>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>
#endif

namespace backend
{
   namespace
   {
      std::optional<std::string> LoadGoogleClientId()
      {
         const std::vector<std::filesystem::path> candidates = {
            std::filesystem::current_path() / "settings" / "google" / "credentials.json",
            std::filesystem::current_path().parent_path() / "settings" / "google" / "credentials.json"
         };

         std::string raw;

         for (const auto& path : candidates)
         {
            raw = ReadFileText(path.string());

            if (!raw.empty())
            {
               break;
            }
         }

         if (raw.empty())
         {
            return std::nullopt;
         }

         try
         {
            const auto payload = nlohmann::json::parse(raw);

            if (payload.contains("web") && payload["web"].is_object())
            {
               const std::string id = payload["web"].value("client_id", "");

               if (!id.empty())
               {
                  return id;
               }
            }

            if (payload.contains("installed") && payload["installed"].is_object())
            {
               const std::string id = payload["installed"].value("client_id", "");

               if (!id.empty())
               {
                  return id;
               }
            }
         }
         catch (...)
         {
         }

         return std::nullopt;
      }

      bool FetchGoogleProfile(const std::string& accessToken, std::string& outEmail, std::string& outName)
      {
         integration::WinHttpClient client;
         integration::HttpResponse response;
         std::string error;

         const bool ok = client.Get("www.googleapis.com", "/oauth2/v3/userinfo", accessToken, response, error);

         if (!ok || response.statusCode < 200 || response.statusCode >= 300)
         {
            return false;
         }

         try
         {
            const auto payload = nlohmann::json::parse(response.body);
            outEmail = payload.value("email", "");
            outName = payload.value("name", "");

            if (outName.empty())
            {
               outName = outEmail;
            }
         }
         catch (...)
         {
            return false;
         }

         return !outEmail.empty();
      }

      bool ClassroomGetJson(const std::string& path, const std::string& accessToken, nlohmann::json& outPayload, std::string& outError)
      {
         integration::WinHttpClient client;
         integration::HttpResponse response;

         if (!client.Get("classroom.googleapis.com", path, accessToken, response, outError))
         {
            return false;
         }

         if (response.statusCode < 200 || response.statusCode >= 300)
         {
            outError = "Classroom API HTTP статус=" + std::to_string(response.statusCode);
            return false;
         }

         try
         {
            outPayload = nlohmann::json::parse(response.body);
         }
         catch (...)
         {
            outError = "Не вдалося розібрати JSON відповідь Classroom API.";
            return false;
         }

         return true;
      }

      std::optional<std::string> ClassroomGetUserEmail(const std::string& userId, const std::string& accessToken)
      {
         nlohmann::json payload;
         std::string error;

         if (!ClassroomGetJson("/v1/userProfiles/" + userId, accessToken, payload, error))
         {
            return std::nullopt;
         }

         const std::string email = payload.value("emailAddress", "");

         if (email.empty())
         {
            return std::nullopt;
         }

         return email;
      }

#ifdef _WIN32
      std::wstring ToWide(const std::string& text)
      {
         if (text.empty())
         {
            return L"";
         }

         const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);

         if (size <= 0)
         {
            return L"";
         }

         std::wstring out(static_cast<size_t>(size), L'\0');
         MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);

         if (!out.empty() && out.back() == L'\0')
         {
            out.pop_back();
         }

         return out;
      }

      bool ClassroomPatchJson(const std::string& path, const std::string& accessToken, const nlohmann::json& body, std::string& outError)
      {
         HINTERNET hSession = WinHttpOpen(L"AIChecker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

         if (hSession == nullptr)
         {
            outError = "WinHttpOpen failed";
            return false;
         }

         HINTERNET hConnect = WinHttpConnect(hSession, L"classroom.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, 0);

         if (hConnect == nullptr)
         {
            outError = "WinHttpConnect failed";
            WinHttpCloseHandle(hSession);
            return false;
         }

         HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"PATCH", ToWide(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

         if (hRequest == nullptr)
         {
            outError = "WinHttpOpenRequest failed";
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
         }

         std::wstring headers =
            L"Authorization: Bearer " + ToWide(accessToken) + L"\r\n" +
            L"Content-Type: application/json\r\n";

         if (!WinHttpAddRequestHeaders(hRequest, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD))
         {
            outError = "WinHttpAddRequestHeaders failed";
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
         }

         const std::string payload = body.dump();
         BOOL ok = WinHttpSendRequest(
            hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            payload.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)payload.data(),
            static_cast<DWORD>(payload.size()),
            static_cast<DWORD>(payload.size()),
            0);

         if (ok)
         {
            ok = WinHttpReceiveResponse(hRequest, nullptr);
         }

         if (!ok)
         {
            outError = "WinHTTP PATCH failed";
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
         }

         DWORD statusCode = 0;
         DWORD size = sizeof(statusCode);
         WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);

         WinHttpCloseHandle(hRequest);
         WinHttpCloseHandle(hConnect);
         WinHttpCloseHandle(hSession);

         if (statusCode < 200 || statusCode >= 300)
         {
            outError = "Classroom PATCH HTTP статус=" + std::to_string(statusCode);
            return false;
         }

         return true;
      }

      std::string Base64UrlEncode(const std::string& input)
      {
         static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
         std::string out;
         int val = 0;
         int valb = -6;

         for (unsigned char c : input)
         {
            val = (val << 8) + c;
            valb += 8;

            while (valb >= 0)
            {
               out.push_back(table[(val >> valb) & 0x3F]);
               valb -= 6;
            }
         }

         if (valb > -6)
         {
            out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
         }

         while (out.size() % 4)
         {
            out.push_back('=');
         }

         for (char& ch : out)
         {
            if (ch == '+') ch = '-';
            else if (ch == '/') ch = '_';
         }

         while (!out.empty() && out.back() == '=')
         {
            out.pop_back();
         }

         return out;
      }

      bool GmailSendMessage(const std::string& accessToken, const std::string& toEmail, const std::string& subject, const std::string& textBody, std::string& outError)
      {
         if (toEmail.empty())
         {
            outError = "Не знайдено email студента";
            return false;
         }

         const std::string mime =
            "To: " + toEmail + "\r\n" +
            "Subject: " + subject + "\r\n" +
            "Content-Type: text/plain; charset=UTF-8\r\n" +
            "MIME-Version: 1.0\r\n" +
            "\r\n" +
            textBody + "\r\n";

         nlohmann::json body;
         body["raw"] = Base64UrlEncode(mime);

         HINTERNET hSession = WinHttpOpen(L"AIChecker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

         if (hSession == nullptr)
         {
            outError = "WinHttpOpen failed";
            return false;
         }

         HINTERNET hConnect = WinHttpConnect(hSession, L"gmail.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, 0);

         if (hConnect == nullptr)
         {
            outError = "WinHttpConnect failed";
            WinHttpCloseHandle(hSession);
            return false;
         }

         HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/gmail/v1/users/me/messages/send", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

         if (hRequest == nullptr)
         {
            outError = "WinHttpOpenRequest failed";
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
         }

         std::wstring headers =
            L"Authorization: Bearer " + ToWide(accessToken) + L"\r\n" +
            L"Content-Type: application/json\r\n";

         if (!WinHttpAddRequestHeaders(hRequest, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD))
         {
            outError = "WinHttpAddRequestHeaders failed";
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
         }

         const std::string payload = body.dump();
         BOOL ok = WinHttpSendRequest(
            hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            payload.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)payload.data(),
            static_cast<DWORD>(payload.size()),
            static_cast<DWORD>(payload.size()),
            0);

         if (ok)
         {
            ok = WinHttpReceiveResponse(hRequest, nullptr);
         }

         if (!ok)
         {
            outError = "WinHTTP Gmail send failed";
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
         }

         DWORD statusCode = 0;
         DWORD size = sizeof(statusCode);
         WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);

         WinHttpCloseHandle(hRequest);
         WinHttpCloseHandle(hConnect);
         WinHttpCloseHandle(hSession);

         if (statusCode < 200 || statusCode >= 300)
         {
            outError = "Gmail API HTTP статус=" + std::to_string(statusCode) + " (потрібен scope gmail.send)";
            return false;
         }

         return true;
      }
#endif

      std::string EscapeSingleQuotes(std::string text)
      {
         size_t pos = 0;

         while ((pos = text.find('\'', pos)) != std::string::npos)
         {
            text.insert(pos, "'");
            pos += 2;
         }

         return text;
      }

      std::string MakeTaskId(const std::string& courseId, const std::string& courseWorkId)
      {
         return courseId + "__" + courseWorkId;
      }

      bool SplitTaskId(const std::string& taskId, std::string& outCourseId, std::string& outCourseWorkId)
      {
         const auto pos = taskId.find("__");

         if (pos == std::string::npos)
         {
            return false;
         }

         outCourseId = taskId.substr(0, pos);
         outCourseWorkId = taskId.substr(pos + 2);
         return !outCourseId.empty() && !outCourseWorkId.empty();
      }

      std::string ExtractGithubUrl(const nlohmann::json& node)
      {
         if (node.is_string())
         {
            const std::string value = node.get<std::string>();
            const auto pos = value.find("github.com/");

            if (pos == std::string::npos)
            {
               return {};
            }

            if (pos >= 8 && value.substr(pos - 8, 8) == "https://")
            {
               return value.substr(pos - 8);
            }

            if (pos >= 7 && value.substr(pos - 7, 7) == "http://")
            {
               return value.substr(pos - 7);
            }

            return "https://" + value.substr(pos);
         }

         if (node.is_array())
         {
            for (const auto& item : node)
            {
               const std::string found = ExtractGithubUrl(item);

               if (!found.empty())
               {
                  return found;
               }
            }

            return {};
         }

         if (node.is_object())
         {
            for (const auto& [_, value] : node.items())
            {
               const std::string found = ExtractGithubUrl(value);

               if (!found.empty())
               {
                  return found;
               }
            }
         }

         return {};
      }

      std::string NormalizeSubmissionStatus(const std::string& state)
      {
         std::string normalized = state;
         std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c)
         {
            return static_cast<char>(std::toupper(c));
         });

         if (normalized == "TURNED_IN" || normalized == "RETURNED")
         {
            return "completed";
         }

         return "working";
      }

      std::optional<SessionInfo> RequireAuth(const httplib::Request& req, LocalSessionStore& sessions)
      {
         const auto sid = GetSessionIdFromCookie(req);

         if (!sid.has_value())
         {
            return std::nullopt;
         }

         return sessions.Get(sid.value());
      }

      bool TryParseBody(const httplib::Request& req, nlohmann::json& out)
      {
         try
         {
            out = nlohmann::json::parse(req.body);
            return true;
         }
         catch (...)
         {
            return false;
         }
      }

      std::string BuildSubmissionReportText(const SubmissionItem& sub)
      {
         std::ostringstream report;
         report << "Student: " << sub.studentName << "\n";
         report << "Email: " << sub.studentEmail << "\n";
         report << "Repository: " << sub.repositoryUrl << "\n";
         report << "Status: " << sub.status << "\n";
         report << "AI score: " << sub.aiScore << "\n";
         report << "Plagiarism score: " << sub.plagiarismScore << "\n";
         report << "Grade: " << sub.grade << "\n";
         report << "Approved: " << (sub.approved ? "true" : "false") << "\n";
         report << "Sent: " << (sub.sent ? "true" : "false") << "\n";
         report << "\nTeacher feedback:\n" << sub.feedback << "\n";
         report << "\nAI detailed report:\n" << sub.aiReport << "\n";
         return report.str();
      }

      std::string SafeFileName(std::string value)
      {
         for (char& c : value)
         {
            if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            {
               c = '_';
            }
         }

         if (value.empty())
         {
            value = "student";
         }

         return value;
      }
   }

   void RegisterSystemRoutes(httplib::Server& server, const std::string& siteRoot)
   {
      server.Get("/api/health", [&](const httplib::Request&, httplib::Response& res)
      {
         nlohmann::json out;
         out["ok"] = true;
         out["appData"] = GetAppDataRoot();
         JsonResponse(res, out);
      });

      server.set_error_handler([siteRoot](const httplib::Request&, httplib::Response& res)
      {
         if (res.status == 404)
         {
            const std::string html = ReadFileText((std::filesystem::path(siteRoot) / "index.html").string());

            if (!html.empty())
            {
               res.status = 200;
               res.set_content(html, "text/html; charset=utf-8");
            }
         }
      });
   }

   void RegisterAuthRoutes(httplib::Server& server, ApiDependencies& deps)
   {
      server.Get("/api/auth/google/config", [&](const httplib::Request&, httplib::Response& res)
      {
         const auto clientId = LoadGoogleClientId();
         JsonResponse(res, {
            {"enabled", clientId.has_value()},
            {"clientId", clientId.value_or("")}
         });
      });

      server.Post("/api/auth/google/exchange", [&](const httplib::Request& req, httplib::Response& res)
      {
         nlohmann::json body;

         if (!TryParseBody(req, body))
         {
            JsonResponse(res, { {"ok", false}, {"message", "Невірний JSON"} }, 400);
            return;
         }

         const std::string accessToken = body.value("accessToken", "");

         if (accessToken.empty())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Відсутній access token"} }, 400);
            return;
         }

         std::string email;
         std::string name;

         if (!FetchGoogleProfile(accessToken, email, name))
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не вдалося отримати профіль Google"} }, 401);
            return;
         }

         GoogleAccount account;
         account.email = email;
         account.name = name;

         const std::string sid = deps.sessions.Create(account, accessToken);
         res.set_header("Set-Cookie", "session_id=" + sid + "; Path=/; HttpOnly; SameSite=Lax");
         JsonResponse(res, { {"ok", true} });
      });

      server.Get("/api/auth/google/accounts", [&](const httplib::Request&, httplib::Response& res)
      {
         nlohmann::json arr = nlohmann::json::array();

         for (const auto& a : deps.accounts)
         {
            nlohmann::json row;
            row["email"] = a.email;
            row["name"] = a.name;
            arr.push_back(row);
         }

         nlohmann::json out;
         out["accounts"] = arr;
         JsonResponse(res, out);
      });

      server.Post("/api/auth/google/login", [&](const httplib::Request& req, httplib::Response& res)
      {
         nlohmann::json body;

         if (!TryParseBody(req, body))
         {
            JsonResponse(res, { {"ok", false}, {"message", "Невірний JSON"} }, 400);
            return;
         }

         const std::string email = body.value("email", "");
         auto it = std::find_if(deps.accounts.begin(), deps.accounts.end(), [&](const GoogleAccount& a)
         {
            return a.email == email;
         });

         if (it == deps.accounts.end())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Email не знайдено"} }, 400);
            return;
         }

         const std::string sid = deps.sessions.Create(*it);
         res.set_header("Set-Cookie", "session_id=" + sid + "; Path=/; HttpOnly; SameSite=Lax");
         JsonResponse(res, { {"ok", true} });
      });

      server.Get("/api/auth/me", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         JsonResponse(res, {
            {"ok", true},
            {"user", {
               {"email", session->email},
               {"name", session->name}
            }}
         });
      });

      server.Post("/api/auth/logout", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto sid = GetSessionIdFromCookie(req);

         if (sid.has_value())
         {
            deps.sessions.Remove(sid.value());
         }

         res.set_header("Set-Cookie", "session_id=; Path=/; HttpOnly; Max-Age=0; SameSite=Lax");
         JsonResponse(res, { {"ok", true} });
      });
   }

   void RegisterClassroomRoutes(httplib::Server& server, ApiDependencies& deps)
   {
      server.Get("/api/classes", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         if (session->accessToken.empty())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Потрібен вхід через Google popup"} }, 401);
            return;
         }

         nlohmann::json payload;
         std::string error;

         if (!ClassroomGetJson("/v1/courses?pageSize=200&courseStates=ACTIVE", session->accessToken, payload, error))
         {
            JsonResponse(res, { {"ok", false}, {"message", error} }, 502);
            return;
         }

         std::vector<ClassItem> classes;

         if (payload.contains("courses") && payload["courses"].is_array())
         {
            for (const auto& course : payload["courses"])
            {
               ClassItem item;
               item.id = course.value("id", "");
               item.name = course.value("name", "");

               if (!item.id.empty())
               {
                  classes.push_back(item);
               }
            }
         }

         deps.data.ReplaceClasses(classes);

         nlohmann::json arr = nlohmann::json::array();

         for (const auto& c : deps.data.Classes())
         {
            arr.push_back(ToJson(c));
         }

         JsonResponse(res, { {"classes", arr} });
      });

      server.Get(R"(/api/classes/([a-zA-Z0-9_-]+)/tasks)", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         if (session->accessToken.empty())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Потрібен вхід через Google popup"} }, 401);
            return;
         }

         const std::string classId = req.matches[1];

         nlohmann::json payload;
         std::string error;

         if (!ClassroomGetJson("/v1/courses/" + classId + "/courseWork?pageSize=200", session->accessToken, payload, error))
         {
            JsonResponse(res, { {"ok", false}, {"message", error} }, 502);
            return;
         }

         std::vector<TaskItem> tasks;

         if (payload.contains("courseWork") && payload["courseWork"].is_array())
         {
            for (const auto& work : payload["courseWork"])
            {
               const std::string workId = work.value("id", "");

               if (workId.empty())
               {
                  continue;
               }

               TaskItem task;
               task.id = MakeTaskId(classId, workId);
               task.classId = classId;
               task.title = work.value("title", "Без назви");
               tasks.push_back(task);
            }
         }

         deps.data.ReplaceTasksForClass(classId, tasks);
         nlohmann::json arr = nlohmann::json::array();

         for (const auto& t : deps.data.TasksByClass(classId))
         {
            arr.push_back(ToJson(t));
         }

         JsonResponse(res, { {"tasks", arr} });
      });

      server.Get(R"(/api/tasks/([a-zA-Z0-9_-]+)/stats)", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         const std::string taskId = req.matches[1];
         std::string courseId;
         std::string courseWorkId;

         if (!SplitTaskId(taskId, courseId, courseWorkId))
         {
            JsonResponse(res, deps.data.TaskStats(taskId));
            return;
         }

         nlohmann::json payload;
         std::string error;

         if (!ClassroomGetJson("/v1/courses/" + courseId + "/courseWork/" + courseWorkId + "/studentSubmissions?pageSize=500", session->accessToken, payload, error))
         {
            JsonResponse(res, { {"ok", false}, {"message", error} }, 502);
            return;
         }

         int working = 0;
         int completed = 0;

         if (payload.contains("studentSubmissions") && payload["studentSubmissions"].is_array())
         {
            for (const auto& submission : payload["studentSubmissions"])
            {
               const std::string status = NormalizeSubmissionStatus(submission.value("state", ""));

               if (status == "completed")
               {
                  completed++;
               }
               else
               {
                  working++;
               }
            }
         }

         JsonResponse(res, {
            {"taskId", taskId},
            {"working", working},
            {"completed", completed},
            {"inReview", 0}
         });
      });

      server.Get(R"(/api/tasks/([a-zA-Z0-9_-]+)/submissions)", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         const std::string taskId = req.matches[1];
         std::string courseId;
         std::string courseWorkId;

         if (SplitTaskId(taskId, courseId, courseWorkId))
         {
            nlohmann::json studentsPayload;
            std::string studentsError;

            if (!ClassroomGetJson("/v1/courses/" + courseId + "/students?pageSize=500", session->accessToken, studentsPayload, studentsError))
            {
               JsonResponse(res, { {"ok", false}, {"message", studentsError} }, 502);
               return;
            }

            std::unordered_map<std::string, std::pair<std::string, std::string>> users;

            if (studentsPayload.contains("students") && studentsPayload["students"].is_array())
            {
               for (const auto& student : studentsPayload["students"])
               {
                  const std::string userId = student.value("userId", "");

                  if (userId.empty())
                  {
                     continue;
                  }

                  const std::string fullName = student.value("profile", nlohmann::json::object())
                     .value("name", nlohmann::json::object())
                     .value("fullName", "");
                  const std::string email = student.value("profile", nlohmann::json::object())
                     .value("emailAddress", "");
                  users[userId] = { fullName, email };
               }
            }

            nlohmann::json submissionsPayload;
            std::string submissionsError;

            if (!ClassroomGetJson("/v1/courses/" + courseId + "/courseWork/" + courseWorkId + "/studentSubmissions?pageSize=500", session->accessToken, submissionsPayload, submissionsError))
            {
               JsonResponse(res, { {"ok", false}, {"message", submissionsError} }, 502);
               return;
            }

            std::vector<SubmissionItem> refreshed;

            if (submissionsPayload.contains("studentSubmissions") && submissionsPayload["studentSubmissions"].is_array())
            {
               for (const auto& submission : submissionsPayload["studentSubmissions"])
               {
                  const std::string submissionId = submission.value("id", "");
                  const std::string userId = submission.value("userId", "");

                  if (submissionId.empty())
                  {
                     continue;
                  }

                  SubmissionItem item;
                  item.id = submissionId;
                  item.taskId = taskId;
                  item.status = NormalizeSubmissionStatus(submission.value("state", ""));
                  item.repositoryUrl = ExtractGithubUrl(submission);

                  const auto userIt = users.find(userId);

                  if (userIt != users.end())
                  {
                     item.studentName = userIt->second.first;
                     item.studentEmail = userIt->second.second;

                     if (item.studentEmail.empty() && session->accessToken.size() > 0)
                     {
                        const auto fetchedEmail = ClassroomGetUserEmail(userId, session->accessToken);

                        if (fetchedEmail.has_value())
                        {
                           item.studentEmail = fetchedEmail.value();
                        }
                     }
                  }
                  else
                  {
                     item.studentName = "Студент " + userId;

                     if (session->accessToken.size() > 0)
                     {
                        const auto fetchedEmail = ClassroomGetUserEmail(userId, session->accessToken);

                        if (fetchedEmail.has_value())
                        {
                           item.studentEmail = fetchedEmail.value();
                        }
                     }
                  }

                  refreshed.push_back(item);
               }
            }

            deps.data.ReplaceSubmissionsForTask(taskId, refreshed);
         }

         nlohmann::json arr = nlohmann::json::array();

         for (auto* s : deps.data.SubmissionsByTask(taskId))
         {
            arr.push_back(ToJson(*s));
         }

         JsonResponse(res, { {"submissions", arr} });
      });

      server.Post(R"(/api/tasks/([a-zA-Z0-9_-]+)/pull-repos)", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         const std::string taskId = req.matches[1];
         auto refs = deps.data.SubmissionsByTask(taskId);
         const nlohmann::json pulled = deps.repoService.Pull(refs);
         deps.cache.Set("repos_" + taskId, pulled);
         JsonResponse(res, { {"rows", pulled} });
      });

      server.Post(R"(/api/tasks/([a-zA-Z0-9_-]+)/send-grades)", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         if (session->accessToken.empty())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Потрібен вхід через Google popup"} }, 401);
            return;
         }

         const std::string taskId = req.matches[1];
         std::string courseId;
         std::string courseWorkId;

         if (!SplitTaskId(taskId, courseId, courseWorkId))
         {
            JsonResponse(res, { {"ok", false}, {"message", "Невірний taskId"} }, 400);
            return;
         }

         int sent = 0;
         int skipped = 0;
         nlohmann::json rows = nlohmann::json::array();

         for (auto* sub : deps.data.SubmissionsByTask(taskId))
         {
            nlohmann::json row;
            row["submissionId"] = sub->id;
            row["studentName"] = sub->studentName;

            if (!sub->approved || sub->grade <= 0)
            {
               skipped++;
               row["ok"] = false;
               row["message"] = "Пропущено: робота не підтверджена або оцінка не задана";
               rows.push_back(row);
               continue;
            }

#ifdef _WIN32
            std::string error;
            const std::string patchPath =
               "/v1/courses/" + courseId +
               "/courseWork/" + courseWorkId +
               "/studentSubmissions/" + sub->id +
               "?updateMask=draftGrade";

            nlohmann::json body = {
               {"draftGrade", sub->grade}
            };

            if (ClassroomPatchJson(patchPath, session->accessToken, body, error))
            {
               sent++;
               row["ok"] = true;
               row["grade"] = sub->grade;
            }
            else
            {
               row["ok"] = false;
               row["message"] = error;
            }
#else
            row["ok"] = false;
            row["message"] = "Підтримується тільки на Windows";
#endif
            rows.push_back(row);
         }

         JsonResponse(res, {
            {"ok", true},
            {"sent", sent},
            {"skipped", skipped},
            {"rows", rows}
         });
      });

      server.Post(R"(/api/tasks/([a-zA-Z0-9_-]+)/send-comments-email)", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         const std::string taskId = req.matches[1];
         nlohmann::json rows = nlohmann::json::array();
         int sent = 0;

         for (auto* sub : deps.data.SubmissionsByTask(taskId))
         {
            nlohmann::json result;
#ifdef _WIN32
            if (!session->accessToken.empty() && !sub->studentEmail.empty())
            {
               std::string err;
               const bool mailed = GmailSendMessage(
                  session->accessToken,
                  sub->studentEmail,
                  "Результат перевірки роботи",
                  sub->feedback.empty() ? "Робота перевірена." : sub->feedback,
                  err);

               if (mailed)
               {
                  sub->sent = true;
                  sent++;
                  result = {
                     {"submissionId", sub->id},
                     {"ok", true},
                     {"message", "Лист надіслано через Gmail API"}
                  };
               }
               else
               {
                  result = deps.finalService.Send(*sub);
                  result["message"] = "Gmail не надіслав: " + err + ". Зроблено fallback у outbox.";
                  if (result.value("ok", false))
                  {
                     sent++;
                  }
               }
            }
            else
            {
               result = deps.finalService.Send(*sub);
               if (result.value("ok", false))
               {
                  sent++;
               }
            }
#else
            result = deps.finalService.Send(*sub);
            if (result.value("ok", false))
            {
               sent++;
            }
#endif
            rows.push_back(result);
         }

         JsonResponse(res, {
            {"ok", true},
            {"sent", sent},
            {"rows", rows}
         });
      });

      server.Post(R"(/api/tasks/([a-zA-Z0-9_-]+)/export-zip)", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         const std::string taskId = req.matches[1];
         const auto now = static_cast<long long>(std::time(nullptr));
         const std::string exportName = "task_" + taskId + "_" + std::to_string(now);
         const std::filesystem::path exportRoot = std::filesystem::path(OutboxDir()) / "exports" / exportName;
         std::filesystem::create_directories(exportRoot);

         auto refs = deps.data.SubmissionsByTask(taskId);

         for (const auto* sub : refs)
         {
            const std::filesystem::path reportFile = exportRoot / (sub->id + "_" + SafeFileName(sub->studentName) + ".txt");
            WriteFileText(reportFile.string(), BuildSubmissionReportText(*sub));
         }

         const std::filesystem::path zipPath = std::filesystem::path(OutboxDir()) / "exports" / (exportName + ".zip");
#ifdef _WIN32
         const std::string cmd =
            "powershell -NoProfile -ExecutionPolicy Bypass -Command \"Compress-Archive -Path '" +
            EscapeSingleQuotes((exportRoot.string() + "\\*")) +
            "' -DestinationPath '" +
            EscapeSingleQuotes(zipPath.string()) +
            "' -Force\"";

         const int rc = std::system(cmd.c_str());

         if (rc != 0 || !std::filesystem::exists(zipPath))
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не вдалося створити ZIP"} }, 500);
            return;
         }

         JsonResponse(res, {
            {"ok", true},
            {"zipPath", zipPath.string()},
            {"downloadUrl", "/api/exports/" + zipPath.filename().string()}
         });
#else
         JsonResponse(res, { {"ok", false}, {"message", "Підтримується тільки на Windows"} }, 500);
#endif
      });

      server.Get(R"(/api/exports/([a-zA-Z0-9_.-]+))", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         const std::string fileName = req.matches[1];
         const std::filesystem::path zipPath = std::filesystem::path(OutboxDir()) / "exports" / fileName;
         const std::string body = ReadFileText(zipPath.string());

         if (body.empty())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Файл не знайдено"} }, 404);
            return;
         }

         res.status = 200;
         res.set_header("Content-Type", "application/zip");
         res.set_header("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
         res.set_content(body, "application/zip");
      });
   }

   void RegisterReviewRoutes(httplib::Server& server, ApiDependencies& deps)
   {
      server.Post("/api/review/ai", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         nlohmann::json body;

         if (!TryParseBody(req, body))
         {
            JsonResponse(res, { {"ok", false}, {"message", "Невірний JSON"} }, 400);
            return;
         }

         const std::string submissionId = body.value("submissionId", "");
         const std::string model = body.value("model", "llama3.2:3b");
         const double temperature = body.value("temperature", 0.1);

         SubmissionItem* sub = deps.data.SubmissionById(submissionId);

         if (sub == nullptr)
         {
            JsonResponse(res, { {"ok", false}, {"message", "Роботу не знайдено"} }, 404);
            return;
         }

         JsonResponse(res, deps.aiService.Analyze(*sub, model, temperature));
      });

      server.Post("/api/review/plagiarism", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         nlohmann::json body;

         if (!TryParseBody(req, body))
         {
            JsonResponse(res, { {"ok", false}, {"message", "Невірний JSON"} }, 400);
            return;
         }

         const std::string submissionId = body.value("submissionId", "");
         SubmissionItem* sub = deps.data.SubmissionById(submissionId);

         if (sub == nullptr)
         {
            JsonResponse(res, { {"ok", false}, {"message", "Роботу не знайдено"} }, 404);
            return;
         }

         JsonResponse(res, deps.plagiarismService.Analyze(*sub));
      });

      server.Post("/api/review/finalize", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         nlohmann::json body;

         if (!TryParseBody(req, body))
         {
            JsonResponse(res, { {"ok", false}, {"message", "Невірний JSON"} }, 400);
            return;
         }

         const std::string submissionId = body.value("submissionId", "");
         const int grade = body.value("grade", 2);
         const std::string feedback = body.value("feedback", "");

         SubmissionItem* sub = deps.data.SubmissionById(submissionId);

         if (sub == nullptr)
         {
            JsonResponse(res, { {"ok", false}, {"message", "Роботу не знайдено"} }, 404);
            return;
         }

         JsonResponse(res, deps.finalService.Approve(*sub, grade, feedback));
      });

      server.Post("/api/review/send-email", [&](const httplib::Request& req, httplib::Response& res)
      {
         const auto session = RequireAuth(req, deps.sessions);

         if (!session.has_value())
         {
            JsonResponse(res, { {"ok", false}, {"message", "Не авторизовано"} }, 401);
            return;
         }

         nlohmann::json body;

         if (!TryParseBody(req, body))
         {
            JsonResponse(res, { {"ok", false}, {"message", "Невірний JSON"} }, 400);
            return;
         }

         const std::string submissionId = body.value("submissionId", "");
         SubmissionItem* sub = deps.data.SubmissionById(submissionId);

         if (sub == nullptr)
         {
            JsonResponse(res, { {"ok", false}, {"message", "Роботу не знайдено"} }, 404);
            return;
         }

#ifdef _WIN32
         if (!session->accessToken.empty() && !sub->studentEmail.empty())
         {
            std::string err;
            const bool mailed = GmailSendMessage(
               session->accessToken,
               sub->studentEmail,
               "Результат перевірки роботи",
               sub->feedback.empty() ? "Робота перевірена." : sub->feedback,
               err);

            if (mailed)
            {
               sub->sent = true;
               JsonResponse(res, {
                  {"submissionId", sub->id},
                  {"ok", true},
                  {"message", "Лист надіслано через Gmail API"}
               });
               return;
            }

            auto fallback = deps.finalService.Send(*sub);
            fallback["message"] = "Gmail не надіслав: " + err + ". Зроблено fallback у outbox.";
            JsonResponse(res, fallback);
            return;
         }
#endif

         JsonResponse(res, deps.finalService.Send(*sub));
      });
   }
}
