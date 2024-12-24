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

#ifdef __x86_64__
#include <immintrin.h>
#elif defined(__aarch64__)
#include "sse2neon.h"
#endif

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "yacl/base/buffer.h"
#include "yacl/utils/parallel.h"

#include "psi/algorithm/spiral/arith/arith_params.h"
#include "psi/algorithm/spiral/common.h"
#include "psi/algorithm/spiral/params.h"
#include "psi/algorithm/spiral/poly_matrix.h"
#include "psi/algorithm/spiral/spiral_client.h"
#include "psi/algorithm/spiral/util.h"

#include "psi/algorithm/spiral/serializable.pb.h"

namespace psi::spiral {

yacl::Buffer SerializePolyMatrixRaw(const PolyMatrixRaw& poly_matrix) {
  // 1. first construct proto object
  PolyMatrixProto proto;

  proto.set_rows(poly_matrix.Rows());
  proto.set_cols(poly_matrix.Cols());

  for (const auto& value : poly_matrix.Data()) {
    proto.add_data(value);
  }
  // then convert to buffer
  yacl::Buffer buffer(proto.ByteSizeLong());
  proto.SerializePartialToArray(buffer.data(), buffer.size());

  return buffer;
}

PolyMatrixRaw DeserializePolyMatrixRaw(const Params& params,
                                       yacl::Buffer& buffer) {
  // first convert buffer into proto object
  PolyMatrixProto proto;
  proto.ParseFromArray(buffer.data(), buffer.size());

  // then convert proto object to Object
  size_t rows = proto.rows();
  size_t cols = proto.cols();

  std::vector<uint64_t> data;
  data.reserve(proto.data_size());
  for (const auto& value : proto.data()) {
    data.push_back(value);
  }

  YACL_ENFORCE_EQ(rows * cols * params.PolyLen(), data.size());
  return PolyMatrixRaw(params.PolyLen(), rows, cols, std::move(data));
}

yacl::Buffer SerializePolyMatrixRawRng(const PolyMatrixRaw& poly_matrix,
                                       const Params& params) {
  // 1. first construct proto object
  PolyMatrixProto proto;

  proto.set_rows(poly_matrix.Rows() - 1);
  proto.set_cols(poly_matrix.Cols());

  // skip the first row
  size_t offset = poly_matrix.Cols() * params.PolyLen();

  for (size_t i = offset; i < poly_matrix.Data().size(); ++i) {
    proto.add_data(poly_matrix.Data()[i]);
  }
  // then convert to buffer
  yacl::Buffer buffer(proto.ByteSizeLong());
  proto.SerializePartialToArray(buffer.data(), buffer.size());

  return buffer;
}

PolyMatrixRaw DeserializePolyMatrixRawRng(yacl::Buffer& buffer,
                                          const Params& params,
                                          yacl::crypto::Prg<uint64_t> rng) {
  // first convert buffer into proto object
  PolyMatrixProto proto;
  proto.ParseFromArray(buffer.data(), buffer.size());

  // then convert proto object to Object
  size_t rows = proto.rows();
  size_t cols = proto.cols();

  YACL_ENFORCE_EQ(rows * cols * params.PolyLen(),
                  static_cast<size_t>(proto.data_size()));

  std::vector<uint64_t> data;
  size_t first_row_coeffs = cols * params.PolyLen();
  size_t rest_row_coeffs = proto.data_size();
  data.reserve(first_row_coeffs + rest_row_coeffs);

  // resconstruct the first row by rng
  for (size_t i = 0; i < first_row_coeffs; ++i) {
    data.push_back(params.Modulus() - (rng() % params.Modulus()));
  }
  // rest rows
  for (const auto& value : proto.data()) {
    data.push_back(value);
  }
  return PolyMatrixRaw(params.PolyLen(), rows + 1, cols, std::move(data));
}

yacl::Buffer SerializeSpiralQuery(const SpiralQuery& query) {
  SpiralQueryProto proto;
  // only one possible
  *proto.mutable_ct() = query.ct_.ToProto();
  proto.set_seed(reinterpret_cast<const uint8_t*>(&query.seed_),
                 sizeof(uint128_t));

  yacl::Buffer buffer(proto.ByteSizeLong());
  proto.SerializePartialToArray(buffer.data(), buffer.size());

  return buffer;
}

SpiralQuery DeserializeSpiralQuery(const Params& params, yacl::Buffer& buffer) {
  SpiralQueryProto proto;
  proto.ParseFromArray(buffer.data(), buffer.size());

  SpiralQuery query;
  query.ct_ = PolyMatrixRaw::FromProto(proto.ct(), params);

  uint128_t seed{0};
  std::memcpy(&seed, proto.seed().data(), sizeof(uint128_t));
  query.seed_ = seed;

  return query;
}

yacl::Buffer SerializeSpiralQueryRng(const SpiralQuery& query) {
  SpiralQueryProto proto;
  *proto.mutable_ct() = query.ct_.ToProtoRng();

  proto.set_seed(reinterpret_cast<const uint8_t*>(&query.seed_),
                 sizeof(uint128_t));

  yacl::Buffer buffer(proto.ByteSizeLong());
  proto.SerializePartialToArray(buffer.data(), buffer.size());

  return buffer;
}

SpiralQuery DeserializeSpiralQueryRng(const Params& params,
                                      yacl::Buffer& buffer) {
  SpiralQueryProto proto;
  proto.ParseFromArray(buffer.data(), buffer.size());

  // first we construct seed and prg
  uint128_t seed{0};
  std::memcpy(&seed, proto.seed().data(), sizeof(uint128_t));

  yacl::crypto::Prg<uint64_t> rng(seed);

  SpiralQuery query;
  query.seed_ = seed;

  query.ct_ = PolyMatrixRaw::FromProtoRng(proto.ct(), params, rng);

  return query;
}

yacl::Buffer SerializePublicParams(const Params& params, const PublicKeys& pp) {
  PublicKeysProto proto;

  for (auto& val : pp.v_packing_) {
    auto val_raw = FromNtt(params, val);
    *proto.add_v_packing() = val_raw.ToProto();
  }

  for (auto& val : pp.v_expansion_left_) {
    auto val_raw = FromNtt(params, val);
    *proto.add_v_expansion_left() = val_raw.ToProto();
  }

  for (auto& val : pp.v_expansion_right_) {
    auto val_raw = FromNtt(params, val);
    *proto.add_v_expansion_right() = val_raw.ToProto();
  }

  for (auto& val : pp.v_conversion_) {
    auto val_raw = FromNtt(params, val);
    *proto.add_v_conversion() = val_raw.ToProto();
  }

  yacl::Buffer buffer(proto.ByteSizeLong());
  proto.SerializePartialToArray(buffer.data(), buffer.size());

  return buffer;
}

PublicKeys DeserializePublicParams(const Params& params, yacl::Buffer& buffer) {
  PublicKeysProto proto;
  proto.ParseFromArray(buffer.data(), buffer.size());

  PublicKeys pp;
  for (const auto& val : proto.v_packing()) {
    auto val_ntt = ToNtt(params, PolyMatrixRaw::FromProto(val, params));
    pp.v_packing_.push_back(val_ntt);
  }

  if (proto.v_expansion_left_size() > 0) {
    std::vector<PolyMatrixNtt> v_expansion_left;
    for (const auto& val : proto.v_expansion_left()) {
      auto val_ntt = ToNtt(params, PolyMatrixRaw::FromProto(val, params));
      v_expansion_left.push_back(val_ntt);
    }
    pp.v_expansion_left_ = std::move(v_expansion_left);
  }

  if (proto.v_expansion_right_size() > 0) {
    std::vector<PolyMatrixNtt> v_expansion_right;
    for (const auto& val : proto.v_expansion_right()) {
      auto val_ntt = ToNtt(params, PolyMatrixRaw::FromProto(val, params));

      v_expansion_right.push_back(val_ntt);
    }
    pp.v_expansion_right_ = std::move(v_expansion_right);
  }

  if (proto.v_conversion_size() > 0) {
    std::vector<PolyMatrixNtt> v_conversion;
    for (const auto& val : proto.v_conversion()) {
      auto val_ntt = ToNtt(params, PolyMatrixRaw::FromProto(val, params));
      v_conversion.push_back(val_ntt);
    }
    pp.v_conversion_ = std::move(v_conversion);
  }
  return pp;
}

}  // namespace psi::spiral
