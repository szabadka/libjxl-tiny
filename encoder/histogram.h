// Copyright (c) the JPEG XL Project Authors.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef ENCODER_HISTOGRAM_H_
#define ENCODER_HISTOGRAM_H_

#include <stdint.h>

#include <vector>

#include "encoder/common.h"
#include "encoder/token.h"

namespace jxl {

struct Histogram {
  Histogram() { total_count_ = 0; }
  void Clear() {
    data_.clear();
    total_count_ = 0;
  }
  void Add(size_t symbol) {
    if (data_.size() <= symbol) {
      data_.resize(DivCeil(symbol + 1, kRounding) * kRounding);
    }
    ++data_[symbol];
    ++total_count_;
  }
  void AddHistogram(const Histogram& other) {
    if (other.data_.size() > data_.size()) {
      data_.resize(other.data_.size());
    }
    for (size_t i = 0; i < other.data_.size(); ++i) {
      data_[i] += other.data_[i];
    }
    total_count_ += other.total_count_;
  }
  void InitStatic(const int32_t* data, size_t len) {
    data_.resize(len);
    total_count_ = 0;
    for (size_t i = 0; i < len; ++i) {
      data_[i] = data[i];
      total_count_ += data_[i];
    }
  }

  std::vector<int32_t> data_;
  size_t total_count_;
  mutable float entropy_;  // WARNING: not kept up-to-date.
  static constexpr size_t kRounding = 8;
};

struct HistogramBuilder {
  explicit HistogramBuilder(const uint8_t* context_map,
                            const size_t num_contexts)
      : static_context_map(context_map), histograms(num_contexts) {}

  void Add(int symbol, size_t context) {
    JXL_CHECK(context < histograms.size());
    histograms[context].Add(symbol);
  }
  void Add(const Token& token) {
    uint32_t tok, nbits, bits;
    UintCoder().Encode(token.value, &tok, &nbits, &bits);
    if (static_context_map != nullptr) {
      Add(tok, static_cast<size_t>(static_context_map[token.context]));
    } else {
      Add(tok, token.context);
    }
  }
  template <typename T>
  void Add(const std::vector<T>& v) {
    for (const auto& i : v) Add(i);
  }

  const uint8_t* static_context_map = nullptr;
  std::vector<Histogram> histograms;
};

template <typename T>
std::vector<Histogram> BuildHistograms(const uint8_t* context_map,
                                       size_t num_contexts, std::vector<T>& v) {
  HistogramBuilder builder(context_map, num_contexts);
  builder.Add(v);
  return builder.histograms;
}

}  // namespace jxl
#endif  // ENCODER_HISTOGRAM_H_
