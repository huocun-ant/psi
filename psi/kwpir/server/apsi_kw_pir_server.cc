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

#include "psi/kwpir/server/apsi_kw_pir_server.h"

#include <memory>

#include "yacl/base/exception.h"

#include "psi/proto/apsi_wrapper.pb.h"

namespace psi::kwpir {

ApsiKeywordPirServer::ApsiKeywordPirServer(InputConfig input,
                                           std::string db_path,
                                           ApsiSenderConfig apsi_config)
    : KeywordPirServer(input, db_path) {
  apsi_option_.group_cnt = apsi_config.experimental_bucket_group_cnt();
  apsi_option_.num_buckets = apsi_config.experimental_bucket_cnt();
  apsi_option_.nonce_byte_count = apsi_config.nonce_byte_count();
  apsi_option_.compress = apsi_config.compress();
  apsi_option_.params_file = apsi_config.params_file();
  apsi_option_.group_cnt = apsi_config.experimental_bucket_group_cnt();
}

void ApsiKeywordPirServer::DoQuery(const KeywordPirServerRequest* request,
                                   KeywordPirServerResponse* response) {
  if (request->step() == "oprf") {
    response->mutable_reply()->Add(sender_->RunOPRF(request->query(0)));
  } else if (request->step() == "params") {
    std::vector<std::string> oprf{request->query().begin(),
                                  request->query().end()};
    auto oprf_res = sender_->RunOPRF(oprf);
    response->mutable_reply()->Assign(oprf_res.begin(), oprf_res.end());
  } else if (request->step() == "query") {
    std::vector<std::string> query{request->query().begin(),
                                   request->query().end()};
    auto result = sender_->RunQuery(query);
    response->mutable_reply()->Assign(result.begin(), result.end());
  } else {
    YACL_THROW("unknown step {}", request->step());
  }
}

void ApsiKeywordPirServer::DoLoadDataBase(const std::string& db_path) {
  sender_ = std::make_shared<apsi_wrapper::api::Sender>(db_path);
}

void ApsiKeywordPirServer::DoGenerateDataBase(
    std::shared_ptr<InputProvider> input, const std::string& db_path) {
  apsi_option_.provider = input;
  apsi_option_.db_path = db_path;
  sender_ = std::make_shared<apsi_wrapper::api::Sender>(apsi_option_);
}

}  // namespace psi::kwpir