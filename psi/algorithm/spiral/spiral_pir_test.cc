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

#include <random>

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/prg.h"
#include "yacl/utils/elapsed_timer.h"

#include "psi/algorithm/spiral/gadget.h"
#include "psi/algorithm/spiral/poly_matrix_utils.h"
#include "psi/algorithm/spiral/public_keys.h"
#include "psi/algorithm/spiral/serialize.h"
#include "psi/algorithm/spiral/spiral_client.h"
#include "psi/algorithm/spiral/spiral_server.h"
#include "psi/algorithm/spiral/util.h"

namespace psi::spiral {

namespace {

struct TestParams {
  size_t database_rows_ = 0;
  size_t database_row_byte_ = 0;
  bool serialized_ = false;
};

std::vector<std::vector<uint8_t>> GenRandomDatabase(size_t rows, size_t lens) {
  yacl::crypto::Prg<uint8_t> prg;
  std::vector<std::vector<uint8_t>> database;
  for (size_t i = 0; i < rows; ++i) {
    std::vector<uint8_t> row(lens);
    prg.Fill(absl::MakeSpan(row));
    database.push_back(std::move(row));
  }
  return database;
}

}  // namespace

class SpiralPirTest : public testing::TestWithParam<TestParams> {};

TEST_P(SpiralPirTest, Works) {
  // get a test params
  auto test_param = GetParam();
  auto database_rows = test_param.database_rows_;
  auto row_byte = test_param.database_row_byte_;
  auto serialized = test_param.serialized_;
  DatabaseMetaInfo database_info(database_rows, row_byte);

  SPDLOG_INFO("database rows: {}, row bytes: {}", database_rows, row_byte);

  yacl::ElapsedTimer timer;

  // Gen database
  SPDLOG_INFO("GenRandomDatabase, this will take much time");
  auto raw_database = GenRandomDatabase(database_rows, row_byte);
  SPDLOG_INFO("GenRandomDatabase, time cost: {} ms", timer.CountMs());

  // get a SpiralParams
  auto spiral_params = util::GetPerformanceImproveParam();
  spiral_params.UpdateByDatabaseInfo(database_info);

  Params params_client(spiral_params);
  Params params_server(spiral_params);

  SPDLOG_INFO("Spiral Params: \n{}", spiral_params.ToString());

  // new client
  SpiralClient client(std::move(params_client), database_info);
  auto pp = client.GenKeys();

  // new Server
  SpiralServer server(std::move(params_server), database_info);

  // set database
  timer.Restart();
  server.SetDatabase(raw_database);
  SPDLOG_INFO("Server SetDatabase, time cost: {} ms", timer.CountMs());

  // gen random target_idx
  std::random_device rd;
  std::mt19937_64 rng(rd());
  size_t raw_idx_target = rng() % database_rows;
  std::vector<uint8_t> correct_row(raw_database[raw_idx_target]);

  yacl::ElapsedTimer timer2;

  // query and response
  if (!serialized) {
    SPDLOG_INFO("Do query without serialized");

    // set public paramter
    server.SetPublicKeys(std::move(pp));

    // now generate Query
    timer.Restart();
    auto query = client.GenQuery(raw_idx_target);
    SPDLOG_INFO("Client GenQuery, time cost: {} ms", timer.CountMs());

    // server handle query
    timer.Restart();
    auto responses = server.ProcessQuery(query);
    SPDLOG_INFO("Server ProcessQuery, time cost: {} ms", timer.CountMs());

    timer.Restart();
    auto decode = client.DecodeResponse(responses, raw_idx_target);
    SPDLOG_INFO("Client DecodeResponse, time cost: {} ms", timer.CountMs());

    // verify
    EXPECT_EQ(correct_row, decode);

  } else {
    SPDLOG_INFO("Do query with serialized");

    timer.Restart();
    auto pp_buffer = SerializePublicParams(server.GetParams(), pp);
    SPDLOG_INFO("public params serialize time cost {} ms", timer.CountMs());
    SPDLOG_INFO("public params serialize size {} kb", pp_buffer.size() / 1024);

    timer.Restart();
    auto pp2 = DeserializePublicParams(server.GetParams(), pp_buffer);
    SPDLOG_INFO("public params deserialize time cost {} ms", timer.CountMs());

    server.SetPublicKeys(std::move(pp2));

    timer2.Restart();
    timer.Restart();
    auto query = client.GenQuery(raw_idx_target);

    SPDLOG_INFO("Client GenQuery, time cost: {} ms", timer.CountMs());

    timer.Restart();
    auto query_buffer = SerializeSpiralQueryRng(query);
    SPDLOG_INFO("Serialize query, time cost: {} ms", timer.CountMs());
    SPDLOG_INFO("query serialize size {} kb", query_buffer.size() / 1024);

    timer.Restart();
    auto query2 = DeserializeSpiralQueryRng(server.GetParams(), query_buffer);
    SPDLOG_INFO("Deserialize query, time cost: {} ms", timer.CountMs());

    timer.Restart();
    auto responses = server.ProcessQuery(query2);
    SPDLOG_INFO("Server ProcessQuery, time cost: {} ms", timer.CountMs());

    timer.Restart();
    auto response_buffer = SerializeResponse(responses);
    SPDLOG_INFO("Serialize response, time cost: {} ms", timer.CountMs());
    SPDLOG_INFO("response serialize size {} kb", response_buffer.size() / 1024);

    timer.Restart();
    auto responses2 = DeserializeResponse(server.GetParams(), response_buffer);
    SPDLOG_INFO("Deserialize response, time cost: {} ms", timer.CountMs());

    // client decode
    timer.Restart();
    auto decode = client.DecodeResponse(responses2, raw_idx_target);
    SPDLOG_INFO("Client DecodeResponse, time cost: {} ms", timer.CountMs());

    // verify
    EXPECT_EQ(correct_row, decode);
  }

  SPDLOG_INFO("database rows: {}, row bytes: {}", database_rows, row_byte);
  SPDLOG_INFO("One time query ,total time: {} ms", timer2.CountMs());
}

INSTANTIATE_TEST_SUITE_P(Works_Instances, SpiralPirTest,
                         testing::Values(TestParams{1000000, 256},
                                         TestParams{1000000, 256, true}));

}  // namespace psi::spiral