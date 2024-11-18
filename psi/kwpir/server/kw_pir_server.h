// Copyright 2024 Ant Group Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <memory>

#include "brpc/server.h"
#include "spdlog/spdlog.h"

#include "psi/kwpir/common/input_provider.h"

#include "psi/proto/kw_pir_server_service.pb.h"

namespace psi::kwpir {

class KeywordPirServer : public KeywordPirServerService {
 public:
  KeywordPirServer(InputConfig input, std::string db_path)
      : input_(std::move(input)), db_path_(std::move(db_path)) {}

  ~KeywordPirServer() override = default;

  void Query(::google::protobuf::RpcController* /*cntl_base*/,
             const KeywordPirServerRequest* request,
             KeywordPirServerResponse* response,
             ::google::protobuf::Closure* done) override;

  void SetUp();

 protected:
  virtual void DoQuery(const KeywordPirServerRequest* request,
                       KeywordPirServerResponse* response) = 0;

  virtual void DoGenerateDataBase(std::shared_ptr<InputProvider> input,
                                  const std::string& db_path) = 0;

  virtual void DoLoadDataBase(const std::string& db_path) = 0;

 protected:
  InputConfig input_;
  std::string db_path_;

 private:
  bool is_ready_ = false;
};

}  // namespace psi::kwpir