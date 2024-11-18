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

#include "psi/apsi_wrapper/api/sender.h"
#include "psi/kwpir/server/kw_pir_server.h"

#include "psi/proto/apsi_wrapper.pb.h"

namespace psi::kwpir {

class ApsiKeywordPirServer : public KeywordPirServer {
 public:
  ApsiKeywordPirServer(InputConfig input, std::string db_path,
                       ApsiSenderConfig apsi_config);

  void DoQuery(const KeywordPirServerRequest* request,
               KeywordPirServerResponse* response) override;

  void DoLoadDataBase(const std::string& db_path) override;

  void DoGenerateDataBase(std::shared_ptr<InputProvider> input,
                          const std::string& db_path) override;

 protected:
  psi::apsi_wrapper::api::Sender::KwPirOption apsi_option_;
  std::shared_ptr<apsi_wrapper::api::Sender> sender_;
};

}  // namespace psi::kwpir