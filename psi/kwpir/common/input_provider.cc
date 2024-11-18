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

#include "psi/kwpir/common/input_provider.h"

#include <filesystem>
#include <memory>

#include "yacl/base/exception.h"

#include "psi/utils/arrow_helper.h"

namespace psi::kwpir {

InputProvider::InputProvider(InputConfig config, size_t batch_size)
    : config_(config), batch_size_(batch_size) {
  YACL_ENFORCE(config.file_type() == FileType::FILE_TYPE_CSV,
               "Only support csv file now");

  std::vector<std::string> keys{config_.key_column_names().begin(),
                                config_.key_column_names().end()};
  std::vector<std::string> values{config.value_column_names().begin(),
                                  config.value_column_names().end()};
  has_label_ = !values.empty();
  csv_provider_ = std::make_shared<ArrowCsvBatchProvider>(
      config.file_name(), keys, batch_size_, values);
}

InputProvider::Batch InputProvider::ReadNextBatch() {
  Batch batch;
  std::tie(batch.keys, batch.labels) = csv_provider_->ReadNextLabeledBatch();
  return batch;
}

}  // namespace psi::kwpir