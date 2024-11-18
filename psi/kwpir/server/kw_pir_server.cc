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

#include "psi/kwpir/server/kw_pir_server.h"

#include <chrono>

#include "yacl/base/exception.h"
#include "yacl/utils/elapsed_timer.h"

namespace psi::kwpir {

void KeywordPirServer::SetUp() {
  yacl::ElapsedTimer timer;

  if (input_.file_name().empty()) {
    DoLoadDataBase(db_path_);
  } else {
    auto provider = std::make_shared<InputProvider>(input_);
    DoGenerateDataBase(provider, db_path_);
  }

  SPDLOG_INFO("SetUp cost: {} ms", timer.CountMs());
  is_ready_ = true;
}

void KeywordPirServer::Query(::google::protobuf::RpcController* /*cntl_base*/,
                             const KeywordPirServerRequest* request,
                             KeywordPirServerResponse* response,
                             ::google::protobuf::Closure* done) {
  brpc::ClosureGuard done_guard(done);

  if (!is_ready_) {
    response->mutable_status()->set_err_code(ErrorCode::NOT_READY);
    response->mutable_status()->set_msg("Server is not ready");
  }

  SPDLOG_INFO("request step: {}", request->step());
  try {
    DoQuery(request, response);
  } catch (yacl::Exception& e) {
    response->mutable_status()->set_err_code(ErrorCode::LOGIC_ERROR);
    response->mutable_status()->set_msg(e.what());
  } catch (const std::exception& e) {
    response->mutable_status()->set_err_code(ErrorCode::UNEXPECTED_ERROR);
    response->mutable_status()->set_msg(e.what());
    return;
  }

  response->mutable_status()->set_err_code(ErrorCode::OK);
}

}  // namespace psi::kwpir