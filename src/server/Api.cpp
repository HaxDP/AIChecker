#include "server/Api.h"

#include "server/Controllers.h"
#include "server/Domain.h"
#include "server/Services.h"
#include "server/Stores.h"

#include <httplib.h>
#include <filesystem>

namespace backend
{
   namespace
   {
      std::string ResolveSiteRoot()
      {
         const std::filesystem::path direct = std::filesystem::current_path() / "site";

         if (std::filesystem::exists(direct / "index.html"))
         {
            return direct.string();
         }

         const std::filesystem::path parent = std::filesystem::current_path().parent_path() / "site";

         if (std::filesystem::exists(parent / "index.html"))
         {
            return parent.string();
         }

         return direct.string();
      }
   }

   void WebApiApp::Run(int port)
   {
      const std::string siteRoot = ResolveSiteRoot();
      std::filesystem::create_directories(siteRoot);

      LocalSessionStore sessions;
      LocalCacheStore cache;
      DataStore data;
      RepositoryService repoService;
      AiReviewService aiService;
      PlagiarismService plagiarismService((std::filesystem::path("settings") / "plagiarism_dataset.txt").string());
      FinalizationService finalService;

      std::vector<GoogleAccount> accounts = {
         {"teacher1@gmail.com", "Викладач 1"},
         {"teacher2@gmail.com", "Викладач 2"},
         {"teacher3@gmail.com", "Викладач 3"}
      };

      ApiDependencies deps {
         sessions,
         cache,
         data,
         repoService,
         aiService,
         plagiarismService,
         finalService,
         accounts
      };

      httplib::Server server;
      server.set_mount_point("/", siteRoot);

      RegisterSystemRoutes(server, siteRoot);
      RegisterAuthRoutes(server, deps);
      RegisterClassroomRoutes(server, deps);
      RegisterReviewRoutes(server, deps);

      server.listen("0.0.0.0", port);
   }
}
