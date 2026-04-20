#pragma once

#include "server/Domain.h"

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <vector>

namespace backend
{
   class RepositoryService
   {
   public:
      nlohmann::json Pull(const std::vector<SubmissionItem*>& submissions);
   };

   class AiReviewService
   {
   public:
      nlohmann::json Analyze(SubmissionItem& submission, const std::string& model, double temperature);

   private:
      double Heuristic(const SubmissionItem& submission);
      double ExtractScore(const std::string& text, double fallback);
   };

   class PlagiarismService
   {
   public:
      explicit PlagiarismService(std::string datasetPath);
      nlohmann::json Analyze(SubmissionItem& submission);
      nlohmann::json GetSitesConfig() const;
      bool UpdateSitesConfig(const std::vector<std::string>& whitelist, const std::vector<std::string>& blacklist, std::string& error);
      bool ReplaceDataset(const std::string& datasetText, std::string& error, size_t& outRows);

   private:
      std::vector<std::string> ReadDataset() const;
      std::set<std::string> Tokenize(const std::string& text);
      double Jaccard(const std::set<std::string>& a, const std::set<std::string>& b);

      std::string datasetPath_;
      std::string sitesConfigPath_;
   };

   class FinalizationService
   {
   public:
      nlohmann::json Approve(SubmissionItem& submission, int grade, const std::string& feedback);
      nlohmann::json Send(SubmissionItem& submission);
   };
}
