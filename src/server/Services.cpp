#include "server/Services.h"

#include "server/Utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
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

      std::string UrlDecodePath(std::string text)
      {
         std::string out;
         out.reserve(text.size());

         auto hexValue = [](char c) -> int
         {
            if (c >= '0' && c <= '9')
            {
               return c - '0';
            }
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (c >= 'a' && c <= 'f')
            {
               return 10 + (c - 'a');
            }
            return -1;
         };

         for (size_t i = 0; i < text.size(); ++i)
         {
            const char c = text[i];

            if (c == '%' && i + 2 < text.size())
            {
               const int hi = hexValue(text[i + 1]);
               const int lo = hexValue(text[i + 2]);

               if (hi >= 0 && lo >= 0)
               {
                  out.push_back(static_cast<char>((hi << 4) | lo));
                  i += 2;
                  continue;
               }
            }

            if (c == '+')
            {
               out.push_back(' ');
            }
            else
            {
               out.push_back(c);
            }
         }

         return out;
      }

      std::string ExtractScopedRelativePathFromRepositoryUrl(const std::string& repositoryUrl)
      {
         const std::string host = "github.com/";
         const auto hostPos = repositoryUrl.find(host);

         if (hostPos == std::string::npos)
         {
            return "";
         }

         std::string tail = repositoryUrl.substr(hostPos + host.size());
         const auto suffixPos = tail.find_first_of("?#");
         if (suffixPos != std::string::npos)
         {
            tail = tail.substr(0, suffixPos);
         }

         while (!tail.empty() && tail.back() == '/')
         {
            tail.pop_back();
         }

         size_t markerPos = tail.find("/tree/");
         if (markerPos == std::string::npos)
         {
            markerPos = tail.find("/blob/");
         }

         if (markerPos == std::string::npos)
         {
            return "";
         }

         const size_t branchStart = markerPos + 6;
         const auto branchEnd = tail.find('/', branchStart);

         if (branchEnd == std::string::npos)
         {
            return "";
         }

         std::string scopedPath = tail.substr(branchEnd + 1);
         scopedPath = UrlDecodePath(scopedPath);
         std::replace(scopedPath.begin(), scopedPath.end(), '\\', '/');

         while (!scopedPath.empty() && scopedPath.front() == '/')
         {
            scopedPath.erase(scopedPath.begin());
         }

         return scopedPath;
      }

      bool IsSafeRelativePath(const std::filesystem::path& rel)
      {
         if (rel.empty() || rel.is_absolute())
         {
            return false;
         }

         for (const auto& part : rel)
         {
            const std::string token = part.string();

            if (token.empty() || token == "..")
            {
               return false;
            }
         }

         return true;
      }

      struct RepositoryScopeSelection
      {
         std::filesystem::path repoRoot;
         std::filesystem::path analysisRoot;
         std::optional<std::filesystem::path> singleFile;
         bool hasScopedPath = false;
         bool scopedPathMissing = false;
         std::string scopedRelativePath;
      };

      RepositoryScopeSelection ResolveRepositoryScopeSelection(const SubmissionItem& submission)
      {
         RepositoryScopeSelection selection;
         selection.repoRoot = std::filesystem::path(RepoCacheDir()) / submission.id;
         selection.analysisRoot = selection.repoRoot;

         const std::string scopedRelativePath = ExtractScopedRelativePathFromRepositoryUrl(submission.repositoryUrl);

         if (scopedRelativePath.empty())
         {
            return selection;
         }

         selection.hasScopedPath = true;
         selection.scopedRelativePath = scopedRelativePath;

         const std::filesystem::path rel = std::filesystem::path(scopedRelativePath).lexically_normal();
         if (!IsSafeRelativePath(rel))
         {
            selection.scopedPathMissing = true;
            return selection;
         }

         const std::filesystem::path candidate = (selection.repoRoot / rel).lexically_normal();

         const std::string base = selection.repoRoot.lexically_normal().generic_string();
         const std::string path = candidate.generic_string();
         const std::string baseWithSlash = base.empty() || base.back() == '/' ? base : (base + "/");

         if (!(path == base || path.rfind(baseWithSlash, 0) == 0))
         {
            selection.scopedPathMissing = true;
            return selection;
         }

         if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate))
         {
            selection.analysisRoot = candidate;
            return selection;
         }

         if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate))
         {
            selection.singleFile = candidate;
            return selection;
         }

         selection.scopedPathMissing = true;
         selection.analysisRoot = candidate;
         return selection;
      }

      std::string NormalizeSpaces(const std::string& input)
      {
         std::string out;
         out.reserve(input.size());
         bool lastSpace = true;

         for (unsigned char ch : input)
         {
            if (std::isspace(ch))
            {
               if (!lastSpace)
               {
                  out.push_back(' ');
               }
               lastSpace = true;
            }
            else
            {
               out.push_back(static_cast<char>(ch));
               lastSpace = false;
            }
         }

         if (!out.empty() && out.back() == ' ')
         {
            out.pop_back();
         }

         return out;
      }

      std::string NormalizeSentenceForMatch(const std::string& input)
      {
         std::string normalized;
         normalized.reserve(input.size());

         for (unsigned char ch : input)
         {
            if (ch >= 128)
            {
               normalized.push_back(static_cast<char>(ch));
               continue;
            }

            if (std::isalnum(ch))
            {
               normalized.push_back(static_cast<char>(std::tolower(ch)));
            }
            else
            {
               normalized.push_back(' ');
            }
         }

         return NormalizeSpaces(normalized);
      }

      std::set<std::string> SplitNormalizedSentences(const std::string& text)
      {
         std::set<std::string> out;
         std::string current;
         current.reserve(256);

         auto flush = [&]()
         {
            const std::string sentence = NormalizeSentenceForMatch(current);
            current.clear();

            if (sentence.size() >= 24)
            {
               out.insert(sentence);
            }
         };

         for (char c : text)
         {
            const bool separator = c == '.' || c == '!' || c == '?' || c == ';' || c == '\n' || c == '\r';

            if (separator)
            {
               flush();
               continue;
            }

            current.push_back(c);
         }

         flush();
         return out;
      }

      struct RepositoryPlagiarismSnapshot
      {
         std::set<std::string> tokens;
         std::set<std::string> sentences;
         size_t textFiles = 0;
         size_t totalBytes = 0;
         size_t totalLines = 0;
         std::string scopedPath = "(repository root)";
         bool scopeMissing = false;
      };

      RepositoryPlagiarismSnapshot BuildRepositoryPlagiarismSnapshot(const SubmissionItem& submission)
      {
         RepositoryPlagiarismSnapshot snapshot;
         const RepositoryScopeSelection scope = ResolveRepositoryScopeSelection(submission);
         const std::filesystem::path repoRoot = scope.repoRoot;
         snapshot.scopedPath = scope.hasScopedPath ? scope.scopedRelativePath : "(repository root)";
         snapshot.scopeMissing = scope.hasScopedPath && scope.scopedPathMissing;

         if (!std::filesystem::exists(repoRoot))
         {
            return snapshot;
         }

         if (snapshot.scopeMissing)
         {
            return snapshot;
         }

         std::string combined;
         combined.reserve(64000);
         const size_t maxCombinedChars = 120000;

         auto processFile = [&](const std::filesystem::path& fullPath)
         {
            if (!std::filesystem::is_regular_file(fullPath))
            {
               return;
            }

            const auto rel = std::filesystem::relative(fullPath, repoRoot);
            const std::string relStr = rel.string();

            if (relStr.rfind(".git", 0) == 0)
            {
               return;
            }

            // Plagiarism should run only on text-like files (no binaries).
            if (!IsLikelyCodeOrDocs(rel))
            {
               return;
            }

            std::string content = ReadFileText(fullPath.string());

            if (content.empty())
            {
               return;
            }

            snapshot.textFiles++;
            std::error_code ec;
            const auto fileSize = std::filesystem::file_size(fullPath, ec);
            if (!ec)
            {
               snapshot.totalBytes += static_cast<size_t>(fileSize);
            }

            snapshot.totalLines += static_cast<size_t>(std::count(content.begin(), content.end(), '\n'));
            if (!content.empty())
            {
               snapshot.totalLines++;
            }

            const std::set<std::string> fileSentences = SplitNormalizedSentences(content);
            snapshot.sentences.insert(fileSentences.begin(), fileSentences.end());

            if (combined.size() < maxCombinedChars)
            {
               const size_t remain = maxCombinedChars - combined.size();
               if (content.size() > remain)
               {
                  content.resize(remain);
               }
               combined += '\n';
               combined += content;
            }
         };

         if (scope.singleFile.has_value())
         {
            processFile(scope.singleFile.value());
         }
         else
         {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(scope.analysisRoot))
            {
               processFile(entry.path());
            }
         }

         std::regex split(R"([^A-Za-z0-9_]+)");
         std::sregex_token_iterator it(combined.begin(), combined.end(), split, -1);
         std::sregex_token_iterator end;

         for (; it != end; ++it)
         {
            std::string token = it->str();
            if (token.empty())
            {
               continue;
            }

            std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c)
            {
               return static_cast<char>(std::tolower(c));
            });

            snapshot.tokens.insert(token);
         }

         return snapshot;
      }

      struct DatasetSnapshot
      {
         std::string raw;
         std::set<std::string> tokens;
         std::set<std::string> sentences;
         size_t chars = 0;
         size_t lines = 0;
      };

      DatasetSnapshot BuildDatasetSnapshot(const std::string& row)
      {
         DatasetSnapshot out;
         out.raw = row;
         out.chars = row.size();
         out.lines = static_cast<size_t>(std::count(row.begin(), row.end(), '\n')) + 1;
         out.sentences = SplitNormalizedSentences(row);

         std::regex split(R"([^A-Za-z0-9_]+)");
         std::sregex_token_iterator it(row.begin(), row.end(), split, -1);
         std::sregex_token_iterator end;

         for (; it != end; ++it)
         {
            std::string token = it->str();

            if (token.empty())
            {
               continue;
            }

            std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c)
            {
               return static_cast<char>(std::tolower(c));
            });

            out.tokens.insert(token);
         }

         return out;
      }

      size_t IntersectionCount(const std::set<std::string>& a, const std::set<std::string>& b)
      {
         size_t count = 0;

         for (const auto& value : a)
         {
            if (b.find(value) != b.end())
            {
               count++;
            }
         }

         return count;
      }

      double RelativeSimilarity(size_t a, size_t b)
      {
         const double maxV = static_cast<double>(std::max(a, b));

         if (maxV <= 0.0)
         {
            return 0.0;
         }

         const double diff = static_cast<double>(a > b ? (a - b) : (b - a));
         const double score = 1.0 - (diff / maxV);
         return score < 0.0 ? 0.0 : score;
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
         const RepositoryScopeSelection scope = ResolveRepositoryScopeSelection(submission);
         const std::filesystem::path repoRoot = scope.repoRoot;

         if (!std::filesystem::exists(repoRoot))
         {
            return "Repository cache is missing. Ask user to press pull-repos before AI review.";
         }

         if (scope.hasScopedPath && scope.scopedPathMissing)
         {
            return "Scoped task path from submission URL was not found in repository cache: " + scope.scopedRelativePath +
               ". Verify that student submitted a valid /tree/<branch>/<folder> link and re-pull repository.";
         }

         std::vector<std::filesystem::path> readmes;
         std::vector<std::filesystem::path> others;
         std::vector<std::string> metadataRows;
         size_t totalFiles = 0;
         size_t textFiles = 0;
         size_t nonTextFiles = 0;
         unsigned long long totalBytes = 0;

         auto processFile = [&](const std::filesystem::path& fullPath)
         {
            if (!std::filesystem::is_regular_file(fullPath))
            {
               return;
            }

            const auto rel = std::filesystem::relative(fullPath, repoRoot);

            if (rel.empty())
            {
               return;
            }

            const std::string relStr = rel.string();

            if (relStr.rfind(".git", 0) == 0)
            {
               return;
            }

            totalFiles++;
            std::error_code sizeEc;
            const auto fileSize = std::filesystem::file_size(fullPath, sizeEc);
            if (!sizeEc)
            {
               totalBytes += static_cast<unsigned long long>(fileSize);
            }

            std::ostringstream meta;
            meta << relStr << " | bytes=" << (sizeEc ? 0 : static_cast<unsigned long long>(fileSize));
            metadataRows.push_back(meta.str());

            if (!IsLikelyCodeOrDocs(rel))
            {
               nonTextFiles++;
               return;
            }

            textFiles++;

            if (IsReadmeFile(rel))
            {
               readmes.push_back(rel);
            }
            else
            {
               others.push_back(rel);
            }
         };

         if (scope.singleFile.has_value())
         {
            processFile(scope.singleFile.value());
         }
         else
         {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(scope.analysisRoot))
            {
               processFile(entry.path());
            }
         }

         std::sort(readmes.begin(), readmes.end());
         std::sort(others.begin(), others.end());

         std::ostringstream out;
         out << "Repository URL: " << submission.repositoryUrl << "\n";
         out << "Student: " << submission.studentName << "\n\n";
         out << "Task scope: " << (scope.hasScopedPath ? scope.scopedRelativePath : "(repository root)") << "\n\n";
         out << "Repository metadata:\n";
         out << "- Total files: " << totalFiles << "\n";
         out << "- Text/code/docs files: " << textFiles << "\n";
         out << "- Other file types: " << nonTextFiles << "\n";
         out << "- Total bytes: " << totalBytes << "\n\n";

         const size_t maxMetadataRows = 60;
         out << "File metadata snapshot:\n";
         for (size_t i = 0; i < metadataRows.size() && i < maxMetadataRows; ++i)
         {
            out << "- " << metadataRows[i] << "\n";
         }
         out << "\n";

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

      std::string Trim(std::string value)
      {
         const auto begin = value.find_first_not_of(" \t\r\n");

         if (begin == std::string::npos)
         {
            return "";
         }

         const auto end = value.find_last_not_of(" \t\r\n");
         return value.substr(begin, end - begin + 1);
      }

      std::string ResolveSettingsFilePath()
      {
         const std::filesystem::path direct = std::filesystem::current_path() / "settings" / "app_settings.cfg";

         if (std::filesystem::exists(direct))
         {
            return direct.string();
         }

         const std::filesystem::path parent = std::filesystem::current_path().parent_path() / "settings" / "app_settings.cfg";

         if (std::filesystem::exists(parent))
         {
            return parent.string();
         }

         return direct.string();
      }

      std::string BuildOllamaBaseUrl()
      {
         const std::string defaultUrl = "http://localhost:11434";
         std::ifstream input(ResolveSettingsFilePath());

         if (!input.is_open())
         {
            return defaultUrl;
         }

         std::string line;

         while (std::getline(input, line))
         {
            const std::string trimmed = Trim(line);

            if (trimmed.empty() || trimmed[0] == '#')
            {
               continue;
            }

            const auto pos = trimmed.find('=');

            if (pos == std::string::npos)
            {
               continue;
            }

            const std::string key = Trim(trimmed.substr(0, pos));

            if (key != "ollama.baseUrl")
            {
               continue;
            }

            std::string value = Trim(trimmed.substr(pos + 1));

            if (value.empty())
            {
               return defaultUrl;
            }

            if (!value.empty() && value.back() == '/')
            {
               value.pop_back();
            }

            return value;
         }

         return defaultUrl;
      }

      std::string BuildOllamaModel()
      {
         const std::string defaultModel = "aichecker-llama3.2-3b:latest";
         std::ifstream input(ResolveSettingsFilePath());

         if (!input.is_open())
         {
            return defaultModel;
         }

         std::string line;

         while (std::getline(input, line))
         {
            const std::string trimmed = Trim(line);

            if (trimmed.empty() || trimmed[0] == '#')
            {
               continue;
            }

            const auto pos = trimmed.find('=');

            if (pos == std::string::npos)
            {
               continue;
            }

            const std::string key = Trim(trimmed.substr(0, pos));

            if (key != "ollama.model")
            {
               continue;
            }

            const std::string value = Trim(trimmed.substr(pos + 1));

            if (!value.empty())
            {
               return value;
            }

            return defaultModel;
         }

         return defaultModel;
      }

      std::string ResolveOllamaModelfilePath()
      {
         const std::filesystem::path direct = std::filesystem::current_path() / "settings" / "ollama" / "Modelfile.aichecker";

         if (std::filesystem::exists(direct))
         {
            return direct.string();
         }

         const std::filesystem::path parent = std::filesystem::current_path().parent_path() / "settings" / "ollama" / "Modelfile.aichecker";

         if (std::filesystem::exists(parent))
         {
            return parent.string();
         }

         return "";
      }

      bool EnsureOllamaModelAvailable(const std::string& modelName, std::string& outError)
      {
         outError.clear();

         if (modelName.empty())
         {
            outError = "Назва моделі порожня.";
            return false;
         }

#ifdef _WIN32
         static std::mutex guard;
         static std::unordered_set<std::string> ensuredModels;
         std::lock_guard<std::mutex> lock(guard);

         if (ensuredModels.find(modelName) != ensuredModels.end())
         {
            return true;
         }

         const std::string showCommand = "ollama show \"" + modelName + "\" >nul 2>nul";
         const int showResult = std::system(showCommand.c_str());

         if (showResult == 0)
         {
            ensuredModels.insert(modelName);
            return true;
         }

         const std::string modelfilePath = ResolveOllamaModelfilePath();

         if (modelfilePath.empty())
         {
            outError = "Модель не знайдена в Ollama і не знайдено settings/ollama/Modelfile.aichecker.";
            return false;
         }

         const std::string createCommand = "ollama create \"" + modelName + "\" -f \"" + modelfilePath + "\"";
         const int createResult = std::system(createCommand.c_str());

         if (createResult != 0)
         {
            outError = "Не вдалося створити модель через 'ollama create'. Перевірте, що Ollama CLI встановлено і сервіс запущено.";
            return false;
         }

         ensuredModels.insert(modelName);
         return true;
#else
         (void)modelName;
         outError = "Авто-створення моделі через Modelfile підтримується лише на Windows у цьому білді.";
         return false;
#endif
      }

      std::string BuildSystemPromptPath()
      {
         const std::filesystem::path direct = std::filesystem::current_path() / "settings" / "ollama" / "SYSTEM_PROMPT.txt";

         if (std::filesystem::exists(direct))
         {
            return direct.string();
         }

         const std::filesystem::path parent = std::filesystem::current_path().parent_path() / "settings" / "ollama" / "SYSTEM_PROMPT.txt";

         if (std::filesystem::exists(parent))
         {
            return parent.string();
         }

         return direct.string();
      }

      std::string ResolveOllamaPromptsDirPath()
      {
         const std::filesystem::path direct = std::filesystem::current_path() / "settings" / "ollama" / "prompts";

         if (std::filesystem::exists(direct) && std::filesystem::is_directory(direct))
         {
            return direct.string();
         }

         const std::filesystem::path parent = std::filesystem::current_path().parent_path() / "settings" / "ollama" / "prompts";

         if (std::filesystem::exists(parent) && std::filesystem::is_directory(parent))
         {
            return parent.string();
         }

         return "";
      }

      std::vector<std::string> LoadOllamaPromptTemplates()
      {
         std::vector<std::string> templates;
         const std::string promptsDir = ResolveOllamaPromptsDirPath();

         if (promptsDir.empty())
         {
            return templates;
         }

         std::vector<std::filesystem::path> files;

         for (const auto& entry : std::filesystem::directory_iterator(promptsDir))
         {
            if (!entry.is_regular_file())
            {
               continue;
            }

            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
            {
               return static_cast<char>(std::tolower(c));
            });

            if (ext == ".txt")
            {
               files.push_back(entry.path());
            }
         }

         std::sort(files.begin(), files.end());

         for (const auto& file : files)
         {
            std::string content = ReadFileText(file.string());
            content = Trim(content);

            if (!content.empty())
            {
               templates.push_back(content);
            }
         }

         return templates;
      }

      std::string BuildPromptTemplatesBundle(const std::vector<std::string>& templates)
      {
         if (templates.empty())
         {
            return "";
         }

         std::ostringstream out;
         out << "Виконай усі додаткові інструкції нижче. Вони рівнозначні і обов'язкові:\n\n";

         for (size_t i = 0; i < templates.size(); ++i)
         {
            out << "[ПРОМПТ " << (i + 1) << "]\n";
            out << ToPromptSafe(templates[i], 1800) << "\n\n";
         }

         out << "Після виконання всіх інструкцій поверни лише JSON за схемою AIChecker.\n\n";
         return out.str();
      }

      std::string ReplaceAll(std::string text, const std::string& from, const std::string& to)
      {
         if (from.empty())
         {
            return text;
         }

         size_t pos = 0;

         while ((pos = text.find(from, pos)) != std::string::npos)
         {
            text.replace(pos, from.size(), to);
            pos += to.size();
         }

         return text;
      }

      std::string AlternateLocalhostBaseUrl(const std::string& baseUrl)
      {
         if (baseUrl.find("localhost") != std::string::npos)
         {
            return ReplaceAll(baseUrl, "localhost", "127.0.0.1");
         }

         if (baseUrl.find("127.0.0.1") != std::string::npos)
         {
            return ReplaceAll(baseUrl, "127.0.0.1", "localhost");
         }

         return "";
      }

      std::string NormalizeCloneUrl(std::string url)
      {
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

      bool TryParseAnyJsonObject(const std::string& text, nlohmann::json& out)
      {
         const std::string trimmed = Trim(text);

         if (trimmed.empty())
         {
            return false;
         }

         try
         {
            out = nlohmann::json::parse(trimmed);
            if (out.is_object())
            {
               return true;
            }
         }
         catch (...)
         {
         }

         // Try fenced code blocks like ```json ... ```.
         size_t fenceStart = 0;
         while ((fenceStart = text.find("```", fenceStart)) != std::string::npos)
         {
            const size_t headerEnd = text.find('\n', fenceStart + 3);
            if (headerEnd == std::string::npos)
            {
               break;
            }

            const size_t fenceEnd = text.find("```", headerEnd + 1);
            if (fenceEnd == std::string::npos)
            {
               break;
            }

            std::string block = Trim(text.substr(headerEnd + 1, fenceEnd - (headerEnd + 1)));
            try
            {
               out = nlohmann::json::parse(block);
               if (out.is_object())
               {
                  return true;
               }
            }
            catch (...)
            {
            }

            fenceStart = fenceEnd + 3;
         }

         // Try first balanced JSON object from mixed text.
         const size_t firstBrace = text.find('{');
         if (firstBrace == std::string::npos)
         {
            return false;
         }

         int depth = 0;
         bool inString = false;
         bool escaped = false;
         size_t objStart = std::string::npos;

         for (size_t i = firstBrace; i < text.size(); ++i)
         {
            const char c = text[i];

            if (inString)
            {
               if (escaped)
               {
                  escaped = false;
               }
               else if (c == '\\')
               {
                  escaped = true;
               }
               else if (c == '"')
               {
                  inString = false;
               }
               continue;
            }

            if (c == '"')
            {
               inString = true;
               continue;
            }

            if (c == '{')
            {
               if (depth == 0)
               {
                  objStart = i;
               }
               depth++;
            }
            else if (c == '}')
            {
               depth--;
               if (depth == 0 && objStart != std::string::npos)
               {
                  const std::string candidate = text.substr(objStart, i - objStart + 1);
                  try
                  {
                     out = nlohmann::json::parse(candidate);
                     if (out.is_object())
                     {
                        return true;
                     }
                  }
                  catch (...)
                  {
                  }
               }

               if (depth < 0)
               {
                  depth = 0;
                  objStart = std::string::npos;
               }
            }
         }

         return false;
      }

      bool TryGetScore(const nlohmann::json& value, const std::string& sourceText, double& score)
      {
         if (value.contains("score"))
         {
            const auto& s = value["score"];

            if (s.is_number())
            {
               score = s.get<double>();
               return true;
            }

            if (s.is_string())
            {
               try
               {
                  score = std::stod(s.get<std::string>());
                  return true;
               }
               catch (...)
               {
               }
            }
         }

         static const std::regex rx(R"((-?\d+(?:\.\d+)?))");
         std::sregex_iterator it(sourceText.begin(), sourceText.end(), rx);
         std::sregex_iterator end;

         if (it == end)
         {
            return false;
         }

         try
         {
            score = std::stod((*it)[1].str());
            return true;
         }
         catch (...)
         {
            return false;
         }
      }

      bool ValidateAndNormalizeAiJson(const std::string& text, nlohmann::json& out)
      {
         nlohmann::json raw;
         if (!TryParseAnyJsonObject(text, raw))
         {
            return false;
         }

         if (!raw.is_object())
         {
            return false;
         }

         double score = 50.0;
         if (!TryGetScore(raw, text, score))
         {
            return false;
         }

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
         normalized["verdict"] = score > 70.0 ? "AI" : (score < 40.0 ? "HUMAN" : "UNCERTAIN");

         auto toStringArray = [](const nlohmann::json& v)
         {
            nlohmann::json arr = nlohmann::json::array();

            if (v.is_array())
            {
               for (const auto& item : v)
               {
                  if (item.is_string())
                  {
                     arr.push_back(item.get<std::string>());
                  }
                  else if (item.is_object())
                  {
                     // Allow structured issue objects and convert them to readable strings.
                     const std::string description = Trim(item.value("description", ""));
                     const std::string file = Trim(item.value("file", ""));
                     const std::string evidence = Trim(item.value("evidence", ""));

                     std::string line;

                     if (!description.empty())
                     {
                        line += description;
                     }

                     if (!file.empty())
                     {
                        if (!line.empty())
                        {
                           line += " ";
                        }
                        line += "[file: " + file + "]";
                     }

                     if (!evidence.empty())
                     {
                        if (!line.empty())
                        {
                           line += " | ";
                        }
                        line += "evidence: " + evidence;
                     }

                     if (!line.empty())
                     {
                        arr.push_back(line);
                     }
                  }
                  else if (item.is_primitive())
                  {
                     arr.push_back(item.dump());
                  }
               }
               return arr;
            }

            if (v.is_string())
            {
               arr.push_back(v.get<std::string>());
            }

            return arr;
         };

         normalized["thinking"] = raw.contains("thinking") && raw["thinking"].is_string()
            ? raw["thinking"].get<std::string>()
            : "";
         normalized["indicators"] = raw.contains("indicators") ? toStringArray(raw["indicators"]) : nlohmann::json::array();
         normalized["strong_parts"] = raw.contains("strong_parts") ? toStringArray(raw["strong_parts"]) : nlohmann::json::array();
         normalized["issues"] = raw.contains("issues") ? toStringArray(raw["issues"]) : nlohmann::json::array();
         normalized["recommendations"] = raw.contains("recommendations") ? toStringArray(raw["recommendations"]) : nlohmann::json::array();
         normalized["email_comment"] = raw.contains("email_comment") && raw["email_comment"].is_string()
            ? raw["email_comment"].get<std::string>()
            : "";
         out = normalized;
         return true;
      }

      std::string UpperAscii(std::string value)
      {
         std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
         {
            return static_cast<char>(std::toupper(c));
         });
         return value;
      }

      std::string LowerAscii(std::string value)
      {
         std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
         {
            return static_cast<char>(std::tolower(c));
         });
         return value;
      }

      bool IsPlaceholderText(const std::string& text)
      {
         const std::string normalized = LowerAscii(Trim(text));

         if (normalized.empty())
         {
            return true;
         }

         static const std::vector<std::string> placeholders = {
            "висновок",
            "нема",
            "немає",
            "відсутньо",
            "без коментаря",
            "n/a",
            "none",
            "no data",
            "-"
         };

         for (const auto& item : placeholders)
         {
            if (normalized == item)
            {
               return true;
            }

            if (normalized.rfind(item + ":", 0) == 0)
            {
               return true;
            }
         }

         if (normalized.size() < 5)
         {
            return true;
         }

         return false;
      }

      size_t CountMeaningfulArrayItems(const nlohmann::json& arr)
      {
         if (!arr.is_array())
         {
            return 0;
         }

         size_t count = 0;

         for (const auto& item : arr)
         {
            if (!item.is_string())
            {
               continue;
            }

            const std::string text = Trim(item.get<std::string>());
            if (text.size() < 16)
            {
               continue;
            }

            if (IsPlaceholderText(text))
            {
               continue;
            }

            count++;
         }

         return count;
      }

      bool IsUkrainianOrAllowedJson(const nlohmann::json& value)
      {
         if (!value.is_object())
         {
            return false;
         }

         const std::vector<std::string> stringFields = { "verdict", "thinking", "email_comment" };
         const std::vector<std::string> arrayFields = { "indicators", "strong_parts", "issues", "recommendations" };

         for (const auto& field : stringFields)
         {
            if (!value.contains(field) || !value[field].is_string())
            {
               return false;
            }
         }

         for (const auto& field : arrayFields)
         {
            if (!value.contains(field) || !value[field].is_array())
            {
               return false;
            }

            for (const auto& item : value[field])
            {
               if (!item.is_string())
               {
                  return false;
               }
            }
         }

         const std::string verdict = UpperAscii(value.value("verdict", ""));
         if (verdict != "AI" && verdict != "HUMAN" && verdict != "UNCERTAIN")
         {
            return false;
         }

         return true;
      }

      bool HasFileEvidenceInJson(const nlohmann::json& value)
      {
         static const std::regex fileRef(R"(([\w\-.\/]+\.(c|cc|cpp|cxx|h|hh|hpp|hxx|py|js|ts|tsx|jsx|java|cs|go|rs|kt|swift|php|rb|sh|ps1|sql|html|css|json|yaml|yml|toml|xml|md|txt)))", std::regex::icase);

         auto containsFileRef = [&](const std::string& text)
         {
            return std::regex_search(text, fileRef);
         };

         if (value.contains("thinking") && value["thinking"].is_string() && containsFileRef(value["thinking"].get<std::string>()))
         {
            return true;
         }

         const std::vector<std::string> arrayFields = { "indicators", "strong_parts", "issues", "recommendations" };

         for (const auto& field : arrayFields)
         {
            if (!value.contains(field) || !value[field].is_array())
            {
               continue;
            }

            for (const auto& item : value[field])
            {
               if (item.is_string() && containsFileRef(item.get<std::string>()))
               {
                  return true;
               }
            }
         }

         return false;
      }

      bool IsLowQualityAiJson(const nlohmann::json& value, bool requireFileEvidence, std::string* reason = nullptr)
      {
         if (reason != nullptr)
         {
            reason->clear();
         }

         if (!value.is_object())
         {
            if (reason != nullptr)
            {
               *reason = "звіт не є JSON-об'єктом";
            }
            return true;
         }

         const std::string thinking = value.value("thinking", "");
         const std::string emailComment = value.value("email_comment", "");

         if (Trim(thinking).size() < 24 || IsPlaceholderText(thinking))
         {
            if (reason != nullptr)
            {
               *reason = "поле thinking занадто коротке або шаблонне";
            }
            return true;
         }

         if (Trim(emailComment).size() < 12 || IsPlaceholderText(emailComment))
         {
            if (reason != nullptr)
            {
               *reason = "поле email_comment занадто коротке або шаблонне";
            }
            return true;
         }

         const size_t indicators = CountMeaningfulArrayItems(value.value("indicators", nlohmann::json::array()));
         const size_t issues = CountMeaningfulArrayItems(value.value("issues", nlohmann::json::array()));
         const size_t recommendations = CountMeaningfulArrayItems(value.value("recommendations", nlohmann::json::array()));

         if (indicators == 0 || issues == 0 || recommendations == 0)
         {
            if (reason != nullptr)
            {
               *reason = "індикатори/проблеми/рекомендації не містять змістовних пунктів";
            }
            return true;
         }

         if (requireFileEvidence && !HasFileEvidenceInJson(value))
         {
            if (reason != nullptr)
            {
               *reason = "немає посилань на конкретні файли репозиторію";
            }
            return true;
         }

         return false;
      }

      struct RepositoryFallbackFacts
      {
         bool cacheExists = false;
         bool hasReadme = false;
         size_t totalFiles = 0;
         size_t textFiles = 0;
         size_t cppLikeFiles = 0;
         bool scopeMissing = false;
         std::string scopedPath = "(repository root)";
         std::vector<std::string> sampleFiles;
      };

      RepositoryFallbackFacts CollectRepositoryFallbackFacts(const SubmissionItem& submission)
      {
         RepositoryFallbackFacts facts;
         const RepositoryScopeSelection scope = ResolveRepositoryScopeSelection(submission);
         const std::filesystem::path repoRoot = scope.repoRoot;
         facts.scopedPath = scope.hasScopedPath ? scope.scopedRelativePath : "(repository root)";
         facts.scopeMissing = scope.hasScopedPath && scope.scopedPathMissing;

         if (!std::filesystem::exists(repoRoot))
         {
            return facts;
         }

         facts.cacheExists = true;

         if (facts.scopeMissing)
         {
            return facts;
         }

         auto processFile = [&](const std::filesystem::path& fullPath)
         {
            if (!std::filesystem::is_regular_file(fullPath))
            {
               return;
            }

            const auto rel = std::filesystem::relative(fullPath, repoRoot);
            const std::string relStr = rel.string();

            if (relStr.rfind(".git", 0) == 0)
            {
               return;
            }

            facts.totalFiles++;

            if (!IsLikelyCodeOrDocs(rel))
            {
               return;
            }

            facts.textFiles++;

            std::string ext = rel.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
            {
               return static_cast<char>(std::tolower(c));
            });

            if (ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx")
            {
               facts.cppLikeFiles++;
            }

            if (IsReadmeFile(rel))
            {
               facts.hasReadme = true;
            }

            if (facts.sampleFiles.size() < 5)
            {
               facts.sampleFiles.push_back(relStr);
            }
         };

         if (scope.singleFile.has_value())
         {
            processFile(scope.singleFile.value());
         }
         else
         {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(scope.analysisRoot))
            {
               processFile(entry.path());
            }
         }

         std::sort(facts.sampleFiles.begin(), facts.sampleFiles.end());
         return facts;
      }

      std::string BuildSampleFilesText(const std::vector<std::string>& files)
      {
         if (files.empty())
         {
            return "немає доступних текстових файлів";
         }

         std::ostringstream out;
         for (size_t i = 0; i < files.size(); ++i)
         {
            if (i > 0)
            {
               out << ", ";
            }
            out << files[i];
         }
         return out.str();
      }

      std::string VerdictFromScore(double score)
      {
         if (score > 70.0)
         {
            return "AI";
         }

         if (score < 40.0)
         {
            return "HUMAN";
         }

         return "UNCERTAIN";
      }

      nlohmann::json BuildDeterministicAiReport(double score, const std::string& modelReason, bool hasRepositoryContext, const SubmissionItem& submission)
      {
         nlohmann::json out;
         out["score"] = score;
         out["verdict"] = VerdictFromScore(score);
         const RepositoryFallbackFacts facts = CollectRepositoryFallbackFacts(submission);
         const std::string sampleFiles = BuildSampleFilesText(facts.sampleFiles);

         if (hasRepositoryContext)
         {
            out["thinking"] = "Через технічну помилку моделі сформовано резервний звіт на базі структури репозиторію.";
            out["indicators"] = nlohmann::json::array({
               "Цільова область перевірки: " + facts.scopedPath + ".",
               "Локально проаналізовано приблизно " + std::to_string(facts.textFiles) + " текстових файлів із " + std::to_string(facts.totalFiles) + " загальних.",
               "Приклади файлів для ручної верифікації: " + sampleFiles + "."
            });
            out["issues"] = nlohmann::json::array({
               "AI-модель була недоступна або повернула технічну помилку, тому повноцінний аналіз не виконано."
            });
            if (facts.scopeMissing)
            {
               out["issues"].push_back("У submission URL вказано шлях '" + facts.scopedPath + "', але його не знайдено в локальній копії репозиторію.");
            }
            if (!facts.hasReadme)
            {
               out["issues"].push_back("У репозиторії не виявлено README-файл, що ускладнює контекстну оцінку роботи.");
            }
            out["recommendations"] = nlohmann::json::array({
               "Спочатку перевірити вручну файли: " + sampleFiles + ".",
               "Після стабілізації моделі повторити AI-перевірку для цього ж submission."
            });
            out["strong_parts"] = nlohmann::json::array({
               "Репозиторій доступний локально, тому базові структурні сигнали вдалося зібрати навіть у fallback-режимі."
            });
            out["email_comment"] = "Через технічну помилку AI-перевірки сформовано резервний висновок; виконайте коротку ручну перевірку вказаних файлів.";
         }
         else
         {
            out["thinking"] = "Локальний кеш репозиторію відсутній, тому AI-оцінка сформована у резервному режимі.";
            out["indicators"] = nlohmann::json::array({
               "Репозиторій не підтягнуто локально перед AI-аналізом."
            });
            out["issues"] = nlohmann::json::array({
               "Недостатньо даних для повноцінного аналізу вмісту файлів."
            });
            out["recommendations"] = nlohmann::json::array({
               "Спочатку виконайте отримання репозиторію, потім повторіть AI-перевірку."
            });
            out["strong_parts"] = nlohmann::json::array();
            out["email_comment"] = "AI-оцінка неповна: перед повторною перевіркою потрібно отримати репозиторій студента.";
         }

         if (!modelReason.empty())
         {
            out["issues"].push_back("Технічна причина fallback: " + modelReason);
         }

         return out;
      }

      nlohmann::json BuildRecoveredAiReport(double score, const std::string& modelText, bool hasRepositoryContext, const SubmissionItem& submission)
      {
         nlohmann::json out;
         const RepositoryFallbackFacts facts = CollectRepositoryFallbackFacts(submission);
         const std::string sampleFiles = BuildSampleFilesText(facts.sampleFiles);

         // Recovered mode means model output quality is insufficient for confident scoring.
         score = 50.0;

         out["score"] = score;
         out["verdict"] = VerdictFromScore(score);
         (void)modelText;
         out["thinking"] = "Модель повернула низькоякісну відповідь; сформовано відновлений звіт із фактичних даних репозиторію.";

         out["strong_parts"] = nlohmann::json::array({
            "Зібрано базові сигнали репозиторію: текстових файлів " + std::to_string(facts.textFiles) + ", C/C++ файлів " + std::to_string(facts.cppLikeFiles) + "."
         });
         out["indicators"] = nlohmann::json::array({
            "Цільова область перевірки: " + facts.scopedPath + ".",
            "Модель не дала якісний структурований звіт, тому використано безпечне відновлення результату.",
            "Для ручної перевірки доступні файли: " + sampleFiles + "."
         });
         out["issues"] = nlohmann::json::array({
            "Модель не повернула валідний JSON за контрактом AIChecker."
         });
         if (facts.scopeMissing)
         {
            out["issues"].push_back("У submission URL вказано шлях '" + facts.scopedPath + "', але його не знайдено в локальній копії репозиторію.");
         }
         if (hasRepositoryContext)
         {
            out["issues"].push_back("Частина деталей могла бути втрачена під час відновлення відповіді.");
         }
         out["recommendations"] = nlohmann::json::array({
            "Переглянути вручну щонайменше такі файли: " + sampleFiles + ".",
            "Підсилити prompt-шаблони прикладами очікуваних змістовних пунктів із file evidence."
         });
         out["email_comment"] = "AI-звіт відновлено у безпечному режимі; для фінального рішення перегляньте ключові файли репозиторію вручну.";
         return out;
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

   std::string WinHttpErrorText(DWORD code)
   {
      if (code == 0)
      {
         return "none";
      }

      switch (code)
      {
      case ERROR_WINHTTP_TIMEOUT:
         return "ERROR_WINHTTP_TIMEOUT";
      case ERROR_WINHTTP_CANNOT_CONNECT:
         return "ERROR_WINHTTP_CANNOT_CONNECT";
      case ERROR_WINHTTP_CONNECTION_ERROR:
         return "ERROR_WINHTTP_CONNECTION_ERROR";
      case ERROR_WINHTTP_NAME_NOT_RESOLVED:
         return "ERROR_WINHTTP_NAME_NOT_RESOLVED";
      case ERROR_WINHTTP_SECURE_FAILURE:
         return "ERROR_WINHTTP_SECURE_FAILURE";
      default:
         return "WINHTTP_ERROR_" + std::to_string(static_cast<unsigned long long>(code));
      }
   }

   bool PostJson(const std::string& url,
      const nlohmann::json& request,
      std::string& responseBody,
      int& statusCode,
      std::string* errorMessage = nullptr)
   {
      if (errorMessage != nullptr)
      {
         errorMessage->clear();
      }

      statusCode = 0;
      responseBody.clear();

      static const std::regex re(R"(^(https?)://([^/:]+)(?::(\d+))?(.*)$)", std::regex::icase);
      std::smatch m;

      if (!std::regex_match(url, m, re))
      {
         if (errorMessage != nullptr)
         {
            *errorMessage = "Invalid URL: " + url;
         }

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

      HINTERNET hSession = WinHttpOpen(L"AICheckerWeb/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

      if (hSession == nullptr)
      {
         const DWORD err = GetLastError();

         if (errorMessage != nullptr)
         {
            *errorMessage = "WinHttpOpen failed: " + WinHttpErrorText(err);
         }

         return false;
      }

      WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 600000);

      HINTERNET hConnect = WinHttpConnect(hSession, ToWide(host).c_str(), static_cast<INTERNET_PORT>(port), 0);

      if (hConnect == nullptr)
      {
         const DWORD err = GetLastError();

         if (errorMessage != nullptr)
         {
            *errorMessage = "WinHttpConnect failed: " + WinHttpErrorText(err) + " host=" + host + " port=" + std::to_string(port);
         }

         WinHttpCloseHandle(hSession);
         return false;
      }

      HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", ToWide(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

      if (hRequest == nullptr)
      {
         const DWORD err = GetLastError();

         if (errorMessage != nullptr)
         {
            *errorMessage = "WinHttpOpenRequest failed: " + WinHttpErrorText(err);
         }

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
         const DWORD err = GetLastError();

         if (errorMessage != nullptr)
         {
            *errorMessage = "WinHttpSendRequest/ReceiveResponse failed: " + WinHttpErrorText(err);
         }

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
         if (s == nullptr)
         {
            continue;
         }

         const std::filesystem::path localPath = std::filesystem::path(RepoCacheDir()) / s->id;
         std::filesystem::create_directories(localPath.parent_path());

         nlohmann::json r;
         r["submissionId"] = s->id;
         r["repositoryUrl"] = s->repositoryUrl;
         r["localPath"] = localPath.string();

         if (Trim(s->repositoryUrl).empty())
         {
            r["ok"] = false;
            r["skipped"] = true;
            r["message"] = "Репозиторій не вказано в Classroom submission.";
            rows.push_back(r);
            continue;
         }

         std::ostringstream cmd;

         if (std::filesystem::exists(localPath / ".git"))
         {
            cmd << "git -C \"" << localPath.string() << "\" pull --ff-only";
            r["action"] = "pull";
         }
         else
         {
            cmd << "git clone --depth 1 \"" << NormalizeCloneUrl(s->repositoryUrl) << "\" \"" << localPath.string() << "\"";
            r["action"] = "clone";
         }

         const int rc = std::system(cmd.str().c_str());
         r["ok"] = (rc == 0);
         r["skipped"] = false;
         r["message"] = (rc == 0)
            ? "Репозиторій успішно отримано."
            : "Не вдалося отримати репозиторій (git clone/pull помилка).";
         rows.push_back(r);
      }

      return rows;
   }

   nlohmann::json AiReviewService::Analyze(SubmissionItem& submission, const std::string& model, double temperature)
   {
      double score = 50.0;
      std::string thinking = "Fallback-режим";
      const std::string context = BuildRepositoryContext(submission);
      const std::string effectiveModel = model.empty() ? BuildOllamaModel() : model;
      const std::vector<std::string> promptTemplates = LoadOllamaPromptTemplates();
      const std::string promptTemplatesBundle = BuildPromptTemplatesBundle(promptTemplates);

#ifdef _WIN32
      nlohmann::json request;
      request["model"] = effectiveModel;
      request["stream"] = false;
      request["format"] = {
         {"type", "object"},
         {"properties", {
            {"score", {{"type", "number"}}},
            {"verdict", {{"type", "string"}}},
            {"thinking", {{"type", "string"}}},
            {"indicators", {{"type", "array"}, {"items", {{"type", "string"}}}}},
            {"strong_parts", {{"type", "array"}, {"items", {{"type", "string"}}}}},
            {"issues", {{"type", "array"}, {"items", {{"type", "string"}}}}},
            {"recommendations", {{"type", "array"}, {"items", {{"type", "string"}}}}},
            {"email_comment", {{"type", "string"}}}
         }},
         {"required", nlohmann::json::array({
            "score",
            "verdict",
            "thinking",
            "indicators",
            "strong_parts",
            "issues",
            "recommendations",
            "email_comment"
         })},
         {"additionalProperties", false}
      };
      const std::string customSystemPrompt = ReadFileText(BuildSystemPromptPath());
      const bool usePromptTemplatesMode = !promptTemplates.empty();

      request["system"] = usePromptTemplatesMode
         ? "You are AIChecker evaluator. Return only valid JSON, no markdown and no extra text."
         : (customSystemPrompt.empty()
            ? "You are AIChecker evaluator. Return only valid JSON, no markdown and no extra text. Be strict and evidence-based."
            : customSystemPrompt);

      request["options"] = {
         {"temperature", temperature},
         {"top_k", 40},
         {"top_p", 0.6},
         {"min_p", 0.05},
         {"seed", 42},
         {"repeat_penalty", 1.1},
         {"repeat_last_n", 256},
         {"num_ctx", 8192},
         {"num_predict", 320}
      };
      const std::string basePrompt =
         "Проаналізуй контекст студентського репозиторію та поверни СУВОРО лише валідний JSON за схемою: "
         "{\"score\": number, \"verdict\": string, \"thinking\": string, \"indicators\": [string], "
         "\"strong_parts\": [string], \"issues\": [string], \"recommendations\": [string], \"email_comment\": string}. "
         "Усі рядкові поля мають бути українською мовою (дозволені технічні токени: E2E, Git Bash, SOLID, API, JSON, README, GitHub, C++, CMake, Ollama). "
         "score у межах 0..100, де більший бал = вища ймовірність AI-генерації. "
         "thinking має бути РЯДКОМ, а не масивом. Без додаткових полів. "
         "Вимога до якості: не пиши загальні фрази на кшталт 'перевірити код' або 'потрібна додаткова інформація'. "
         "У indicators/issues/recommendations наведи конкретні технічні спостереження з прив'язкою до файлів (вкажи назви файлів, наприклад src/main.cpp або README.md). "
         "Якщо файлів майже немає, прямо зазнач це як причину невизначеності, без шаблонних порад.\n\n"
         "Контекст репозиторію:\n" + context;
      request["prompt"] = promptTemplatesBundle + basePrompt;

      int status = 0;
      std::string response;
      const std::string ollamaBaseUrl = BuildOllamaBaseUrl();
      const std::string primaryUrl = ollamaBaseUrl + "/api/generate";
      const std::string alternateBaseUrl = AlternateLocalhostBaseUrl(ollamaBaseUrl);
      const std::string fallbackUrl = alternateBaseUrl.empty() ? "" : alternateBaseUrl + "/api/generate";

      std::string modelSetupError;
      const bool modelReady = EnsureOllamaModelAvailable(effectiveModel, modelSetupError);

      std::string transportError;
      bool ok = PostJson(primaryUrl, request, response, status, &transportError);

      if (!ok)
      {
         std::string fallbackTransportError;
         if (!fallbackUrl.empty())
         {
            ok = PostJson(fallbackUrl, request, response, status, &fallbackTransportError);
         }

         if (!ok && !fallbackTransportError.empty())
         {
            if (!transportError.empty())
            {
               transportError += " | ";
            }

            transportError += "fallback=" + fallbackTransportError;
         }
      }

      if (ok && status >= 200 && status < 300)
      {
         try
         {
            const auto outer = nlohmann::json::parse(response);
            const std::string text = outer.value("response", "");

            if (!text.empty())
            {
               nlohmann::json strictJson;
               const bool evidenceRequired = context.find("Repository cache is missing") == std::string::npos;
               std::string qualityReason;

               if (ValidateAndNormalizeAiJson(text, strictJson)
                  && IsUkrainianOrAllowedJson(strictJson)
                  && !IsLowQualityAiJson(strictJson, evidenceRequired, &qualityReason))
               {
                  score = strictJson.value("score", score);
                  thinking = strictJson.dump(2);
               }
               else
               {
                  nlohmann::json repairRequest = request;
                  repairRequest["prompt"] =
                     promptTemplatesBundle +
                     "Попередня відповідь невалідна або низької якості. Виправ її. "
                     "Поверни ЛИШЕ валідний JSON з обов'язковими полями та правильними типами. "
                     "Усі рядкові поля українською мовою, thinking = рядок. "
                     "Не використовуй шаблонні загальні слова: 'висновок', 'немає', 'без коментаря', 'n/a'. "
                     "У indicators/issues/recommendations дай змістовні технічні пункти з конкретними посиланнями на файли (наприклад src/main.cpp, README.md). "
                     "Причина відхилення попередньої відповіді: " + (qualityReason.empty() ? "невалідний формат або низька інформативність" : qualityReason) + ".\n\n"
                     "Оригінальна відповідь:\n" + text + "\n\n"
                     "Повторний аналіз за контекстом репозиторію:\n" + context;

                  std::string repairedResponse;
                  int repairedStatus = 0;
                  std::string repairedTransportError;
                  bool repairedOk = PostJson(primaryUrl, repairRequest, repairedResponse, repairedStatus, &repairedTransportError);

                  if (!repairedOk)
                  {
                     std::string repairedFallbackError;
                     if (!fallbackUrl.empty())
                     {
                        repairedOk = PostJson(fallbackUrl, repairRequest, repairedResponse, repairedStatus, &repairedFallbackError);
                     }

                     if (!repairedOk && !repairedFallbackError.empty())
                     {
                        if (!repairedTransportError.empty())
                        {
                           repairedTransportError += " | ";
                        }

                        repairedTransportError += "fallback=" + repairedFallbackError;
                     }
                  }

                  if (repairedOk && repairedStatus >= 200 && repairedStatus < 300)
                  {
                     try
                     {
                        const auto repairedOuter = nlohmann::json::parse(repairedResponse);
                        const std::string repairedText = repairedOuter.value("response", "");
                        nlohmann::json repairedStrict;
                        std::string repairedQualityReason;

                        if (ValidateAndNormalizeAiJson(repairedText, repairedStrict)
                           && IsUkrainianOrAllowedJson(repairedStrict)
                           && !IsLowQualityAiJson(repairedStrict, evidenceRequired, &repairedQualityReason))
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
            thinking = "Fallback-режим: не вдалося розпарсити JSON відповіді Ollama.";

            if (!response.empty())
            {
               thinking += " Тіло відповіді: " + ToPromptSafe(response, 300);
            }
         }
      }
      else
      {
         std::ostringstream reason;
         reason << "Fallback-режим: запит до Ollama неуспішний";

         if (!modelReady && !modelSetupError.empty())
         {
            reason << " (ініціалізація моделі: " << modelSetupError << ")";
         }

         if (ok)
         {
            reason << " (HTTP " << status << ")";
         }
         else
         {
            reason << " (помилка з'єднання)";

            if (!transportError.empty())
            {
               reason << ": " << transportError;
            }
         }

         if (!response.empty())
         {
            reason << ". Тіло відповіді: " << ToPromptSafe(response, 300);
         }

         thinking = reason.str();
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

      nlohmann::json finalReport;
      const bool hasRepositoryContext = context.find("Repository cache is missing") == std::string::npos;
      std::string finalQualityReason;
      const bool isStrictReport = ValidateAndNormalizeAiJson(thinking, finalReport)
         && IsUkrainianOrAllowedJson(finalReport)
         && !IsLowQualityAiJson(finalReport, hasRepositoryContext, &finalQualityReason);

      if (isStrictReport)
      {
         finalReport["score"] = score;
         finalReport["verdict"] = VerdictFromScore(score);

         if (hasRepositoryContext && !HasFileEvidenceInJson(finalReport))
         {
            finalReport["issues"].push_back("У відповіді мало прямих посилань на файли репозиторію, перевірте висновок вручну.");
         }
      }
      else
      {
         const bool hardTechnicalFallback = thinking.find("Fallback-режим") != std::string::npos;

         if (!hardTechnicalFallback)
         {
            // Do not trust score from low-quality/non-structured output.
            score = 50.0;
            finalReport = BuildRecoveredAiReport(score, thinking, hasRepositoryContext, submission);

            if (!finalQualityReason.empty())
            {
               finalReport["issues"].push_back("Причина відхилення структурованого звіту: " + finalQualityReason);
            }
         }
         else
         {
            // Never trust score when model/service is unavailable.
            score = 50.0;

            std::string reason = "fallback-режим через недоступність або технічну помилку моделі";
            finalReport = BuildDeterministicAiReport(score, reason, hasRepositoryContext, submission);
         }
      }

      thinking = finalReport.dump(2);

      submission.aiScore = score;
      submission.aiReport = thinking;

      nlohmann::json out;
      out["submissionId"] = submission.id;
      out["aiScore"] = score;
      out["thinking"] = thinking;
      out["decision"] = score >= 70.0 ? "Ймовірно AI" : (score >= 40.0 ? "Потрібна ручна перевірка" : "Ознак AI мало");
      out["source"] = promptTemplates.empty() ? "repo+readme" : "repo+multi-prompts";
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
      const RepositoryPlagiarismSnapshot repo = BuildRepositoryPlagiarismSnapshot(submission);

      struct ScoredRow
      {
         double total = 0.0;
         double tokenScore = 0.0;
         double sentenceScore = 0.0;
         double metadataScore = 0.0;
         size_t identicalSentenceCount = 0;
         std::vector<std::string> identicalSentences;
         std::string sample;
      };

      double best = 0.0;
      std::vector<ScoredRow> scored;

      for (const auto& row : dataset)
      {
         const DatasetSnapshot ds = BuildDatasetSnapshot(row);
         const double tokenScore = Jaccard(repo.tokens, ds.tokens);

         const size_t identical = IntersectionCount(repo.sentences, ds.sentences);
         const double sentenceScore = ds.sentences.empty()
            ? 0.0
            : static_cast<double>(identical) / static_cast<double>(ds.sentences.size());

         const double metadataScore = (
            RelativeSimilarity(repo.totalBytes, ds.chars)
            + RelativeSimilarity(repo.totalLines, ds.lines)
            + RelativeSimilarity(repo.sentences.size(), ds.sentences.size())) / 3.0;

         const double totalScore = 0.60 * sentenceScore + 0.25 * tokenScore + 0.15 * metadataScore;
         best = std::max(best, totalScore);

         ScoredRow rowScore;
         rowScore.total = totalScore;
         rowScore.tokenScore = tokenScore;
         rowScore.sentenceScore = sentenceScore;
         rowScore.metadataScore = metadataScore;
         rowScore.identicalSentenceCount = identical;
         rowScore.sample = row;

         for (const auto& sentence : ds.sentences)
         {
            if (repo.sentences.find(sentence) != repo.sentences.end())
            {
               rowScore.identicalSentences.push_back(sentence);
               if (rowScore.identicalSentences.size() >= 2)
               {
                  break;
               }
            }
         }

         scored.push_back(std::move(rowScore));
      }

      std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b)
      {
         return a.total > b.total;
      });

      submission.plagiarismScore = best * 100.0;

      nlohmann::json out;
      out["submissionId"] = submission.id;
      out["plagiarismScore"] = submission.plagiarismScore;
      out["algorithm"] = "metadata+identical-sentences";
      out["dataset"] = "text-only";
      out["samplesChecked"] = dataset.size();
      out["metadata"] = {
         {"scopePath", repo.scopedPath},
         {"scopeMissing", repo.scopeMissing},
         {"textFilesChecked", repo.textFiles},
         {"totalBytes", repo.totalBytes},
         {"totalLines", repo.totalLines},
         {"sentencesIndexed", repo.sentences.size()}
      };
      out["topMatches"] = nlohmann::json::array();

      for (size_t i = 0; i < scored.size() && i < 3; ++i)
      {
         nlohmann::json row;
         row["score"] = scored[i].total * 100.0;
         row["sentenceScore"] = scored[i].sentenceScore * 100.0;
         row["tokenScore"] = scored[i].tokenScore * 100.0;
         row["metadataScore"] = scored[i].metadataScore * 100.0;
         row["identicalSentenceCount"] = scored[i].identicalSentenceCount;
         row["identicalSentences"] = nlohmann::json::array();
         for (const auto& sentence : scored[i].identicalSentences)
         {
            row["identicalSentences"].push_back(ToPromptSafe(sentence, 160));
         }
         row["sample"] = ToPromptSafe(scored[i].sample, 220);
         out["topMatches"].push_back(row);
      }

      return out;
   }

   std::vector<std::string> PlagiarismService::ReadDataset()
   {
      std::vector<std::string> lines;
      std::ifstream input(datasetPath_);
      std::string line;

      while (std::getline(input, line))
      {
         const std::string trimmed = Trim(line);

         if (!trimmed.empty())
         {
            lines.push_back(trimmed);
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