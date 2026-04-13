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

   private:
      std::vector<std::string> ReadDataset();
      std::set<std::string> Tokenize(const std::string& text);
      double Jaccard(const std::set<std::string>& a, const std::set<std::string>& b);

      std::string datasetPath_;
   };

   class FinalizationService
   {
   public:
      nlohmann::json Approve(SubmissionItem& submission, int grade, const std::string& feedback);
      nlohmann::json Send(SubmissionItem& submission);
   };
}