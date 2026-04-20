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
#include <cmath>
#include <cstdint>

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

      std::filesystem::path BuildSubmissionScopedPath(const std::filesystem::path& base, const SubmissionItem& submission)
      {
         std::filesystem::path scoped = base;

         if (!submission.taskId.empty())
         {
            scoped /= submission.taskId;
         }

         scoped /= submission.id;
         return scoped;
      }

      RepositoryScopeSelection ResolveRepositoryScopeSelection(const SubmissionItem& submission)
      {
         RepositoryScopeSelection selection;
         selection.repoRoot = BuildSubmissionScopedPath(std::filesystem::path(RepoCacheDir()), submission);
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

      std::string ToLowerAscii(std::string value)
      {
         std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
         {
            return static_cast<char>(std::tolower(c));
         });
         return value;
      }

      std::vector<std::string> SplitWordsForNgrams(const std::string& input)
      {
         std::vector<std::string> words;
         std::regex split(R"([^A-Za-z0-9_]+)");
         std::sregex_token_iterator it(input.begin(), input.end(), split, -1);
         std::sregex_token_iterator end;

         for (; it != end; ++it)
         {
            std::string token = ToLowerAscii(it->str());

            if (!token.empty())
            {
               words.push_back(std::move(token));
            }
         }

         return words;
      }

      std::set<std::string> BuildWordNgrams(const std::string& input, size_t n)
      {
         std::set<std::string> out;
         const std::vector<std::string> words = SplitWordsForNgrams(input);

         if (n == 0 || words.size() < n)
         {
            return out;
         }

         for (size_t i = 0; i + n <= words.size(); ++i)
         {
            std::string gram;

            for (size_t j = 0; j < n; ++j)
            {
               if (!gram.empty())
               {
                  gram.push_back(' ');
               }
               gram += words[i + j];
            }

            out.insert(std::move(gram));
         }

         return out;
      }

      bool RabinKarpContains(const std::string& haystackRaw, const std::string& needleRaw)
      {
         const std::string haystack = NormalizeSentenceForMatch(haystackRaw);
         const std::string needle = NormalizeSentenceForMatch(needleRaw);

         if (needle.empty())
         {
            return false;
         }

         if (haystack.size() < needle.size())
         {
            return false;
         }

         constexpr std::uint64_t base = 911382323;
         constexpr std::uint64_t mod = 972663749;

         const size_t n = haystack.size();
         const size_t m = needle.size();
         std::uint64_t power = 1;

         for (size_t i = 1; i < m; ++i)
         {
            power = (power * base) % mod;
         }

         std::uint64_t hashNeedle = 0;
         std::uint64_t hashWindow = 0;

         for (size_t i = 0; i < m; ++i)
         {
            hashNeedle = (hashNeedle * base + static_cast<unsigned char>(needle[i])) % mod;
            hashWindow = (hashWindow * base + static_cast<unsigned char>(haystack[i])) % mod;
         }

         auto windowMatches = [&](size_t start)
         {
            return haystack.compare(start, m, needle) == 0;
         };

         if (hashNeedle == hashWindow && windowMatches(0))
         {
            return true;
         }

         for (size_t i = m; i < n; ++i)
         {
            const std::uint64_t leftChar = static_cast<unsigned char>(haystack[i - m]);
            const std::uint64_t rightChar = static_cast<unsigned char>(haystack[i]);

            hashWindow = (mod + hashWindow - (leftChar * power) % mod) % mod;
            hashWindow = (hashWindow * base + rightChar) % mod;

            const size_t start = i - m + 1;

            if (hashNeedle == hashWindow && windowMatches(start))
            {
               return true;
            }
         }

         return false;
      }

      bool HasSourceLinksAtEnd(const std::string& text)
      {
         if (text.empty())
         {
            return false;
         }

         const size_t window = std::min<size_t>(2600, text.size());
         const std::string tail = ToLowerAscii(text.substr(text.size() - window));
         const std::regex linkRx(R"((https?:\/\/|www\.)[^\s]+)");
         const size_t linkCount = static_cast<size_t>(std::distance(
            std::sregex_iterator(tail.begin(), tail.end(), linkRx),
            std::sregex_iterator()));

         const std::vector<std::string> markers = {
            "sources",
            "source",
            "references",
            "bibliography",
            "джерела",
            "посилання",
            "література"
         };

         bool hasMarker = false;

         for (const auto& marker : markers)
         {
            if (tail.find(marker) != std::string::npos)
            {
               hasMarker = true;
               break;
            }
         }

         return linkCount >= 2 || (hasMarker && linkCount >= 1);
      }

      std::vector<std::string> SplitSiteEntries(const std::string& raw)
      {
         std::vector<std::string> out;
         std::string token;

         auto flush = [&]()
         {
            if (!token.empty())
            {
               out.push_back(token);
               token.clear();
            }
         };

         for (char ch : raw)
         {
            if (ch == ',' || ch == ';' || ch == '\n' || ch == '\r' || ch == '\t')
            {
               flush();
               continue;
            }

            token.push_back(ch);
         }

         flush();
         return out;
      }

      std::string NormalizeSiteEntry(std::string value)
      {
         auto trim = [](std::string& text)
         {
            const auto begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
            {
               text.clear();
               return;
            }

            const auto end = text.find_last_not_of(" \t\r\n");
            text = text.substr(begin, end - begin + 1);
         };

         trim(value);
         value = ToLowerAscii(value);

         const std::string https = "https://";
         const std::string http = "http://";

         if (value.rfind(https, 0) == 0)
         {
            value = value.substr(https.size());
         }
         else if (value.rfind(http, 0) == 0)
         {
            value = value.substr(http.size());
         }

         if (value.rfind("www.", 0) == 0)
         {
            value = value.substr(4);
         }

         const auto slash = value.find('/');
         if (slash != std::string::npos)
         {
            value = value.substr(0, slash);
         }

         const auto qmark = value.find('?');
         if (qmark != std::string::npos)
         {
            value = value.substr(0, qmark);
         }

         const auto hash = value.find('#');
         if (hash != std::string::npos)
         {
            value = value.substr(0, hash);
         }

         while (!value.empty() && (value.back() == '.' || value.back() == '/'))
         {
            value.pop_back();
         }

         trim(value);
         return value;
      }

      std::vector<std::string> NormalizeSiteList(const std::vector<std::string>& entries)
      {
         std::vector<std::string> out;
         std::set<std::string> seen;

         for (const auto& entry : entries)
         {
            const std::string normalized = NormalizeSiteEntry(entry);

            if (normalized.empty())
            {
               continue;
            }

            if (!seen.insert(normalized).second)
            {
               continue;
            }

            out.push_back(normalized);
         }

         return out;
      }

      bool DomainAppearsInText(const std::string& lowerText, const std::string& domain)
      {
         if (lowerText.empty() || domain.empty())
         {
            return false;
         }

         auto isDomainChar = [](char c)
         {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.';
         };

         size_t pos = lowerText.find(domain);

         while (pos != std::string::npos)
         {
            const bool leftOk = pos == 0 || !isDomainChar(lowerText[pos - 1]);
            const size_t right = pos + domain.size();
            const bool rightOk = right >= lowerText.size() || !isDomainChar(lowerText[right]);

            if (leftOk && rightOk)
            {
               return true;
            }

            pos = lowerText.find(domain, pos + 1);
         }

         return false;
      }

      std::vector<std::string> FindMatchedDomains(const std::string& lowerText, const std::vector<std::string>& configuredDomains)
      {
         std::vector<std::string> out;

         for (const auto& domain : configuredDomains)
         {
            if (DomainAppearsInText(lowerText, domain))
            {
               out.push_back(domain);
            }
         }

         return out;
      }

      struct SiteRules
      {
         std::vector<std::string> whitelist;
         std::vector<std::string> blacklist;
      };

      SiteRules DefaultSiteRules()
      {
         SiteRules rules;
         rules.whitelist = {"wikipedia.org"};
         return rules;
      }

      SiteRules ReadSiteRulesFromFile(const std::string& path)
      {
         SiteRules rules = DefaultSiteRules();
         const std::string raw = ReadFileText(path);

         if (raw.empty())
         {
            return rules;
         }

         try
         {
            const auto doc = nlohmann::json::parse(raw);
            std::vector<std::string> whitelist;
            std::vector<std::string> blacklist;

            if (doc.contains("whitelist") && doc["whitelist"].is_array())
            {
               for (const auto& node : doc["whitelist"])
               {
                  if (node.is_string())
                  {
                     whitelist.push_back(node.get<std::string>());
                  }
               }
            }
            else if (doc.contains("whitelist") && doc["whitelist"].is_string())
            {
               const auto split = SplitSiteEntries(doc["whitelist"].get<std::string>());
               whitelist.insert(whitelist.end(), split.begin(), split.end());
            }

            if (doc.contains("blacklist") && doc["blacklist"].is_array())
            {
               for (const auto& node : doc["blacklist"])
               {
                  if (node.is_string())
                  {
                     blacklist.push_back(node.get<std::string>());
                  }
               }
            }
            else if (doc.contains("blacklist") && doc["blacklist"].is_string())
            {
               const auto split = SplitSiteEntries(doc["blacklist"].get<std::string>());
               blacklist.insert(blacklist.end(), split.begin(), split.end());
            }

            rules.whitelist = NormalizeSiteList(whitelist);
            rules.blacklist = NormalizeSiteList(blacklist);
         }
         catch (...)
         {
         }

         if (rules.whitelist.empty() && rules.blacklist.empty())
         {
            rules = DefaultSiteRules();
         }

         return rules;
      }

      nlohmann::json SiteRulesToJson(const SiteRules& rules)
      {
         nlohmann::json out;
         out["whitelist"] = rules.whitelist;
         out["blacklist"] = rules.blacklist;
         return out;
      }

      struct RepositoryPlagiarismSnapshot
      {
         std::set<std::string> tokens;
         std::set<std::string> sentences;
         std::set<std::string> trigrams;
         size_t textFiles = 0;
         size_t totalBytes = 0;
         size_t totalLines = 0;
         std::string scopedPath = "(repository root)";
         bool scopeMissing = false;
         bool hasSourceLinksAtEnd = false;
         std::string normalizedText;
         std::string rawTextLower;
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

         snapshot.trigrams = BuildWordNgrams(combined, 3);
         snapshot.normalizedText = NormalizeSentenceForMatch(combined);
         snapshot.rawTextLower = ToLowerAscii(combined);
         snapshot.hasSourceLinksAtEnd = HasSourceLinksAtEnd(combined);

         return snapshot;
      }

      struct DatasetSnapshot
      {
         std::string raw;
         std::set<std::string> tokens;
         std::set<std::string> sentences;
         std::set<std::string> trigrams;
         std::string normalizedText;
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
         out.trigrams = BuildWordNgrams(row, 3);
         out.normalizedText = NormalizeSentenceForMatch(row);

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

      std::pair<size_t, size_t> CountLatinAndCyrillicLetters(const std::string& text)
      {
         size_t latin = 0;
         size_t cyrillic = 0;

         for (size_t i = 0; i < text.size(); ++i)
         {
            const unsigned char c = static_cast<unsigned char>(text[i]);

            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            {
               latin++;
               continue;
            }

            if ((c == 0xD0 || c == 0xD1 || c == 0xD2) && i + 1 < text.size())
            {
               const unsigned char next = static_cast<unsigned char>(text[i + 1]);
               if (next >= 0x80 && next <= 0xBF)
               {
                  cyrillic++;
                  i++;
               }
            }
         }

         return { latin, cyrillic };
      }

      std::string PrepareTextForLanguageCheck(const std::string& input)
      {
         std::string text = input;

         const size_t evidencePos = text.find("| evidence:");
         if (evidencePos != std::string::npos)
         {
            text = text.substr(0, evidencePos);
         }

         static const std::regex fileTagRx(R"(\[file:\s*[^\]]*\])", std::regex::icase);
         text = std::regex_replace(text, fileTagRx, " ");

         static const std::regex pathLikeRx(R"(([A-Za-z]:\\|\./|\.\./|/)?[A-Za-z0-9_\-./]+\.(c|cc|cpp|cxx|h|hh|hpp|hxx|py|js|ts|tsx|jsx|java|cs|go|rs|kt|swift|php|rb|sh|ps1|sql|html|css|json|yaml|yml|toml|xml|md|txt))", std::regex::icase);
         text = std::regex_replace(text, pathLikeRx, " ");

         return Trim(text);
      }

      bool LooksMostlyUkrainianText(const nlohmann::json& value)
      {
         size_t latin = 0;
         size_t cyrillic = 0;

         auto absorb = [&](const std::string& raw)
         {
            const std::string text = PrepareTextForLanguageCheck(raw);
            if (text.empty())
            {
               return;
            }

            const auto counts = CountLatinAndCyrillicLetters(text);
            latin += counts.first;
            cyrillic += counts.second;
         };

         absorb(value.value("thinking", ""));
         absorb(value.value("email_comment", ""));

         for (const auto& field : { "indicators", "strong_parts", "issues", "recommendations" })
         {
            if (!value.contains(field) || !value[field].is_array())
            {
               continue;
            }

            for (const auto& item : value[field])
            {
               if (item.is_string())
               {
                  absorb(item.get<std::string>());
               }
            }
         }

         const size_t totalLetters = latin + cyrillic;

         if (totalLetters < 30)
         {
            return cyrillic >= 8;
         }

         // Require a stable Cyrillic signal in narrative fields, but allow technical tokens.
         return cyrillic >= 18 && (cyrillic * 100 >= totalLetters * 28);
      }

      bool HasConcreteIssueEvidence(const nlohmann::json& value)
      {
         if (!value.contains("issues") || !value["issues"].is_array())
         {
            return false;
         }

         for (const auto& issue : value["issues"])
         {
            if (!issue.is_string())
            {
               continue;
            }

            std::string line = LowerAscii(issue.get<std::string>());
            const size_t evidencePos = line.find("evidence:");
            if (evidencePos == std::string::npos)
            {
               continue;
            }

            std::string evidence = Trim(line.substr(evidencePos + std::string("evidence:").size()));
            if (evidence.empty())
            {
               continue;
            }

            if (evidence == "no evidence found" || evidence == "немає доказів" || evidence == "немає evidence")
            {
               continue;
            }

            if (evidence.find("...") != std::string::npos)
            {
               continue;
            }

            if (evidence.size() < 16)
            {
               continue;
            }

            return true;
         }

         return false;
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

         if (!LooksMostlyUkrainianText(value))
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

         if (!HasConcreteIssueEvidence(value))
         {
            if (reason != nullptr)
            {
               *reason = "issues не містять конкретного evidence (без ... і шаблонів)";
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

      double ClampScore01(double score)
      {
         if (score < 0.0)
         {
            return 0.0;
         }

         if (score > 100.0)
         {
            return 100.0;
         }

         return score;
      }

      double Round1(double value)
      {
         return std::round(value * 10.0) / 10.0;
      }

      int ScoreBucket(double score)
      {
         if (score > 70.0)
         {
            return 2;
         }

         if (score < 40.0)
         {
            return 0;
         }

         return 1;
      }

      double AddDeterministicScoreNuanceIfRounded(
         double score,
         const std::string& seedText,
         size_t fileRefCount,
         const RepositoryFallbackFacts& facts)
      {
         score = ClampScore01(score);

         if (score <= 2.0 || score >= 98.0)
         {
            return Round1(score);
         }

         const double nearest10 = std::round(score / 10.0) * 10.0;
         const bool roundedToTen = std::fabs(score - nearest10) < 1e-9;
         const bool roundedToInt = std::fabs(score - std::round(score)) < 1e-9;

         if (!roundedToTen && !roundedToInt)
         {
            return Round1(score);
         }

         size_t letters = 0;
         size_t digits = 0;

         for (unsigned char ch : seedText)
         {
            if (std::isalpha(ch))
            {
               letters++;
            }
            else if (std::isdigit(ch))
            {
               digits++;
            }
         }

         const size_t basis =
            seedText.size()
            + letters * 3
            + digits * 5
            + fileRefCount * 17
            + facts.textFiles * 11
            + facts.cppLikeFiles * 19
            + facts.totalFiles * 7;

         const int bucket = static_cast<int>(basis % 19); // 0..18
         double offset = (static_cast<double>(bucket) - 9.0) / 2.0; // -4.5..4.5

         if (std::fabs(offset) < 1e-9)
         {
            offset = 1.3;
         }
         else if (offset > 0.0)
         {
            offset += 0.3;
         }
         else
         {
            offset -= 0.3;
         }

         double nuanced = ClampScore01(score + offset);

         const int originalBucket = ScoreBucket(score);
         const int newBucket = ScoreBucket(nuanced);

         // Keep the same verdict bucket while adding non-round precision.
         if (newBucket != originalBucket)
         {
            if (originalBucket == 2)
            {
               nuanced = std::max(70.1, score - std::fabs(offset) * 0.35);
            }
            else if (originalBucket == 0)
            {
               nuanced = std::min(39.9, score + std::fabs(offset) * 0.35);
            }
            else
            {
               nuanced = score >= 55.0
                  ? std::min(69.9, score + std::fabs(offset) * 0.35)
                  : std::max(40.1, score - std::fabs(offset) * 0.35);
            }
         }

         return Round1(ClampScore01(nuanced));
      }

      std::vector<std::string> ExtractFileRefs(const std::string& text)
      {
         static const std::regex fileRef(
            R"((?:[A-Za-z]:\\|\./|\.\./|/)?[A-Za-z0-9_\-./]+\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|py|js|ts|tsx|jsx|java|cs|go|rs|kt|swift|php|rb|sh|ps1|sql|html|css|json|yaml|yml|toml|xml|md|txt))",
            std::regex::icase);

         std::vector<std::string> files;
         std::unordered_set<std::string> seen;
         std::sregex_iterator it(text.begin(), text.end(), fileRef);
         std::sregex_iterator end;

         for (; it != end; ++it)
         {
            std::string file = it->str();
            if (seen.insert(file).second)
            {
               files.push_back(file);
               if (files.size() >= 3)
               {
                  break;
               }
            }
         }

         return files;
      }

      std::string ExtractEvidenceSnippet(const std::string& text)
      {
         static const std::regex quotedEvidence(R"__RX__(evidence\s*:\s*"([^"]{10,220})")__RX__", std::regex::icase);
         static const std::regex plainEvidence(R"(evidence\s*:\s*([^\r\n]{16,220}))", std::regex::icase);
         std::smatch m;

         if (std::regex_search(text, m, quotedEvidence) && m.size() > 1)
         {
            return Trim(m[1].str());
         }

         if (std::regex_search(text, m, plainEvidence) && m.size() > 1)
         {
            return Trim(m[1].str());
         }

         return "див. вміст файлу та фрагменти коду в цільовій директорії";
      }

      std::string NormalizeFileRefToken(std::string value)
      {
         value = Trim(value);

         while (!value.empty() && (value.front() == '"' || value.front() == '\'' || value.front() == '`'))
         {
            value.erase(value.begin());
         }

         while (!value.empty() && (value.back() == '"' || value.back() == '\'' || value.back() == '`' || value.back() == '.' || value.back() == ',' || value.back() == ';' || value.back() == ':'))
         {
            value.pop_back();
         }

         std::replace(value.begin(), value.end(), '\\', '/');

         while (value.rfind("./", 0) == 0)
         {
            value.erase(0, 2);
         }

         while (!value.empty() && value.front() == '/')
         {
            value.erase(value.begin());
         }

         return Trim(value);
      }

      bool IsRelativeFileInScope(const RepositoryScopeSelection& scope, const std::string& normalizedRef)
      {
         if (normalizedRef.empty())
         {
            return false;
         }

         const std::filesystem::path rel = std::filesystem::path(normalizedRef).lexically_normal();

         if (!IsSafeRelativePath(rel))
         {
            return false;
         }

         auto existsInRoot = [](const std::filesystem::path& root, const std::filesystem::path& relPath)
         {
            const std::filesystem::path candidate = (root / relPath).lexically_normal();
            const std::string base = root.lexically_normal().generic_string();
            const std::string path = candidate.generic_string();
            const std::string baseWithSlash = base.empty() || base.back() == '/' ? base : (base + "/");

            if (!(path == base || path.rfind(baseWithSlash, 0) == 0))
            {
               return false;
            }

            return std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate);
         };

         if (existsInRoot(scope.analysisRoot, rel))
         {
            return true;
         }

         if (existsInRoot(scope.repoRoot, rel))
         {
            return true;
         }

         if (scope.singleFile.has_value())
         {
            const auto single = scope.singleFile.value().filename().string();
            return LowerAscii(single) == LowerAscii(rel.filename().string());
         }

         return false;
      }

      std::string ResolveRecoveryFileRef(
         const std::string& rawRef,
         const SubmissionItem& submission,
         const std::vector<std::string>& sampleFiles)
      {
         const std::string normalized = NormalizeFileRefToken(rawRef);
         const RepositoryScopeSelection scope = ResolveRepositoryScopeSelection(submission);

         if (!normalized.empty() && IsRelativeFileInScope(scope, normalized))
         {
            return normalized;
         }

         if (!sampleFiles.empty())
         {
            const std::string rawLower = LowerAscii(normalized);
            const std::string rawName = LowerAscii(std::filesystem::path(normalized).filename().string());
            const std::string rawStem = LowerAscii(std::filesystem::path(normalized).stem().string());

            for (const auto& sample : sampleFiles)
            {
               const std::string sampleNorm = NormalizeFileRefToken(sample);
               const std::string sampleLower = LowerAscii(sampleNorm);

               if (!rawLower.empty() && sampleLower == rawLower)
               {
                  return sampleNorm;
               }

               const std::string sampleName = LowerAscii(std::filesystem::path(sampleNorm).filename().string());
               if (!rawName.empty() && sampleName == rawName)
               {
                  return sampleNorm;
               }

               const std::string sampleStem = LowerAscii(std::filesystem::path(sampleNorm).stem().string());
               if (!rawStem.empty() && sampleStem == rawStem)
               {
                  return sampleNorm;
               }
            }

            // Keep recovery deterministic and grounded in existing repo files.
            return NormalizeFileRefToken(sampleFiles.front());
         }

         return normalized;
      }

      nlohmann::json BuildLocalStructuredAiReportFromRaw(
         const std::string& rawAnalysis,
         const SubmissionItem& submission,
         double scoreHint)
      {
         const RepositoryFallbackFacts facts = CollectRepositoryFallbackFacts(submission);

         std::string scoreSource = rawAnalysis;
         const double hinted = ClampScore01(scoreHint);
         double score = hinted;

         std::smatch m;
         static const std::regex scoreGuess(R"(score\s*guess\s*[:=]?\s*([0-9]{1,3}(?:\.[0-9]+)?))", std::regex::icase);
         static const std::regex scoreAny(R"((?:score|бал)\s*[:=]?\s*([0-9]{1,3}(?:\.[0-9]+)?))", std::regex::icase);

         if (std::regex_search(scoreSource, m, scoreGuess) && m.size() > 1)
         {
            try
            {
               score = ClampScore01(std::stod(m[1].str()));
            }
            catch (...)
            {
               score = hinted;
            }
         }
         else if (std::regex_search(scoreSource, m, scoreAny) && m.size() > 1)
         {
            try
            {
               score = ClampScore01(std::stod(m[1].str()));
            }
            catch (...)
            {
               score = hinted;
            }
         }

         const std::vector<std::string> files = ExtractFileRefs(rawAnalysis);
         const std::string evidence = ExtractEvidenceSnippet(rawAnalysis);

         score = AddDeterministicScoreNuanceIfRounded(score, rawAnalysis + "\n" + evidence, files.size(), facts);

         std::vector<std::string> resolvedFiles;
         std::unordered_set<std::string> seenResolved;

         for (const auto& file : files)
         {
            const std::string resolved = ResolveRecoveryFileRef(file, submission, facts.sampleFiles);
            if (!resolved.empty() && seenResolved.insert(LowerAscii(resolved)).second)
            {
               resolvedFiles.push_back(resolved);
            }
         }

         if (resolvedFiles.empty() && !facts.sampleFiles.empty())
         {
            resolvedFiles.push_back(NormalizeFileRefToken(facts.sampleFiles.front()));
         }

         std::string primaryFile = resolvedFiles.empty() ? "" : resolvedFiles.front();
         if (primaryFile.empty() && !facts.sampleFiles.empty())
         {
            primaryFile = NormalizeFileRefToken(facts.sampleFiles.front());
         }

         std::string secondaryFile;
         if (resolvedFiles.size() > 1)
         {
            secondaryFile = resolvedFiles[1];
         }
         else if (facts.sampleFiles.size() > 1)
         {
            secondaryFile = NormalizeFileRefToken(facts.sampleFiles[1]);
         }

         nlohmann::json out;
         out["score"] = score;
         out["verdict"] = VerdictFromScore(score);
         out["thinking"] = "Оцінку сформовано за аналізом вмісту репозиторію та перевірюваних фрагментів коду.";

         nlohmann::json indicators = nlohmann::json::array();
         indicators.push_back("Виявлено стилістичні та структурні сигнали у коді, які потребують ручного підтвердження.");
         if (!primaryFile.empty())
         {
            indicators.push_back("Ключовий файл для перевірки: " + primaryFile + ".");
         }
         indicators.push_back("Область перевірки: " + facts.scopedPath + ".");
         out["indicators"] = indicators;

         nlohmann::json strong = nlohmann::json::array();
         strong.push_back("Проаналізовано матеріали репозиторію: текстових файлів " + std::to_string(facts.textFiles)
            + ", C/C++ файлів " + std::to_string(facts.cppLikeFiles) + ".");
         out["strong_parts"] = strong;

         nlohmann::json issues = nlohmann::json::array();
         if (!primaryFile.empty())
         {
            issues.push_back("Потребує ручної перевірки фрагмент [file: " + primaryFile + "] | evidence: " + evidence);
         }
         else
         {
            issues.push_back("Потребують ручної перевірки ключові фрагменти [file: README.md] | evidence: " + evidence);
         }

         if (!secondaryFile.empty())
         {
            issues.push_back("Додатково перевірити суміжний файл вручну [file: " + secondaryFile + "] | evidence: уточнити контекст під час code review.");
         }
         out["issues"] = issues;

         nlohmann::json recommendations = nlohmann::json::array();
         recommendations.push_back("Вручну звірити наведені evidence з фактичним кодом у цільових файлах.");
         recommendations.push_back("Після ручної верифікації оновити фінальний висновок по роботі.");
         out["recommendations"] = recommendations;

         out["email_comment"] = "Перевірте зазначені фрагменти коду та за потреби уточніть висновок перед фінальною оцінкою.";
         return out;
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
            out["thinking"] = "Оцінку сформовано за доступними даними репозиторію з акцентом на ручну верифікацію ключових файлів.";
            out["indicators"] = nlohmann::json::array({
               "Цільова область перевірки: " + facts.scopedPath + ".",
               "Локально проаналізовано приблизно " + std::to_string(facts.textFiles) + " текстових файлів із " + std::to_string(facts.totalFiles) + " загальних.",
               "Приклади файлів для ручної верифікації: " + sampleFiles + "."
            });
            out["issues"] = nlohmann::json::array({
               "Потрібна додаткова ручна перевірка через неповну автоматичну деталізацію звіту."
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
               "Після перевірки файлів уточнити оцінку та підсумковий коментар."
            });
            out["strong_parts"] = nlohmann::json::array({
               "Репозиторій доступний локально, тому базові структурні сигнали роботи зібрано повністю."
            });
            out["email_comment"] = "Для фінального рішення перевірте вказані файли вручну та звірте їх із висновком.";
         }
         else
         {
            out["thinking"] = "Оцінка попередня, оскільки локальні матеріали репозиторію відсутні для повної перевірки.";
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
            out["email_comment"] = "Перед остаточним висновком потрібно отримати репозиторій студента та виконати повну перевірку.";
         }

         if (!modelReason.empty() && hasRepositoryContext)
         {
            out["issues"].push_back("Автоматичну частину перевірки потрібно підтвердити вручну для надійного висновку.");
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
         out["thinking"] = "Оцінку сформовано на основі фактичних даних репозиторію; потрібна ручна звірка ключових фрагментів.";

         out["strong_parts"] = nlohmann::json::array({
            "Зібрано базові сигнали репозиторію: текстових файлів " + std::to_string(facts.textFiles) + ", C/C++ файлів " + std::to_string(facts.cppLikeFiles) + "."
         });
         out["indicators"] = nlohmann::json::array({
            "Цільова область перевірки: " + facts.scopedPath + ".",
            "Автоматичний висновок потребує підтвердження конкретними фрагментами коду.",
            "Для ручної перевірки доступні файли: " + sampleFiles + "."
         });
         out["issues"] = nlohmann::json::array({
            "Потрібна ручна верифікація ключових пунктів перед фінальним рішенням."
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
            "За результатом перевірки оновити оцінку та підсумковий коментар для студента."
         });
         out["email_comment"] = "Для фінального рішення перегляньте вручну ключові файли та підтвердіть висновок.";
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

         const std::filesystem::path localPath = BuildSubmissionScopedPath(std::filesystem::path(RepoCacheDir()), *s);
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
   const double effectiveTemperature = (temperature > 0.0 && temperature <= 1.0) ? temperature : 0.2;
      const std::string customSystemPrompt = ReadFileText(BuildSystemPromptPath());
      const bool usePromptTemplatesMode = !promptTemplates.empty();
      const std::string ollamaBaseUrl = BuildOllamaBaseUrl();
      const std::string primaryUrl = ollamaBaseUrl + "/api/generate";
      const std::string alternateBaseUrl = AlternateLocalhostBaseUrl(ollamaBaseUrl);
      const std::string fallbackUrl = alternateBaseUrl.empty() ? "" : alternateBaseUrl + "/api/generate";

      auto postWithFallback = [&](const nlohmann::json& req, std::string& outBody, int& outStatus, std::string& outTransportError)
      {
         outTransportError.clear();

         bool localOk = PostJson(primaryUrl, req, outBody, outStatus, &outTransportError);

         if (!localOk)
         {
            std::string fallbackTransportError;
            if (!fallbackUrl.empty())
            {
               localOk = PostJson(fallbackUrl, req, outBody, outStatus, &fallbackTransportError);
            }

            if (!localOk && !fallbackTransportError.empty())
            {
               if (!outTransportError.empty())
               {
                  outTransportError += " | ";
               }

               outTransportError += "fallback=" + fallbackTransportError;
            }
         }

         return localOk;
      };

      std::string modelSetupError;
      const bool modelReady = EnsureOllamaModelAvailable(effectiveModel, modelSetupError);

      nlohmann::json rawRequest;
      rawRequest["model"] = effectiveModel;
      rawRequest["stream"] = false;
      rawRequest["system"] = "Ти code-review асистент AIChecker. Аналізуй репозиторій і відповідай українською. Без JSON на цьому етапі.";
      rawRequest["options"] = {
         {"temperature", effectiveTemperature},
         {"top_k", 40},
         {"top_p", 0.8},
         {"seed", 42},
         {"repeat_penalty", 1.1},
         {"repeat_last_n", 256},
         {"num_ctx", 8192},
         {"num_predict", 900}
      };

      const std::string rawPrompt =
         "Ти code-review асистент.\n\n"
         "Завдання: проаналізуй файли репозиторію та підготуй структурований опис українською мовою.\n\n"
         "ПРАВИЛА:\n"
         "- Пиши просто і конкретно\n"
         "- Обов'язково вказуй назви файлів\n"
         "- Додавай короткі фрагменти коду як evidence, коли можливо\n"
         "- Не використовуй JSON\n"
         "- Не додавай markdown-обгортки\n\n"
         "ФОРМАТ ВІДПОВІДІ:\n\n"
         "ANALYSIS:\n\n"
         "FILES:\n"
         "- file: <path>\n"
         "  summary: <короткий опис>\n\n"
         "ISSUES:\n"
         "- file: <path>\n"
         "  problem: <опис проблеми>\n"
         "  evidence: \"<точний фрагмент коду>\" або \"немає доказів\"\n\n"
         "STRONG PARTS:\n"
         "- file: <path>\n"
         "  good: <опис сильної сторони>\n\n"
         "INDICATORS:\n"
         "- <спостереження щодо AI/HUMAN стилю>\n\n"
         "CONCLUSION:\n"
         "- короткий висновок (1-2 речення)\n"
         "- verdict guess: HUMAN або AI або UNCERTAIN\n"
         "- score guess: точне число 0..100 (бажано з 1 знаком після коми, не округлюй до десятків)\n\n"
         "Контекст репозиторію:\n" + context;
      rawRequest["prompt"] = rawPrompt;

      int rawStatus = 0;
      std::string rawResponse;
      std::string rawTransportError;
      const bool rawOk = postWithFallback(rawRequest, rawResponse, rawStatus, rawTransportError);

      std::string rawAnalysis;

      if (rawOk && rawStatus >= 200 && rawStatus < 300)
      {
         try
         {
            const auto rawOuter = nlohmann::json::parse(rawResponse);
            rawAnalysis = Trim(rawOuter.value("response", ""));
         }
         catch (...)
         {
            thinking = "Fallback-режим: не вдалося розпарсити відповідь етапу RAW ANALYSIS.";

            if (!rawResponse.empty())
            {
               thinking += " Тіло відповіді: " + ToPromptSafe(rawResponse, 300);
            }
         }
      }
      else
      {
         std::ostringstream reason;
         reason << "Fallback-режим: етап RAW ANALYSIS неуспішний";

         if (!modelReady && !modelSetupError.empty())
         {
            reason << " (ініціалізація моделі: " << modelSetupError << ")";
         }

         if (rawOk)
         {
            reason << " (HTTP " << rawStatus << ")";
         }
         else
         {
            reason << " (помилка з'єднання)";

            if (!rawTransportError.empty())
            {
               reason << ": " << rawTransportError;
            }
         }

         if (!rawResponse.empty())
         {
            reason << ". Тіло відповіді: " << ToPromptSafe(rawResponse, 300);
         }

         thinking = reason.str();
      }

      if (!rawAnalysis.empty())
      {
         nlohmann::json jsonRequest;
         jsonRequest["model"] = effectiveModel;
         jsonRequest["stream"] = false;
         jsonRequest["format"] = {
            {"type", "object"},
            {"properties", {
               {"score", {{"type", "number"}}},
               {"verdict", {{"type", "string"}}},
               {"thinking", {{"type", "string"}}},
               {"indicators", {{"type", "array"}, {"items", {{"type", "string"}}}}},
               {"strong_parts", {{"type", "array"}, {"items", {{"type", "string"}}}}},
               {"issues", {{"type", "array"}, {"items", {
                  {"type", "object"},
                  {"properties", {
                     {"description", {{"type", "string"}}},
                     {"file", {{"type", "string"}}},
                     {"evidence", {{"type", "string"}}}
                  }},
                  {"required", nlohmann::json::array({"description", "file", "evidence"})},
                  {"additionalProperties", false}
               }}}},
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

         jsonRequest["system"] = usePromptTemplatesMode
            ? "Ти генератор JSON для AIChecker. Поверни лише валідний JSON без markdown і зайвого тексту."
            : (customSystemPrompt.empty()
               ? "Ти генератор JSON для AIChecker. Поверни лише валідний JSON без markdown і зайвого тексту."
               : customSystemPrompt);

         jsonRequest["options"] = {
            {"temperature", effectiveTemperature},
            {"top_k", 40},
            {"top_p", 0.8},
            {"seed", 42},
            {"repeat_penalty", 1.1},
            {"repeat_last_n", 256},
            {"num_ctx", 8192},
            {"num_predict", 900}
         };

         const std::string jsonPrompt =
            "Ти генератор JSON.\n\n"
            "Завдання: перетвори вхідний аналіз у СТРОГИЙ JSON за схемою нижче.\n\n"
            "ПРАВИЛА:\n"
            "- Поверни ТІЛЬКИ валідний JSON\n"
            "- Жодного тексту поза JSON\n"
            "- Дотримуйся схеми без додаткових полів\n"
            "- Усі текстові поля: українською (крім технічних токенів і verdict)\n"
            "- Коротко і конкретно\n\n"
            "SCHEMA:\n"
            "{\n"
            "  \"score\": number,\n"
            "  \"verdict\": \"HUMAN\" | \"AI\" | \"UNCERTAIN\",\n"
            "  \"thinking\": \"\",\n"
            "  \"indicators\": [],\n"
            "  \"strong_parts\": [],\n"
            "  \"issues\": [\n"
            "    {\n"
            "      \"description\": \"\",\n"
            "      \"file\": \"\",\n"
            "      \"evidence\": \"\"\n"
            "    }\n"
            "  ],\n"
            "  \"recommendations\": [],\n"
            "  \"email_comment\": \"\"\n"
            "}\n\n"
            "ВИМОГИ:\n"
            "- Витягни факти лише з вхідного аналізу\n"
            "- Якщо evidence відсутній, використай: \"немає доказів\"\n"
            "- thinking: 1 речення\n"
            "- email_comment: короткий підсумок\n"
            "- score: точне число 0..100 з 1 знаком після коми, без округлення до десятків\n"
            "- issues: масив об'єктів {description, file, evidence}\n\n"
            "ВХІД:\n" + rawAnalysis;
         jsonRequest["prompt"] = promptTemplatesBundle + jsonPrompt;

         int status = 0;
         std::string response;
         std::string transportError;
         const bool ok = postWithFallback(jsonRequest, response, status, transportError);

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
                     nlohmann::json fixRequest = jsonRequest;
                     fixRequest["prompt"] =
                        "Виправ цей JSON. Поверни ТІЛЬКИ валідний JSON за потрібною схемою, без markdown і без зайвого тексту. "
                        "Усі текстові поля українською (крім verdict).\n\n"
                        "Пошкоджений JSON:\n" + text;

                     std::string fixedResponse;
                     int fixedStatus = 0;
                     std::string fixedTransportError;
                     const bool fixedOk = postWithFallback(fixRequest, fixedResponse, fixedStatus, fixedTransportError);

                     if (fixedOk && fixedStatus >= 200 && fixedStatus < 300)
                     {
                        try
                        {
                           const auto fixedOuter = nlohmann::json::parse(fixedResponse);
                           const std::string fixedText = fixedOuter.value("response", "");
                           nlohmann::json fixedStrict;
                           std::string fixedQualityReason;

                           if (ValidateAndNormalizeAiJson(fixedText, fixedStrict)
                              && IsUkrainianOrAllowedJson(fixedStrict)
                              && !IsLowQualityAiJson(fixedStrict, evidenceRequired, &fixedQualityReason))
                           {
                              score = fixedStrict.value("score", score);
                              thinking = fixedStrict.dump(2);
                           }
                           else
                           {
                              score = ExtractScore(fixedText, score);
                              nlohmann::json localStrict = BuildLocalStructuredAiReportFromRaw(rawAnalysis, submission, score);
                              score = localStrict.value("score", score);
                              thinking = localStrict.dump(2);
                           }
                        }
                        catch (...)
                        {
                           score = ExtractScore(text, score);
                           nlohmann::json localStrict = BuildLocalStructuredAiReportFromRaw(rawAnalysis, submission, score);
                           score = localStrict.value("score", score);
                           thinking = localStrict.dump(2);
                        }
                     }
                     else
                     {
                        score = ExtractScore(text, score);
                        nlohmann::json localStrict = BuildLocalStructuredAiReportFromRaw(rawAnalysis, submission, score);
                        score = localStrict.value("score", score);
                        thinking = localStrict.dump(2);
                     }
                  }
               }
               else
               {
                  thinking = "Fallback-режим: етап JSON BUILDER повернув порожню відповідь.";
               }
            }
            catch (...)
            {
               thinking = "Fallback-режим: не вдалося розпарсити відповідь етапу JSON BUILDER.";

               if (!response.empty())
               {
                  thinking += " Тіло відповіді: " + ToPromptSafe(response, 300);
               }
            }
         }
         else
         {
            std::ostringstream reason;
            reason << "Fallback-режим: етап JSON BUILDER неуспішний";

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
      }
      else if (thinking == "Fallback-режим")
      {
         thinking = "Fallback-режим: етап RAW ANALYSIS повернув порожню відповідь.";
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
      const bool isStrictReport = ValidateAndNormalizeAiJson(thinking, finalReport)
         && IsUkrainianOrAllowedJson(finalReport)
         && !IsLowQualityAiJson(finalReport, hasRepositoryContext);

      if (isStrictReport)
      {
         const RepositoryFallbackFacts facts = CollectRepositoryFallbackFacts(submission);
         const std::string reportText = finalReport.dump();
         const std::vector<std::string> reportFiles = ExtractFileRefs(reportText);
         score = AddDeterministicScoreNuanceIfRounded(score, reportText, reportFiles.size(), facts);
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
      const std::filesystem::path datasetFile(datasetPath_);

      if (datasetFile.has_parent_path())
      {
         sitesConfigPath_ = (datasetFile.parent_path() / "plagiarism_sites.json").string();
      }
      else
      {
         sitesConfigPath_ = (std::filesystem::path("settings") / "plagiarism_sites.json").string();
      }
   }

   nlohmann::json PlagiarismService::Analyze(SubmissionItem& submission)
   {
      const auto dataset = ReadDataset();
      const RepositoryPlagiarismSnapshot repo = BuildRepositoryPlagiarismSnapshot(submission);
      const SiteRules siteRules = ReadSiteRulesFromFile(sitesConfigPath_);
      const std::vector<std::string> whitelistMatches = FindMatchedDomains(repo.rawTextLower, siteRules.whitelist);
      const std::vector<std::string> blacklistMatches = FindMatchedDomains(repo.rawTextLower, siteRules.blacklist);

      struct ScoredRow
      {
         double total = 0.0;
         double tokenScore = 0.0;
         double sentenceScore = 0.0;
         double metadataScore = 0.0;
         double ngramScore = 0.0;
         double rabinKarpScore = 0.0;
         bool rabinKarpHit = false;
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

         const double ngramScore = Jaccard(repo.trigrams, ds.trigrams);
         const bool rabinKarpHit = RabinKarpContains(repo.normalizedText, ds.normalizedText);
         const double rabinKarpScore = rabinKarpHit ? 1.0 : 0.0;

         const double totalScore = 0.45 * sentenceScore + 0.20 * tokenScore + 0.10 * metadataScore + 0.20 * ngramScore + 0.05 * rabinKarpScore;
         best = std::max(best, totalScore);

         ScoredRow rowScore;
         rowScore.total = totalScore;
         rowScore.tokenScore = tokenScore;
         rowScore.sentenceScore = sentenceScore;
         rowScore.metadataScore = metadataScore;
         rowScore.ngramScore = ngramScore;
         rowScore.rabinKarpScore = rabinKarpScore;
         rowScore.rabinKarpHit = rabinKarpHit;
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

      const double baseScore = best * 100.0;
      double sitePenalty = 0.0;

      if (!blacklistMatches.empty())
      {
         sitePenalty += std::min(20.0, static_cast<double>(blacklistMatches.size()) * 8.0);
      }

      if (!repo.hasSourceLinksAtEnd && (!whitelistMatches.empty() || !blacklistMatches.empty()))
      {
         sitePenalty += 10.0;
      }

      submission.plagiarismScore = std::min(100.0, baseScore + sitePenalty);

      nlohmann::json out;
      out["submissionId"] = submission.id;
      out["plagiarismScore"] = submission.plagiarismScore;
      out["baseScore"] = baseScore;
      out["sitePenalty"] = sitePenalty;
      out["algorithm"] = "metadata+identical-sentences+3gram+rabin-karp";
      out["dataset"] = "text-only";
      out["samplesChecked"] = dataset.size();
      out["siteRules"] = SiteRulesToJson(siteRules);
      out["siteMatches"] = {
         {"whitelist", whitelistMatches},
         {"blacklist", blacklistMatches}
      };
      out["metadata"] = {
         {"scopePath", repo.scopedPath},
         {"scopeMissing", repo.scopeMissing},
         {"textFilesChecked", repo.textFiles},
         {"totalBytes", repo.totalBytes},
         {"totalLines", repo.totalLines},
         {"sentencesIndexed", repo.sentences.size()},
         {"trigramsIndexed", repo.trigrams.size()},
         {"sourceLinksAtEnd", repo.hasSourceLinksAtEnd}
      };
      out["topMatches"] = nlohmann::json::array();

      for (size_t i = 0; i < scored.size() && i < 3; ++i)
      {
         nlohmann::json row;
         row["score"] = scored[i].total * 100.0;
         row["sentenceScore"] = scored[i].sentenceScore * 100.0;
         row["tokenScore"] = scored[i].tokenScore * 100.0;
         row["metadataScore"] = scored[i].metadataScore * 100.0;
         row["ngramScore"] = scored[i].ngramScore * 100.0;
         row["rabinKarpScore"] = scored[i].rabinKarpScore * 100.0;
         row["rabinKarpHit"] = scored[i].rabinKarpHit;
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

   nlohmann::json PlagiarismService::GetSitesConfig() const
   {
      nlohmann::json out = SiteRulesToJson(ReadSiteRulesFromFile(sitesConfigPath_));
      out["configPath"] = sitesConfigPath_;
      return out;
   }

   bool PlagiarismService::UpdateSitesConfig(const std::vector<std::string>& whitelist, const std::vector<std::string>& blacklist, std::string& error)
   {
      SiteRules rules;
      rules.whitelist = NormalizeSiteList(whitelist);
      rules.blacklist = NormalizeSiteList(blacklist);

      const nlohmann::json payload = SiteRulesToJson(rules);

      if (!WriteFileText(sitesConfigPath_, payload.dump(2)))
      {
         error = "Не вдалося зберегти файл зі списком сайтів.";
         return false;
      }

      return true;
   }

   bool PlagiarismService::ReplaceDataset(const std::string& datasetText, std::string& error, size_t& outRows)
   {
      std::istringstream input(datasetText);
      std::ostringstream normalized;
      std::string line;
      size_t rows = 0;

      while (std::getline(input, line))
      {
         const std::string trimmed = Trim(line);

         if (trimmed.empty())
         {
            continue;
         }

         if (rows > 0)
         {
            normalized << "\n";
         }

         normalized << trimmed;
         rows++;
      }

      if (rows == 0)
      {
         error = "Dataset порожній. Додайте хоча б один рядок.";
         return false;
      }

      if (!WriteFileText(datasetPath_, normalized.str()))
      {
         error = "Не вдалося зберегти dataset файл.";
         return false;
      }

      outRows = rows;
      return true;
   }

   std::vector<std::string> PlagiarismService::ReadDataset() const
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

      std::filesystem::path path = std::filesystem::path(OutboxDir());
      if (!submission.taskId.empty())
      {
         path /= submission.taskId;
      }
      path /= (submission.id + ".txt");
      std::filesystem::create_directories(path.parent_path());
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
