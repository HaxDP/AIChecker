#include "server/Domain.h"
#include "server/Services.h"
#include "server/Stores.h"
#include "server/Utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
   struct TestContext
   {
      int passed = 0;
      int failed = 0;
   };

   void Check(TestContext& ctx, bool condition, const std::string& message)
   {
      if (condition)
      {
         ++ctx.passed;
      }
      else
      {
         ++ctx.failed;
         std::cerr << "[FAIL] " << message << "\n";
      }
   }

   std::string MakeTempDataset()
   {
      const std::filesystem::path path = std::filesystem::temp_directory_path() / "aichecker_dataset_test.txt";
      std::ofstream out(path, std::ios::trunc);
      out << "github com student task\n";
      out << "for while class vector\n";
      out.close();
      return path.string();
   }

   backend::SubmissionItem MakeSubmission(const std::string& id, const std::string& taskId, const std::string& url)
   {
      backend::SubmissionItem s;
      s.id = id;
      s.taskId = taskId;
      s.studentName = "Student " + id;
      s.studentEmail = "student@example.com";
      s.repositoryUrl = url;
      s.status = "completed";
      return s;
   }

   void TestRuntimeDirectories(TestContext& ctx)
   {
      const auto root = backend::GetAppDataRoot();
      const auto sessions = backend::SessionDir();
      const auto cache = backend::CacheDir();
      const auto repos = backend::RepoCacheDir();
      const auto outbox = backend::OutboxDir();

      Check(ctx, !root.empty(), "GetAppDataRoot should not be empty");
      Check(ctx, std::filesystem::exists(root), "App data root should exist");
      Check(ctx, std::filesystem::exists(sessions), "Session directory should exist");
      Check(ctx, std::filesystem::exists(cache), "Cache directory should exist");
      Check(ctx, std::filesystem::exists(repos), "Repo cache directory should exist");
      Check(ctx, std::filesystem::exists(outbox), "Outbox directory should exist");
   }

   void TestReadWriteFileHelpers(TestContext& ctx)
   {
      const std::filesystem::path testPath = std::filesystem::path(backend::CacheDir()) / "unit_rw_test.txt";
      const std::string body = "hello unit test";

      Check(ctx, backend::WriteFileText(testPath.string(), body), "WriteFileText should succeed");
      Check(ctx, backend::ReadFileText(testPath.string()) == body, "ReadFileText should return written data");
   }

   void TestSessionStoreRoundtrip(TestContext& ctx)
   {
      backend::LocalSessionStore store;
      backend::GoogleAccount account;
      account.email = "teacher@example.com";
      account.name = "Teacher";

      const std::string sid = store.Create(account, "token-123");
      Check(ctx, !sid.empty(), "Session ID should be generated");

      const auto session = store.Get(sid);
      Check(ctx, session.has_value(), "Created session should be retrievable");

      if (session.has_value())
      {
         Check(ctx, session->email == "teacher@example.com", "Session email should match input");
         Check(ctx, session->accessToken == "token-123", "Session token should match input");
      }

      store.Remove(sid);
      Check(ctx, !store.Get(sid).has_value(), "Session should be removed");
   }

   void TestCacheStoreWritesJson(TestContext& ctx)
   {
      backend::LocalCacheStore cache;
      const std::string key = "unit_cache_payload";
      nlohmann::json payload = {
         {"ok", true},
         {"value", 42}
      };

      cache.Set(key, payload);
      const std::filesystem::path cacheFile = std::filesystem::path(backend::CacheDir()) / (key + ".json");
      Check(ctx, std::filesystem::exists(cacheFile), "Cache JSON file should exist");

      const auto raw = backend::ReadFileText(cacheFile.string());
      Check(ctx, !raw.empty(), "Cache JSON content should not be empty");
   }

   void TestDataStoreReplaceAndQueries(TestContext& ctx)
   {
      backend::DataStore store;

      std::vector<backend::ClassItem> classes = {
         {"c1", "Class A"},
         {"c2", "Class B"}
      };
      store.ReplaceClasses(classes);
      Check(ctx, store.Classes().size() == 2, "ReplaceClasses should store all classes");

      std::vector<backend::TaskItem> tasks = {
         {"t1", "c1", "Task 1"},
         {"t2", "c1", "Task 2"}
      };
      store.ReplaceTasksForClass("c1", tasks);
      Check(ctx, store.TasksByClass("c1").size() == 2, "TasksByClass should return class tasks");

      std::vector<backend::SubmissionItem> submissions = {
         MakeSubmission("s1", "t1", "https://github.com/a/b"),
         MakeSubmission("s2", "t1", "https://github.com/a/c")
      };
      store.ReplaceSubmissionsForTask("t1", submissions);
      Check(ctx, store.SubmissionsByTask("t1").size() == 2, "SubmissionsByTask should return task submissions");
      Check(ctx, store.SubmissionById("s1") != nullptr, "SubmissionById should find existing submission");

      const auto stats = store.TaskStats("t1");
      Check(ctx, stats.value("completed", -1) >= 0, "TaskStats should contain completed field");
   }

   void TestDataStoreKeepsReviewFieldsOnRefresh(TestContext& ctx)
   {
      backend::DataStore store;
      std::vector<backend::SubmissionItem> initial = { MakeSubmission("s10", "t10", "https://github.com/x/y") };
      store.ReplaceSubmissionsForTask("t10", initial);

      auto* current = store.SubmissionById("s10");
      Check(ctx, current != nullptr, "Submission should exist before refresh");
      if (current == nullptr)
      {
         return;
      }

      current->aiScore = 77.0;
      current->plagiarismScore = 22.0;
      current->grade = 95;
      current->approved = true;
      current->sent = true;
      current->feedback = "ok";
      current->aiReport = "report";

      std::vector<backend::SubmissionItem> refreshed = { MakeSubmission("s10", "t10", "https://github.com/x/y") };
      store.ReplaceSubmissionsForTask("t10", refreshed);

      auto* after = store.SubmissionById("s10");
      Check(ctx, after != nullptr, "Submission should exist after refresh");
      if (after == nullptr)
      {
         return;
      }

      Check(ctx, after->aiScore == 77.0, "aiScore should be preserved");
      Check(ctx, after->plagiarismScore == 22.0, "plagiarismScore should be preserved");
      Check(ctx, after->grade == 95, "grade should be preserved");
      Check(ctx, after->approved, "approved should be preserved");
      Check(ctx, after->sent, "sent should be preserved");
      Check(ctx, after->feedback == "ok", "feedback should be preserved");
      Check(ctx, after->aiReport == "report", "aiReport should be preserved");
   }

   void TestPlagiarismAnalyze(TestContext& ctx)
   {
      backend::PlagiarismService service(MakeTempDataset());
      auto submission = MakeSubmission("p1", "tp", "https://github.com/student/task");
      auto result = service.Analyze(submission);

      const double score = result.value("plagiarismScore", -1.0);
      Check(ctx, score >= 0.0 && score <= 100.0, "Plagiarism score should be in [0,100]");
      Check(ctx, submission.plagiarismScore == score, "Submission plagiarismScore should be updated");
   }

   void TestFinalizationApproveAndSend(TestContext& ctx)
   {
      backend::FinalizationService service;
      auto submission = MakeSubmission("f1", "tf", "https://github.com/student/task");
      submission.aiScore = 31.0;
      submission.plagiarismScore = 17.0;

      auto approve = service.Approve(submission, 88, "good work");
      Check(ctx, approve.value("approved", false), "Approve should set approved true");
      Check(ctx, submission.grade == 88, "Approve should set grade");

      auto send = service.Send(submission);
      Check(ctx, send.value("ok", false), "Send should succeed after approval");
      Check(ctx, submission.sent, "Send should set submission.sent");

      const std::filesystem::path outboxFile = std::filesystem::path(backend::OutboxDir()) / "f1.txt";
      Check(ctx, std::filesystem::exists(outboxFile), "Send should create outbox file");
   }

   void TestFinalizationSendRejectedWhenNotApproved(TestContext& ctx)
   {
      backend::FinalizationService service;
      auto submission = MakeSubmission("f2", "tf", "https://github.com/student/task");

      auto send = service.Send(submission);
      Check(ctx, !send.value("ok", true), "Send should fail when submission is not approved");
      Check(ctx, !submission.sent, "sent flag should remain false for rejected send");
   }

   void TestAiAnalyzeFallback(TestContext& ctx)
   {
      backend::AiReviewService service;
      auto submission = MakeSubmission("ai1", "ta", "https://github.com/student/task");

      auto result = service.Analyze(submission, "aichecker", 0.2);
      const double score = result.value("aiScore", -1.0);
      const std::string decision = result.value("decision", "");

      Check(ctx, score >= 0.0 && score <= 100.0, "AI score should be in [0,100]");
      Check(ctx, !decision.empty(), "AI decision should be present");
      Check(ctx, submission.aiScore == score, "Submission aiScore should be updated");
   }
}

int main(int argc, char** argv)
{
   using TestFn = void (*)(TestContext&);
   const std::vector<std::pair<std::string, TestFn>> tests = {
      {"RuntimeDirectories", TestRuntimeDirectories},
      {"ReadWriteFileHelpers", TestReadWriteFileHelpers},
      {"SessionStoreRoundtrip", TestSessionStoreRoundtrip},
      {"CacheStoreWritesJson", TestCacheStoreWritesJson},
      {"DataStoreReplaceAndQueries", TestDataStoreReplaceAndQueries},
      {"DataStoreKeepsReviewFieldsOnRefresh", TestDataStoreKeepsReviewFieldsOnRefresh},
      {"PlagiarismAnalyze", TestPlagiarismAnalyze},
      {"FinalizationApproveAndSend", TestFinalizationApproveAndSend},
      {"FinalizationSendRejectedWhenNotApproved", TestFinalizationSendRejectedWhenNotApproved},
      {"AiAnalyzeFallback", TestAiAnalyzeFallback}
   };

   if (argc > 1)
   {
      const std::string mode = argv[1];

      if (mode == "--list")
      {
         for (const auto& [name, _] : tests)
         {
            (void)_;
            std::cout << name << "\n";
         }
         return 0;
      }

      if (mode == "--run" && argc > 2)
      {
         const std::string name = argv[2];
         TestContext single;

         for (const auto& [testName, fn] : tests)
         {
            if (testName == name)
            {
               fn(single);
               std::cout << "Passed: " << single.passed << "\n";
               std::cout << "Failed: " << single.failed << "\n";
               return single.failed == 0 ? 0 : 1;
            }
         }

         std::cerr << "Unknown test: " << name << "\n";
         return 2;
      }
   }

   TestContext ctx;
   for (const auto& [_, fn] : tests)
   {
      (void)_;
      fn(ctx);
   }

   std::cout << "Passed: " << ctx.passed << "\n";
   std::cout << "Failed: " << ctx.failed << "\n";

   return ctx.failed == 0 ? 0 : 1;
}