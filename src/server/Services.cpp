#include "server/Services.h"

#include "server/Utils.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <unordered_set>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>
#endif

namespace backend
{
   namespace
   {
      bool IsReadmeFile(const std::filesystem::path& p)
      {
         std::string name = p.filename().string();
         std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
         {
            return static_cast<char>(std::tolower(c));
         });
         return name == "readme.md" || name == "readme.txt" || name == "readme";
      }

      bool IsLikelyCodeOrDocs(const std::filesystem::path& p)
      {
         static const std::unordered_set<std::string> allowed = {
            ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
            ".py", ".js", ".ts", ".tsx", ".jsx", ".java", ".cs", ".go", ".rs", ".kt",
            ".swift", ".php", ".rb", ".sh", ".ps1", ".sql", ".html", ".css",
            ".json", ".yaml", ".yml", ".toml", ".xml", ".md", ".txt"
         };

         std::string ext = p.extension().string();
         std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
         {
            return static_cast<char>(std::tolower(c));
         });
         return allowed.find(ext) != allowed.end() || IsReadmeFile(p);
      }

      std::string ToPromptSafe(std::string text, size_t maxChars)
      {
         for (char& c : text)
         {
            if (c == '\0')
            {
               c = ' ';
            }
         }

         if (text.size() > maxChars)
         {
            text.resize(maxChars);
            text += "\n...[TRUNCATED]";
         }

         return text;
      }

      std::string BuildRepositoryContext(const SubmissionItem& submission)
      {
         const std::filesystem::path repoRoot = std::filesystem::path(RepoCacheDir()) / submission.id;

         if (!std::filesystem::exists(repoRoot))
         {
            return "Repository cache is missing. Ask user to press pull-repos before AI review.";
         }

         std::vector<std::filesystem::path> readmes;
         std::vector<std::filesystem::path> others;

         for (const auto& entry : std::filesystem::recursive_directory_iterator(repoRoot))
         {
            if (!entry.is_regular_file())
            {
               continue;
            }

            const auto rel = std::filesystem::relative(entry.path(), repoRoot);

            if (rel.empty())
            {
               continue;
            }

            const std::string relStr = rel.string();

            if (relStr.rfind(".git", 0) == 0)
            {
               continue;
            }

            if (!IsLikelyCodeOrDocs(rel))
            {
               continue;
            }

            if (IsReadmeFile(rel))
            {
               readmes.push_back(rel);
            }
            else
            {
               others.push_back(rel);
            }
         }

         std::sort(readmes.begin(), readmes.end());
         std::sort(others.begin(), others.end());

         std::ostringstream out;
         out << "Repository URL: " << submission.repositoryUrl << "\n";
         out << "Student: " << submission.studentName << "\n\n";

         size_t filesUsed = 0;
         size_t totalChars = 0;
         const size_t maxFiles = 18;
         const size_t maxTotalChars = 24000;
         const size_t maxPerFile = 2200;

         auto appendFile = [&](const std::filesystem::path& rel)
         {
            if (filesUsed >= maxFiles || totalChars >= maxTotalChars)
            {
               return;
            }

            const auto full = repoRoot / rel;
            std::string content = ReadFileText(full.string());

            if (content.empty())
            {
               return;
            }

            content = ToPromptSafe(content, maxPerFile);
            totalChars += content.size();
            filesUsed++;

            out << "### FILE: " << rel.string() << "\n";
            out << content << "\n\n";
         };

         for (const auto& p : readmes)
         {
            appendFile(p);
         }

         for (const auto& p : others)
         {
            appendFile(p);
         }

         out << "Files included: " << filesUsed << "\n";
         return out.str();
      }

      std::string NormalizeCloneUrl(std::string url)
      {
         // Convert GitHub tree/blob links to repository clone URL.
         const std::string host = "github.com/";
         const auto pos = url.find(host);

         if (pos == std::string::npos)
         {
            return url;
         }

         std::string tail = url.substr(pos + host.size());

         if (!tail.empty() && tail.back() == '/')
         {
            tail.pop_back();
         }

         const auto treePos = tail.find("/tree/");
         const auto blobPos = tail.find("/blob/");
         const size_t cutPos = (treePos != std::string::npos) ? treePos : blobPos;

         if (cutPos != std::string::npos)
         {
            tail = tail.substr(0, cutPos);
         }

         // Keep only owner/repo.
         size_t slashCount = 0;
         size_t end = 0;

         for (; end < tail.size(); ++end)
         {
            if (tail[end] == '/')
            {
               slashCount++;

               if (slashCount == 2)
               {
                  break;
               }
            }
         }

         if (slashCount >= 2)
         {
            tail = tail.substr(0, end);
         }

         if (tail.find('/') == std::string::npos)
         {
            return url;
         }

         if (tail.size() < 4 || tail.substr(tail.size() - 4) != ".git")
         {
            tail += ".git";
         }

         return "https://github.com/" + tail;
      }

      bool ValidateAndNormalizeAiJson(const std::string& text, nlohmann::json& out)
      {
         try
         {
            out = nlohmann::json::parse(text);
         }
         catch (...)
         {
            return false;
         }

         if (!out.is_object())
         {
            return false;
         }

         if (!out.contains("score") || !out["score"].is_number())
         {
            return false;
         }

         const std::vector<std::string> stringFields = { "verdict", "thinking", "email_comment" };

         for (const auto& field : stringFields)
         {
            if (!out.contains(field) || !out[field].is_string())
            {
               return false;
            }
         }

         const std::vector<std::string> arrayFields = { "indicators", "strong_parts", "issues", "recommendations" };

         for (const auto& field : arrayFields)
         {
            if (!out.contains(field) || !out[field].is_array())
            {
               return false;
            }
         }

         // Clamp score to expected range and keep only schema fields.
         double score = out.value("score", 0.0);

         if (score < 0.0)
         {
            score = 0.0;
         }

         if (score > 100.0)
         {
            score = 100.0;
         }

         nlohmann::json normalized;
         normalized["score"] = score;
         normalized["verdict"] = out.value("verdict", "");
         normalized["thinking"] = out.value("thinking", "");
         normalized["indicators"] = out.value("indicators", nlohmann::json::array());
         normalized["strong_parts"] = out.value("strong_parts", nlohmann::json::array());
         normalized["issues"] = out.value("issues", nlohmann::json::array());
         normalized["recommendations"] = out.value("recommendations", nlohmann::json::array());
         normalized["email_comment"] = out.value("email_comment", "");
         out = normalized;
         return true;
      }
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

   bool PostJson(const std::string& url, const nlohmann::json& request, std::string& responseBody, int& statusCode)
   {
      static const std::regex re(R"(^(https?)://([^/:]+)(?::(\d+))?(.*)$)", std::regex::icase);
      std::smatch m;

      if (!std::regex_match(url, m, re))
      {
         return false;
      }

      std::string scheme = m[1].str();
      std::string host = m[2].str();
      int port = scheme == "https" ? 443 : 80;
      std::string path = m[4].str().empty() ? "/" : m[4].str();

      if (m[3].matched)
      {
         port = std::stoi(m[3].str());
      }

      const DWORD flags = (scheme == "https") ? WINHTTP_FLAG_SECURE : 0;

      HINTERNET hSession = WinHttpOpen(L"AICheckerWeb/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

      if (hSession == nullptr)
      {
         return false;
      }

      HINTERNET hConnect = WinHttpConnect(hSession, ToWide(host).c_str(), static_cast<INTERNET_PORT>(port), 0);

      if (hConnect == nullptr)
      {
         WinHttpCloseHandle(hSession);
         return false;
      }

      HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", ToWide(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

      if (hRequest == nullptr)
      {
         WinHttpCloseHandle(hConnect);
         WinHttpCloseHandle(hSession);
         return false;
      }

      const std::string body = request.dump();
      std::wstring headers = L"Content-Type: application/json\r\n";

      BOOL ok = WinHttpSendRequest(hRequest, headers.c_str(), static_cast<DWORD>(-1), body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);

      if (ok)
      {
         ok = WinHttpReceiveResponse(hRequest, nullptr);
      }

      if (!ok)
      {
         WinHttpCloseHandle(hRequest);
         WinHttpCloseHandle(hConnect);
         WinHttpCloseHandle(hSession);
         return false;
      }

      DWORD nativeStatus = 0;
      DWORD sz = sizeof(nativeStatus);
      WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &nativeStatus, &sz, WINHTTP_NO_HEADER_INDEX);
      statusCode = static_cast<int>(nativeStatus);

      responseBody.clear();
      DWORD available = 0;

      do
      {
         if (!WinHttpQueryDataAvailable(hRequest, &available))
         {
            break;
         }

         if (available == 0)
         {
            break;
         }

         std::string buffer(available, '\0');
         DWORD read = 0;

         if (!WinHttpReadData(hRequest, buffer.data(), available, &read))
         {
            break;
         }

         buffer.resize(read);
         responseBody += buffer;
      }
      while (available > 0);

      WinHttpCloseHandle(hRequest);
      WinHttpCloseHandle(hConnect);
      WinHttpCloseHandle(hSession);
      return true;
   }
#endif

   nlohmann::json RepositoryService::Pull(const std::vector<SubmissionItem*>& submissions)
   {
      nlohmann::json rows = nlohmann::json::array();

      for (auto* s : submissions)
      {
         const std::filesystem::path localPath = std::filesystem::path(RepoCacheDir()) / s->id;
         std::filesystem::create_directories(localPath.parent_path());

         std::ostringstream cmd;

         if (std::filesystem::exists(localPath / ".git"))
         {
            cmd << "git -C \"" << localPath.string() << "\" pull --ff-only";
         }
         else
         {
            cmd << "git clone --depth 1 \"" << NormalizeCloneUrl(s->repositoryUrl) << "\" \"" << localPath.string() << "\"";
         }

         const int rc = std::system(cmd.str().c_str());

         nlohmann::json r;
         r["submissionId"] = s->id;
         r["repositoryUrl"] = s->repositoryUrl;
         r["localPath"] = localPath.string();
         r["ok"] = (rc == 0);
         rows.push_back(r);
      }

      return rows;
   }

   nlohmann::json AiReviewService::Analyze(SubmissionItem& submission, const std::string& model, double temperature)
   {
      double score = Heuristic(submission);
      std::string thinking = "Fallback-режим";
      const std::string context = BuildRepositoryContext(submission);

#ifdef _WIN32
      nlohmann::json request;
      request["model"] = model;
      request["stream"] = false;
      const std::string customSystemPrompt = ReadFileText((std::filesystem::path("settings") / "ollama" / "SYSTEM_PROMPT.txt").string());

      request["system"] = customSystemPrompt.empty()
         ? "You are AIChecker evaluator. Return only valid JSON, no markdown and no extra text. Be strict and evidence-based."
         : customSystemPrompt;

      request["options"] = {
         {"temperature", temperature},
         {"top_p", 1.0},
         {"seed", 42},
         {"num_predict", 700}
      };
      const std::string basePrompt =
         "Analyze student repository context and produce strict JSON with this schema: "
         "{\"score\": number, \"verdict\": string, \"thinking\": string, \"indicators\": [string], "
         "\"strong_parts\": [string], \"issues\": [string], \"recommendations\": [string], \"email_comment\": string}. "
         "Score must be 0..100 where higher means more likely AI-generated. Keep thinking concise and evidence-based. "
         "Do not add fields outside schema.\n\n"
         "Repository context:\n" + context;
      request["prompt"] = basePrompt;

      int status = 0;
      std::string response;
      const bool ok = PostJson("http://localhost:11434/api/generate", request, response, status);

      if (ok && status >= 200 && status < 300)
      {
         try
         {
            const auto outer = nlohmann::json::parse(response);
            const std::string text = outer.value("response", "");

            if (!text.empty())
            {
               nlohmann::json strictJson;

               if (ValidateAndNormalizeAiJson(text, strictJson))
               {
                  score = strictJson.value("score", score);
                  thinking = strictJson.dump(2);
               }
               else
               {
                  // Retry once with repair instruction if model broke template.
                  nlohmann::json repairRequest = request;
                  repairRequest["prompt"] =
                     "Your previous output was not valid for schema. Repair it. "
                     "Return ONLY valid JSON with required fields and correct types.\n\n"
                     "Original output:\n" + text + "\n\n"
                     "Re-analyze using repository context:\n" + context;

                  std::string repairedResponse;
                  int repairedStatus = 0;
                  const bool repairedOk = PostJson("http://localhost:11434/api/generate", repairRequest, repairedResponse, repairedStatus);

                  if (repairedOk && repairedStatus >= 200 && repairedStatus < 300)
                  {
                     try
                     {
                        const auto repairedOuter = nlohmann::json::parse(repairedResponse);
                        const std::string repairedText = repairedOuter.value("response", "");
                        nlohmann::json repairedStrict;

                        if (ValidateAndNormalizeAiJson(repairedText, repairedStrict))
                        {
                           score = repairedStrict.value("score", score);
                           thinking = repairedStrict.dump(2);
                        }
                        else
                        {
                           score = ExtractScore(repairedText, score);
                           thinking = repairedText;
                        }
                     }
                     catch (...)
                     {
                        score = ExtractScore(text, score);
                        thinking = text;
                     }
                  }
                  else
                  {
                     score = ExtractScore(text, score);
                     thinking = text;
                  }
               }
            }
            else
            {
               thinking = "Ollama без тексту";
            }
         }
         catch (...)
         {
         }
      }
#endif

      if (score < 0.0)
      {
         score = 0.0;
      }

      if (score > 100.0)
      {
         score = 100.0;
      }

      submission.aiScore = score;
      submission.aiReport = thinking;

      nlohmann::json out;
      out["submissionId"] = submission.id;
      out["aiScore"] = score;
      out["thinking"] = thinking;
      out["decision"] = score >= 70.0 ? "Ймовірно AI" : (score >= 40.0 ? "Потрібна ручна перевірка" : "Ознак AI мало");
      out["source"] = "repo+readme";
      return out;
   }

   double AiReviewService::Heuristic(const SubmissionItem& submission)
   {
      std::hash<std::string> h;
      return static_cast<double>(h(submission.repositoryUrl) % 100);
   }

   double AiReviewService::ExtractScore(const std::string& text, double fallback)
   {
      static const std::regex rx(R"((-?\d+(?:\.\d+)?))");
      std::sregex_iterator it(text.begin(), text.end(), rx);
      std::sregex_iterator end;

      if (it == end)
      {
         return fallback;
      }

      double val = fallback;

      for (; it != end; ++it)
      {
         try
         {
            val = std::stod((*it)[1].str());
         }
         catch (...)
         {
         }
      }

      return val;
   }

   PlagiarismService::PlagiarismService(std::string datasetPath)
      : datasetPath_(std::move(datasetPath))
   {
   }

   nlohmann::json PlagiarismService::Analyze(SubmissionItem& submission)
   {
      const auto dataset = ReadDataset();
      const auto tokensA = Tokenize(submission.repositoryUrl);
      double best = 0.0;

      for (const auto& row : dataset)
      {
         const auto tokensB = Tokenize(row);
         best = std::max(best, Jaccard(tokensA, tokensB));
      }

      submission.plagiarismScore = best * 100.0;

      nlohmann::json out;
      out["submissionId"] = submission.id;
      out["plagiarismScore"] = submission.plagiarismScore;
      out["dataset"] = "fixed";
      return out;
   }

   std::vector<std::string> PlagiarismService::ReadDataset()
   {
      std::vector<std::string> lines;
      std::ifstream input(datasetPath_);
      std::string line;

      while (std::getline(input, line))
      {
         if (!line.empty())
         {
            lines.push_back(line);
         }
      }

      if (lines.empty())
      {
         lines.push_back("for while if class template vector");
      }

      return lines;
   }

   std::set<std::string> PlagiarismService::Tokenize(const std::string& text)
   {
      std::set<std::string> out;
      std::regex split(R"([^A-Za-z0-9_]+)");
      std::sregex_token_iterator it(text.begin(), text.end(), split, -1);
      std::sregex_token_iterator end;

      for (; it != end; ++it)
      {
         std::string t = it->str();

         if (t.empty())
         {
            continue;
         }

         std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c)
         {
            return static_cast<char>(std::tolower(c));
         });

         out.insert(t);
      }

      return out;
   }

   double PlagiarismService::Jaccard(const std::set<std::string>& a, const std::set<std::string>& b)
   {
      if (a.empty() && b.empty())
      {
         return 0.0;
      }

      size_t inter = 0;

      for (const auto& x : a)
      {
         if (b.find(x) != b.end())
         {
            inter++;
         }
      }

      const size_t uni = a.size() + b.size() - inter;

      if (uni == 0)
      {
         return 0.0;
      }

      return static_cast<double>(inter) / static_cast<double>(uni);
   }

   nlohmann::json FinalizationService::Approve(SubmissionItem& submission, int grade, const std::string& feedback)
   {
      submission.grade = grade;
      submission.feedback = feedback;
      submission.approved = true;

      nlohmann::json out;
      out["submissionId"] = submission.id;
      out["approved"] = true;
      out["grade"] = grade;
      out["feedback"] = feedback;
      return out;
   }

   nlohmann::json FinalizationService::Send(SubmissionItem& submission)
   {
      nlohmann::json out;
      out["submissionId"] = submission.id;

      if (!submission.approved)
      {
         out["ok"] = false;
         out["message"] = "Спочатку підтвердіть оцінку";
         return out;
      }

      const std::filesystem::path path = std::filesystem::path(OutboxDir()) / (submission.id + ".txt");
      std::ostringstream body;
      body << "Кому: " << (submission.studentEmail.empty() ? "немає email" : submission.studentEmail) << "\n";
      body << "Тема: Результат перевірки\n";
      body << "Оцінка: " << submission.grade << "\n";
      body << "AI: " << std::fixed << std::setprecision(1) << submission.aiScore << "%\n";
      body << "Плагіат: " << std::fixed << std::setprecision(1) << submission.plagiarismScore << "%\n";
      body << "Фідбек: " << submission.feedback << "\n";

      const bool ok = WriteFileText(path.string(), body.str());
      submission.sent = ok;
      out["ok"] = ok;
      out["message"] = ok ? "Лист підготовлено" : "Помилка підготовки листа";
      out["outboxPath"] = path.string();
      return out;
   }
}
