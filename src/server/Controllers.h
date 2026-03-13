#pragma once

#include "server/Domain.h"
#include "server/Services.h"
#include "server/Stores.h"

#include <httplib.h>

#include <vector>

namespace backend
{
   struct ApiDependencies
   {
      LocalSessionStore& sessions;
      LocalCacheStore& cache;
      DataStore& data;
      RepositoryService& repoService;
      AiReviewService& aiService;
      PlagiarismService& plagiarismService;
      FinalizationService& finalService;
      const std::vector<GoogleAccount>& accounts;
   };

   void RegisterSystemRoutes(httplib::Server& server, const std::string& siteRoot);
   void RegisterAuthRoutes(httplib::Server& server, ApiDependencies& deps);
   void RegisterClassroomRoutes(httplib::Server& server, ApiDependencies& deps);
   void RegisterReviewRoutes(httplib::Server& server, ApiDependencies& deps);
}
