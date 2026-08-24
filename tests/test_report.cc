#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "src/context/context.h"
#include "src/database/database.h"
#include "src/report/buried_report.h"

namespace {

using namespace std::chrono_literals;

class TempDirectory {
 public:
  explicit TempDirectory(const std::string& prefix) {
    static std::atomic<unsigned long long> next_id{0};
    auto id = next_id.fetch_add(1);
    auto timestamp = std::chrono::steady_clock::now()
                         .time_since_epoch()
                         .count();
    path_ = std::filesystem::temp_directory_path() /
            (prefix + "_" + std::to_string(timestamp) + "_" +
             std::to_string(id));
    std::filesystem::create_directories(path_);
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

buried::CommonService MakeCommonService() {
  buried::CommonService service;
  service.host = "127.0.0.1";
  service.port = "1";
  service.topic = "/buried";
  service.user_id = "test-user";
  service.app_name = "strand-test";
  service.app_version = "1.0.0";
  service.custom_data = nlohmann::json::object();
  return service;
}

}  // namespace

TEST(BuriedReportConcurrencyTest, ConcurrentInsertPersistsEveryRow) {
  constexpr int kProducerCount = 8;
  constexpr int kRowsPerProducer = 25;
  constexpr int kRowCount = kProducerCount * kRowsPerProducer;

  buried::Context::GetGlobalContext().Start();
  TempDirectory directory("buried_report_concurrent");
  {
    buried::BuriedReport report(nullptr, MakeCommonService(),
                                directory.Path().string());
    std::vector<std::thread> producers;
    for (int producer = 0; producer < kProducerCount; ++producer) {
      producers.emplace_back([&, producer]() {
        for (int sequence = 0; sequence < kRowsPerProducer; ++sequence) {
          buried::BuriedData data;
          data.title = "producer-" + std::to_string(producer);
          data.data = "row-" + std::to_string(sequence);
          data.priority = producer * kRowsPerProducer + sequence;
          report.InsertData(data);
        }
      });
    }
    for (auto& producer : producers) {
      producer.join();
    }
  }

  buried::BuriedDb database((directory.Path() / "buried.db").string());
  auto rows = database.QueryData(kRowCount + 1);
  EXPECT_EQ(rows.size(), kRowCount);
}

TEST(BuriedReportConcurrencyTest, DestroyWithPendingWorkIsSafe) {
  buried::Context::GetGlobalContext().Start();
  TempDirectory directory("buried_report_destroy");
  auto started_at = std::chrono::steady_clock::now();
  {
    buried::BuriedReport report(nullptr, MakeCommonService(),
                                directory.Path().string());
    report.Start();
    report.InsertData({"title", "data", 1});
  }
  auto elapsed = std::chrono::steady_clock::now() - started_at;

  EXPECT_LT(elapsed, 2s);
  buried::BuriedDb database((directory.Path() / "buried.db").string());
  auto rows = database.QueryData(2);
  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(rows.front().priority, 1);
}
