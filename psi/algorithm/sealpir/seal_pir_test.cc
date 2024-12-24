// Copyright 2023 Ant Group Co., Ltd.
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

#include "psi/algorithm/sealpir/seal_pir.h"

#include <seal/seal.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"

using namespace std::chrono;
using namespace std;
using namespace seal;

namespace psi::sealpir {
namespace {
struct TestParams {
  uint32_t N = 4096;
  uint64_t num_of_items;
  uint64_t size_per_item = 256;
  uint32_t d = 2;
  uint32_t logt = 20;
  bool isSerialized = false;
};
}  // namespace
class SealPirTest : public testing::TestWithParam<TestParams> {};

TEST_P(SealPirTest, Works) {
  auto params = GetParam();
  uint32_t num_of_items = params.num_of_items;
  uint32_t size_per_item = params.size_per_item;
  bool isSerialized = params.isSerialized;

  SealPirOptions options{params.N, params.num_of_items, params.size_per_item,
                         params.d};

  // Initialize PIR Server
  SPDLOG_INFO("Main: Initializing server and client");
  SealPirClient client(options);

  std::shared_ptr<IDbPlaintextStore> plaintext_store =
      std::make_shared<MemoryDbPlaintextStore>();
  SealPirServer server(options, plaintext_store);

  SPDLOG_INFO("Main: Initializing the database (this may take some time) ...");

  vector<uint8_t> db_data(num_of_items * size_per_item);
  yacl::crypto::Prg<uint8_t> prg(yacl::crypto::SecureRandU128());
  prg.Fill(absl::MakeSpan(db_data));

  shared_ptr<IDbElementProvider> db_provider =
      make_shared<MemoryDbElementProvider>(std::move(db_data),
                                           params.size_per_item);

  // Measure database setup
  server.SetDatabaseByProvider(db_provider);
  SPDLOG_INFO("Main: database pre processed ");

  // Set galois key for client with id 0
  SPDLOG_INFO("Main: Setting Galois keys...");
  if (isSerialized) {
    string galois_keys_str = client.SerializeSealObject<seal::GaloisKeys>(
        client.GenerateGaloisKeys());
    server.SetGaloisKey(
        0, server.DeSerializeSealObject<seal::GaloisKeys>(galois_keys_str));
  } else {
    GaloisKeys galois_keys = client.GenerateGaloisKeys();
    server.SetGaloisKey(0, galois_keys);
  }

  uint64_t ele_index = yacl::crypto::RandU64() %
                       num_of_items;  // element in DB at random position
  uint64_t target_raw_idx = ele_index;

  uint64_t pt_idx =
      client.GetPtIndex(target_raw_idx);  // pt_idx of FV plaintext
  uint64_t pt_offset =
      client.GetPtOffset(target_raw_idx);  // pt_offset in FV plaintext
  SPDLOG_INFO("Main: raw_idx = {} from [0, {}]", ele_index, num_of_items - 1);
  SPDLOG_INFO("Main: FV pt_idx = {}, FV pt_offset = {}", pt_idx, pt_offset);

  // Measure query generation
  vector<uint8_t> elems;
  if (isSerialized) {
    yacl::Buffer query_buffer = client.GenerateIndexQuery(target_raw_idx).first;
    SPDLOG_INFO("Main: query generated");

    yacl::Buffer reply_buffer = server.GenerateIndexReply(query_buffer);
    SPDLOG_INFO("Main: reply generated");

    elems = client.DecodeIndexReply(reply_buffer, pt_offset);
    SPDLOG_INFO("Main: reply decoded");
  } else {
    SealPir::PirQuery query = client.GenerateQuery(pt_idx);
    SPDLOG_INFO("Main: query generated");

    SealPir::PirReply reply = server.GenerateReply(query, 0);
    SPDLOG_INFO("Main: reply generated");

    elems = client.DecodeReply(reply, pt_offset);
    SPDLOG_INFO("Main: reply decoded");
  }
  SPDLOG_INFO("Main: query finished");
  EXPECT_EQ(elems.size(), size_per_item);

  // Check that we retrieved the correct element
  EXPECT_EQ(elems, db_provider->ReadElement(ele_index * size_per_item));
  SPDLOG_INFO("Main: PIR result correct!");
}

INSTANTIATE_TEST_SUITE_P(Works_Instances, SealPirTest,
                         testing::Values(
                             // large num items
                             TestParams{4096, 10000, 256, 2},
                             TestParams{4096, 100000, 256, 2},
                             TestParams{4096, 1 << 20, 256, 2},
                             TestParams{4096, 1 << 20, 256, 2, true}));
}  // namespace psi::sealpir
