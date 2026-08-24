#include "report/buried_report.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "boost/asio/deadline_timer.hpp"
#include "boost/asio/error.hpp"
#include "context/context.h"
#include "crypt/crypt.h"
#include "database/database.h"
#include "report/http_report.h"
#include "spdlog/spdlog.h"

namespace buried {

static const char kDbName[] = "buried.db";

class BuriedReportState
    : public std::enable_shared_from_this<BuriedReportState> {
 public:
  static std::shared_ptr<BuriedReportState> Create(
      std::shared_ptr<spdlog::logger> logger, CommonService common_service,
      std::string work_path) {
    Context::GetGlobalContext().Start();
    auto state = std::shared_ptr<BuriedReportState>(new BuriedReportState(
        std::move(logger), std::move(common_service), std::move(work_path)));
    state->InitializeAsync_();
    return state;
  }

  void Start();

  void InsertData(const BuriedData& data);

  void Shutdown() noexcept;

 private:
  BuriedReportState(std::shared_ptr<spdlog::logger> logger,
                    CommonService common_service, std::string work_path)
      : logger_(std::move(logger)),
        common_service_(std::move(common_service)),
        work_dir_(std::move(work_path)) {
    if (logger_ == nullptr) {
      logger_ = spdlog::default_logger();
    }
    std::string key = AESCrypt::GetKey("buried_salt", "buried_password");
    crypt_ = std::make_unique<AESCrypt>(key);
    SPDLOG_LOGGER_INFO(logger_, "BuriedReportState init success");
  }

  void InitializeAsync_();

  void Init_();

  void Start_();

  void CancelTimer_();

  void ScheduleNextCycle_();

  void OnTimer_(const boost::system::error_code& error);

  void ReportCache_();

  BuriedDb::Data MakeDbData_(const BuriedData& data);

  std::string GenReportData_(const std::vector<BuriedDb::Data>& datas);

  bool ReportData_(const std::string& data);

  std::shared_ptr<spdlog::logger> logger_;
  std::string work_dir_;
  std::unique_ptr<BuriedDb> db_;
  CommonService common_service_;
  std::unique_ptr<buried::Crypt> crypt_;
  std::unique_ptr<boost::asio::deadline_timer> timer_;
  std::vector<BuriedDb::Data> data_caches_;

  std::mutex submit_mutex_;
  bool accepting_{true};
  bool start_requested_{false};
  std::atomic<bool> stopping_{false};
};

class BuriedReportImpl {
 public:
  BuriedReportImpl(std::shared_ptr<spdlog::logger> logger,
                   CommonService common_service, std::string work_path)
      : state_(BuriedReportState::Create(
            std::move(logger), std::move(common_service),
            std::move(work_path))) {}

  ~BuriedReportImpl() { state_->Shutdown(); }

  void Start() { state_->Start(); }

  void InsertData(const BuriedData& data) { state_->InsertData(data); }

 private:
  std::shared_ptr<BuriedReportState> state_;
};

void BuriedReportState::InitializeAsync_() {
  auto self = shared_from_this();
  if (!Context::GetGlobalContext().GetReportStrand().Post(
          [self = std::move(self)]() { self->Init_(); })) {
    throw std::runtime_error("report strand is closed");
  }
}

void BuriedReportState::Init_() {
  try {
    std::filesystem::path db_path = work_dir_;
    SPDLOG_LOGGER_INFO(logger_, "BuriedReportState init db path: {}",
                       db_path.string());
    db_path /= kDbName;
    db_ = std::make_unique<BuriedDb>(db_path.string());
  } catch (const std::exception& error) {
    SPDLOG_LOGGER_ERROR(logger_, "BuriedReportState init db error: {}",
                        error.what());
  } catch (...) {
    SPDLOG_LOGGER_ERROR(logger_,
                        "BuriedReportState init db unknown error");
  }
}

void BuriedReportState::Start() {
  std::lock_guard<std::mutex> lock(submit_mutex_);
  if (!accepting_ || start_requested_) {
    return;
  }

  start_requested_ = true;
  auto self = shared_from_this();
  if (!Context::GetGlobalContext().GetReportStrand().Post(
          [self = std::move(self)]() { self->Start_(); })) {
    start_requested_ = false;
  }
}

void BuriedReportState::Start_() {
  if (stopping_.load()) {
    return;
  }
  if (!db_) {
    SPDLOG_LOGGER_ERROR(logger_,
                        "BuriedReportState cannot start without database");
    return;
  }

  SPDLOG_LOGGER_INFO(logger_, "BuriedReportState start");
  timer_ = std::make_unique<boost::asio::deadline_timer>(
      Context::GetGlobalContext().GetReportContext());
  ScheduleNextCycle_();
}

void BuriedReportState::InsertData(const BuriedData& data) {
  std::lock_guard<std::mutex> lock(submit_mutex_);
  if (!accepting_) {
    return;
  }

  auto self = shared_from_this();
  if (!Context::GetGlobalContext().GetReportStrand().Post(
          [self = std::move(self), data]() {
            if (!self->db_) {
              SPDLOG_LOGGER_ERROR(
                  self->logger_,
                  "BuriedReportState cannot insert without database");
              return;
            }
            self->db_->InsertData(self->MakeDbData_(data));
          })) {
    SPDLOG_LOGGER_ERROR(logger_,
                        "BuriedReportState rejected insert during shutdown");
  }
}

void BuriedReportState::Shutdown() noexcept {
  auto& strand = Context::GetGlobalContext().GetReportStrand();
  std::shared_ptr<std::promise<void>> completion;
  std::future<void> completion_future;
  bool posted = false;

  try {
    {
      std::lock_guard<std::mutex> lock(submit_mutex_);
      if (!accepting_) {
        return;
      }
      accepting_ = false;
      stopping_.store(true);

      if (strand.RunningInThisThread()) {
        CancelTimer_();
        return;
      }

      completion = std::make_shared<std::promise<void>>();
      completion_future = completion->get_future();
      auto self = shared_from_this();
      posted = strand.Post([self = std::move(self), completion]() {
        self->CancelTimer_();
        completion->set_value();
      });
    }

    if (posted) {
      // The completion task is ordered after every accepted submission on the
      // report strand. Returning earlier would allow callers to read or remove
      // the database while those submissions are still running.
      completion_future.wait();
    }
  } catch (const std::exception& error) {
    SPDLOG_LOGGER_ERROR(logger_, "BuriedReportState shutdown error: {}",
                        error.what());
  } catch (...) {
    SPDLOG_LOGGER_ERROR(logger_,
                        "BuriedReportState shutdown unknown error");
  }
}

void BuriedReportState::CancelTimer_() {
  if (!timer_) {
    return;
  }

  boost::system::error_code error;
  timer_->cancel(error);
  timer_.reset();
  if (error) {
    SPDLOG_LOGGER_ERROR(logger_, "BuriedReportState cancel timer error: {}",
                        error.message());
  }
}

void BuriedReportState::ScheduleNextCycle_() {
  if (stopping_.load() || !timer_) {
    return;
  }

  timer_->expires_from_now(boost::posix_time::seconds(5));
  std::weak_ptr<BuriedReportState> weak_self = weak_from_this();
  timer_->async_wait([weak_self](const boost::system::error_code& error) {
    auto self = weak_self.lock();
    if (!self) {
      return;
    }

    Context::GetGlobalContext().GetReportStrand().Post(
        [self = std::move(self), error]() { self->OnTimer_(error); });
  });
}

void BuriedReportState::OnTimer_(const boost::system::error_code& error) {
  if (stopping_.load()) {
    return;
  }
  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      SPDLOG_LOGGER_ERROR(logger_, "BuriedReportState timer error: {}",
                          error.message());
    }
    return;
  }

  ReportCache_();
}

bool BuriedReportState::ReportData_(const std::string& data) {
  HttpReporter reporter(logger_);
  return reporter.Host(common_service_.host)
      .Topic(common_service_.topic)
      .Port(common_service_.port)
      .Body(data)
      .Report();
}

void BuriedReportState::ReportCache_() {
  if (!db_) {
    SPDLOG_LOGGER_ERROR(logger_,
                        "BuriedReportState cannot report without database");
    return;
  }

  SPDLOG_LOGGER_INFO(logger_, "BuriedReportState report cache");
  if (data_caches_.empty()) {
    data_caches_ = db_->QueryData(10);
  }

  if (!data_caches_.empty()) {
    std::string report_data = GenReportData_(data_caches_);
    if (ReportData_(report_data)) {
      db_->DeleteDatas(data_caches_);
      data_caches_.clear();
    }
  }

  ScheduleNextCycle_();
}

std::string BuriedReportState::GenReportData_(
    const std::vector<BuriedDb::Data>& datas) {
  nlohmann::json json_datas;
  for (const auto& data : datas) {
    std::string content =
        crypt_->Decrypt(data.content.data(), data.content.size());
    SPDLOG_LOGGER_INFO(logger_, "BuriedReportState report data content size: {}",
                       data.content.size());
    json_datas.push_back(content);
  }
  return json_datas.dump();
}

BuriedDb::Data BuriedReportState::MakeDbData_(const BuriedData& data) {
  BuriedDb::Data db_data;
  db_data.id = -1;
  db_data.priority = data.priority;
  db_data.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  nlohmann::json json_data;
  json_data["title"] = data.title;
  json_data["data"] = data.data;
  json_data["user_id"] = common_service_.user_id;
  json_data["app_version"] = common_service_.app_version;
  json_data["app_name"] = common_service_.app_name;
  json_data["custom_data"] = common_service_.custom_data;
  json_data["system_version"] = common_service_.system_version;
  json_data["device_name"] = common_service_.device_name;
  json_data["device_id"] = common_service_.device_id;
  json_data["buried_version"] = common_service_.buried_version;
  json_data["lifecycle_id"] = common_service_.lifecycle_id;
  json_data["priority"] = data.priority;
  json_data["timestamp"] = CommonService::GetNowDate();
  json_data["process_time"] = CommonService::GetProcessTime();
  json_data["report_id"] = CommonService::GetRandomId();
  std::string report_data = crypt_->Encrypt(json_data.dump());
  db_data.content = std::vector<char>(report_data.begin(), report_data.end());
  SPDLOG_LOGGER_INFO(logger_, "BuriedReportState insert data size: {}",
                     db_data.content.size());
  return db_data;
}

BuriedReport::BuriedReport(std::shared_ptr<spdlog::logger> logger,
                           CommonService common_service, std::string work_path)
    : impl_(std::make_unique<BuriedReportImpl>(
          std::move(logger), std::move(common_service), std::move(work_path))) {
}

void BuriedReport::Start() { impl_->Start(); }

void BuriedReport::InsertData(const BuriedData& data) {
  impl_->InsertData(data);
}

BuriedReport::~BuriedReport() = default;

}  // namespace buried
