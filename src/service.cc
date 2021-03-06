#include <iostream>
#include <map>

#include <absl/hash/hash.h>

#include "service.h"

namespace bpfki {

BPFKIServiceImpl::BPFKIServiceImpl(const std::shared_ptr<spdlog::logger>& logger)
  : logger_(logger) {
}

BPFKIServiceImpl::~BPFKIServiceImpl() {

}

grpc::Status BPFKIServiceImpl::SetTime(const std::string& type,
                                       const BumpTimeRequest* request,
                                       StatusResponse* response) {
  std::unique_ptr<BPFKI> bpfki = nullptr;
  auto it = bpfki_map_.find(type);
  if (it == bpfki_map_.end()) {
    bpfki = createBPFKI("BumpTime", logger_, type);
    try {
      bpfki->init();
    } catch (const std::invalid_argument& ia) {
      logger_->error("Internal error invalid argument: {}", ia.what());
      return grpc::Status(grpc::StatusCode::INTERNAL, ia.what());
    }
  } else {
    bpfki = std::move(it->second);
  }
  try {
    auto left_nr_filters = bpfki->update_inject_cond(
      reinterpret_cast<const void*>(request));
    logger_->info("left_nr_filters: {}", left_nr_filters);
  } catch (const std::invalid_argument& ia) {
    logger_->error("Internal error invalid argument: {}", ia.what());
    return grpc::Status(grpc::StatusCode::INTERNAL, ia.what());
  }
  bpfki_map_[type] = std::move(bpfki);
  return grpc::Status::OK;
}

grpc::Status BPFKIServiceImpl::RecoverTime(const std::string& type,
                                           const BumpTimeRequest *request,
                                           StatusResponse* response) {
  auto it = bpfki_map_.find(type);
  if (it == bpfki_map_.end())
    return grpc::Status::OK;
  try {
    auto left_nr_filters = it->second->update_inject_cond(
      reinterpret_cast<const void*>(request), true);
    logger_->info("left_nr_filters: {}", left_nr_filters);
    if (left_nr_filters == 0)
      bpfki_map_.erase(it);
  } catch (const std::invalid_argument& ia) {
    logger_->error("Internal error invalid argument: {}", ia.what());
    return grpc::Status(grpc::StatusCode::INTERNAL, ia.what());
  }
  return grpc::Status::OK;
}

grpc::Status BPFKIServiceImpl::SetTimeVal(grpc::ServerContext* context,
                                          const BumpTimeRequest* request,
                                          StatusResponse* response) {
  std::lock_guard<std::mutex> guard(bpfki_map_mtx_);
  return SetTime("gettimeofday", request, response);
}

grpc::Status BPFKIServiceImpl::RecoverTimeVal(grpc::ServerContext* context,
                                              const BumpTimeRequest* request,
                                              StatusResponse* response) {
  std::lock_guard<std::mutex> guard(bpfki_map_mtx_);
  return RecoverTime("gettimeofday", request, response);
}

grpc::Status BPFKIServiceImpl::SetTimeSpec(grpc::ServerContext* context,
                                           const BumpTimeRequest* request,
                                           StatusResponse* response) {
  std::lock_guard<std::mutex> guard(bpfki_map_mtx_);
  return RecoverTime("clock_gettime", request, response);
}

grpc::Status BPFKIServiceImpl::RecoverTimeSpec(grpc::ServerContext* context,
                                               const BumpTimeRequest* request,
                                               StatusResponse* response) {
  std::lock_guard<std::mutex> guard(bpfki_map_mtx_);
  return RecoverTime("clock_gettime", request, response);
}

grpc::Status BPFKIServiceImpl::FailMMOrBIO(grpc::ServerContext *context,
                                           const FailKernRequest *request,
                                           StatusResponse *response) {
  std::vector<std::string> headers;
  int header_size = request->headers_size();
  for (int i = 0; i < header_size; i++) {
    headers.push_back(request->headers(i));
  }
  std::vector<FailKern::Frame> callchain;
  int callchain_size = request->callchain_size();
  for (int i = 0; i < callchain_size; i++) {
    auto frame = request->callchain(i);
    if (i != callchain_size - 1 && frame.funcname() == "")
      throw std::invalid_argument("");
    callchain.push_back(FailKern::Frame(frame.funcname(),
                                        frame.parameters(),
                                        frame.predicate()));
  }
  auto k = std::to_string(absl::Hash<std::vector<FailKern::Frame>>{}(callchain));
  std::unique_ptr<BPFKI> bpfki = nullptr;
  std::lock_guard<std::mutex> guard(bpfki_map_mtx_);
  auto it = bpfki_map_.find(k);
  if (it == bpfki_map_.end()) {
    auto type = static_cast<const FailKern::Type>(request->ftype());
    bpfki = createBPFKI("FailKern", logger_, type, headers, callchain);
    try {
      bpfki->init();
    } catch(const std::invalid_argument& ia) {
      logger_->error("Internal error invalid argument: {}", ia.what());
      return grpc::Status(grpc::StatusCode::INTERNAL, ia.what());
    }
  } else {
    bpfki = std::move(it->second);
  }
  try {
    auto left_nr_filters = bpfki->update_inject_cond(
      reinterpret_cast<const void*>(request));
    logger_->info("left_nr_filters: {}", left_nr_filters);
  } catch (const std::invalid_argument& ia) {
    logger_->error("Internal error invalid argument: {}", ia.what());
    return grpc::Status(grpc::StatusCode::INTERNAL, ia.what());
  }
  bpfki_map_[k] = std::move(bpfki);
  return grpc::Status::OK;
}

grpc::Status BPFKIServiceImpl::RecoverMMOrBIO(grpc::ServerContext* context,
                                              const FailKernRequest* request,
                                              StatusResponse* response) {
  std::vector<FailKern::Frame> callchain;
  int callchain_size = request->callchain_size();
  for (int i = 0; i < callchain_size; i++) {
    auto frame = request->callchain(i);
    if (i != callchain_size - 1 && frame.funcname() == "")
      throw std::invalid_argument("");
    callchain.push_back(FailKern::Frame(frame.funcname(),
                                        frame.parameters(),
                                        frame.predicate()));
  }
  auto k = std::to_string(absl::Hash<std::vector<FailKern::Frame>>{}(callchain));
  std::unique_ptr<BPFKI> bpfki = nullptr;
  std::lock_guard<std::mutex> guard(bpfki_map_mtx_);
  auto it = bpfki_map_.find(k);
  if (it == bpfki_map_.end())
    return grpc::Status::OK;
  try {
    auto left_nr_filters = it->second->update_inject_cond(
      reinterpret_cast<const void*>(request), true);
    logger_->info("left_nr_filters: {}", left_nr_filters);
    if (left_nr_filters == 0)
      bpfki_map_.erase(it);
  } catch (const std::invalid_argument& ia) {
    logger_->error("Internal error invalid argument: {}", ia.what());
    return grpc::Status(grpc::StatusCode::INTERNAL, ia.what());
  }
  return grpc::Status::OK;
}

grpc::Status BPFKIServiceImpl::FailSyscall(grpc::ServerContext* context,
                                           const FailSyscallRequest* request,
                                           StatusResponse* response) {
  return grpc::Status::OK;
}

grpc::Status BPFKIServiceImpl::RecoverSyscall(grpc::ServerContext* context,
                                              const FailSyscallRequest* request,
                                              StatusResponse* response) {
  return grpc::Status::OK;
}

};
