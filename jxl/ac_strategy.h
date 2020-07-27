// Copyright (c) the JPEG XL Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef JXL_AC_STRATEGY_H_
#define JXL_AC_STRATEGY_H_

#include <stddef.h>
#include <stdint.h>

#include "jxl/base/status.h"
#include "jxl/coeff_order_fwd.h"
#include "jxl/common.h"
#include "jxl/image_ops.h"

// Defines the different kinds of transforms, and heuristics to choose between
// them.
// `AcStrategy` represents what transform should be used, and which sub-block of
// that transform we are currently in. Note that DCT4x4 is applied on all four
// 4x4 sub-blocks of an 8x8 block.
// `AcStrategyImage` defines which strategy should be used for each 8x8 block
// of the image. The highest 4 bits represent the strategy to be used, the
// lowest 4 represent the index of the block inside that strategy. Blocks should
// be aligned, i.e. 32x32 blocks should only start in positions that are
// multiples of 32.

namespace jxl {

class AcStrategy {
 public:
  // Extremal values for the number of blocks/coefficients of a single strategy.
  static constexpr size_t kMaxCoeffBlocks = 4;
  static constexpr size_t kMaxBlockDim = kBlockDim * kMaxCoeffBlocks;
  static constexpr size_t kMaxCoeffArea = kMaxBlockDim * kMaxBlockDim;

  // Raw strategy types.
  enum Type : uint32_t {
    // Regular block size DCT (value matches kQuantKind)
    DCT = 0,
    // Encode pixels without transforming (value matches kQuantKind)
    IDENTITY = 1,
    // Use 2-by-2 DCT (value matches kQuantKind)
    DCT2X2 = 2,
    // Use 4-by-4 DCT (value matches kQuantKind)
    DCT4X4 = 3,
    // Use 16-by-16 DCT
    DCT16X16 = 4,
    // Use 32-by-32 DCT
    DCT32X32 = 5,
    // Use 16-by-8 DCT
    DCT16X8 = 6,
    // Use 8-by-16 DCT
    DCT8X16 = 7,
    // Use 32-by-8 DCT
    DCT32X8 = 8,
    // Use 8-by-32 DCT
    DCT8X32 = 9,
    // Use 32-by-16 DCT
    DCT32X16 = 10,
    // Use 16-by-32 DCT
    DCT16X32 = 11,
    // 4x8 and 8x4 DCT
    DCT4X8 = 12,
    DCT8X4 = 13,
    // Corner-DCT.
    AFV0 = 14,
    AFV1 = 15,
    AFV2 = 16,
    AFV3 = 17,
    // Marker for num of valid strategies.
    kNumValidStrategies
  };

  static constexpr uint32_t TypeBit(const Type type) {
    return 1u << static_cast<uint32_t>(type);
  }

  // Returns true if this block is the first 8x8 block (i.e. top-left) of a
  // possibly multi-block strategy.
  JXL_INLINE bool IsFirstBlock() const { return is_first_; }

  JXL_INLINE bool IsMultiblock() const {
    constexpr uint32_t bits = TypeBit(Type::DCT16X16) |
                              TypeBit(Type::DCT32X32) | TypeBit(Type::DCT16X8) |
                              TypeBit(Type::DCT8X16) | TypeBit(Type::DCT32X8) |
                              TypeBit(Type::DCT8X32) | TypeBit(Type::DCT16X32) |
                              TypeBit(Type::DCT32X16);
    JXL_DASSERT(Strategy() < kNumValidStrategies);
    return ((1u << static_cast<uint32_t>(Strategy())) & bits) != 0;
  }

  // Returns the raw strategy value. Should only be used for tokenization.
  JXL_INLINE uint8_t RawStrategy() const {
    return static_cast<uint8_t>(strategy_);
  }

  JXL_INLINE Type Strategy() const { return strategy_; }

  // Inverse check
  static JXL_INLINE constexpr bool IsRawStrategyValid(uint8_t raw_strategy) {
    return raw_strategy < kNumValidStrategies;
  }
  static JXL_INLINE AcStrategy FromRawStrategy(uint8_t raw_strategy) {
    return FromRawStrategy(static_cast<Type>(raw_strategy));
  }
  static JXL_INLINE AcStrategy FromRawStrategy(Type raw_strategy) {
    JXL_DASSERT(IsRawStrategyValid(static_cast<uint32_t>(raw_strategy)));
    return AcStrategy(raw_strategy, /*is_first_block=*/true);
  }

  // "Natural order" means the order of increasing of "anisotropic" frequency of
  // continuous version of DCT basis.
  // Round-trip, for any given strategy s:
  //  X = NaturalCoeffOrder(s)[NaturalCoeffOrderLutN(s)[X]]
  //  X = NaturalCoeffOrderLut(s)[NaturalCoeffOrderN(s)[X]]
  JXL_INLINE const coeff_order_t* NaturalCoeffOrder() const {
    return CoeffOrder()->order +
           CoeffOrderAndLut::kOffset[RawStrategy()] * kDCTBlockSize;
  }

  JXL_INLINE const coeff_order_t* NaturalCoeffOrderLut() const {
    return CoeffOrder()->lut +
           CoeffOrderAndLut::kOffset[RawStrategy()] * kDCTBlockSize;
  }

  // Number of 8x8 blocks that this strategy will cover. 0 for non-top-left
  // blocks inside a multi-block transform.
  JXL_INLINE size_t covered_blocks_x() const {
    static constexpr uint8_t kLut[] = {1, 1, 1, 1, 2, 4, 1, 2, 1,
                                       4, 2, 4, 1, 1, 1, 1, 1, 1};
    static_assert(sizeof(kLut) / sizeof(*kLut) == kNumValidStrategies,
                  "Update LUT");
    return kLut[size_t(strategy_)];
  }

  JXL_INLINE size_t covered_blocks_y() const {
    static constexpr uint8_t kLut[] = {1, 1, 1, 1, 2, 4, 2, 1, 4,
                                       1, 4, 2, 1, 1, 1, 1, 1, 1};
    static_assert(sizeof(kLut) / sizeof(*kLut) == kNumValidStrategies,
                  "Update LUT");
    return kLut[size_t(strategy_)];
  }

  JXL_INLINE size_t log2_covered_blocks() const {
    static constexpr uint8_t kLut[] = {0, 0, 0, 0, 2, 4, 1, 1, 2,
                                       2, 3, 3, 0, 0, 0, 0, 0, 0};
    static_assert(sizeof(kLut) / sizeof(*kLut) == kNumValidStrategies,
                  "Update LUT");
    return kLut[size_t(strategy_)];
  }

  // 1 / covered_block_x() / covered_block_y(), for fast division.
  // Should only be called with block_ == 0.
  JXL_INLINE float inverse_covered_blocks() const {
    if (strategy_ == Type::DCT32X32) return 1.0f / 16;
    if (strategy_ == Type::DCT16X16) return 0.25f;
    if (strategy_ == Type::DCT8X16 || strategy_ == Type::DCT16X8) return 0.5f;
    if (strategy_ == Type::DCT8X32 || strategy_ == Type::DCT32X8) return 0.25f;
    if (strategy_ == Type::DCT32X16 || strategy_ == Type::DCT16X32)
      return 1.0f / 8;
    return 1.0f;
  }

  JXL_INLINE float InverseNumACCoefficients() const {
    JXL_DASSERT(IsFirstBlock());
    if (strategy_ == Type::DCT32X32) return 1.0f / (32 * 32 - 16);
    if (strategy_ == Type::DCT16X16) return 1.0f / (16 * 16 - 4);
    if (strategy_ == Type::DCT8X16 || strategy_ == Type::DCT16X8)
      return 1.0f / (8 * 16 - 2);
    if (strategy_ == Type::DCT8X32 || strategy_ == Type::DCT32X8)
      return 1.0f / (8 * 32 - 4);
    if (strategy_ == Type::DCT32X16 || strategy_ == Type::DCT16X32)
      return 1.0f / (32 * 16 - 8);
    return 1.0f / (8 * 8 - 1);
  }

  struct CoeffOrderAndLut {
    // Those offsets get multiplied by kDCTBlockSize.
    static constexpr size_t kOffset[kNumValidStrategies + 1] = {
        0, 1, 2, 3, 4, 8, 24, 26, 28, 32, 36, 44, 52, 53, 54, 55, 56, 57, 58};
    static constexpr size_t kTotalTableSize =
        kOffset[kNumValidStrategies] * kDCTBlockSize;
    coeff_order_t order[kTotalTableSize];
    coeff_order_t lut[kTotalTableSize];
  };

 private:
  friend class AcStrategyRow;
  JXL_INLINE AcStrategy(Type strategy, bool is_first)
      : strategy_(strategy), is_first_(is_first) {
    JXL_DASSERT(IsMultiblock() || is_first == true);
  }

  Type strategy_;
  bool is_first_;

  static const CoeffOrderAndLut* CoeffOrder();
};

// Class to use a certain row of the AC strategy.
class AcStrategyRow {
 public:
  explicit AcStrategyRow(const uint8_t* row) : row_(row) {}
  AcStrategy operator[](size_t x) const {
    return AcStrategy(static_cast<AcStrategy::Type>(row_[x] >> 1), row_[x] & 1);
  }

 private:
  const uint8_t* JXL_RESTRICT row_;
};

class AcStrategyImage {
 public:
  AcStrategyImage() = default;
  AcStrategyImage(size_t xsize, size_t ysize);
  AcStrategyImage(AcStrategyImage&&) = default;
  AcStrategyImage& operator=(AcStrategyImage&&) = default;

  void FillDCT8() {
    FillImage<uint8_t>((static_cast<uint8_t>(AcStrategy::Type::DCT) << 1) | 1,
                       &layers_);
  }

  void FillInvalid() { FillImage(INVALID, &layers_); }

  void Set(size_t x, size_t y, AcStrategy::Type type) {
#if JXL_ENABLE_ASSERT
    AcStrategy acs = AcStrategy::FromRawStrategy(type);
#endif  // JXL_ENABLE_ASSERT
    JXL_ASSERT(y + acs.covered_blocks_y() <= layers_.ysize());
    JXL_ASSERT(x + acs.covered_blocks_x() <= layers_.xsize());
    JXL_CHECK(SetNoBoundsCheck(x, y, type, /*check=*/false));
  }

  Status SetNoBoundsCheck(size_t x, size_t y, AcStrategy::Type type,
                          bool check = true) {
    AcStrategy acs = AcStrategy::FromRawStrategy(type);
    for (size_t iy = 0; iy < acs.covered_blocks_y(); iy++) {
      for (size_t ix = 0; ix < acs.covered_blocks_x(); ix++) {
        size_t pos = (y + iy) * stride_ + x + ix;
        if (check && row_[pos] != INVALID) {
          return JXL_FAILURE("Invalid AC strategy: block overlap");
        }
        row_[pos] =
            (static_cast<uint8_t>(type) << 1) | ((iy | ix) == 0 ? 1 : 0);
      }
    }
    return true;
  }

  bool IsValid(size_t x, size_t y) { return row_[y * stride_ + x] != INVALID; }

  AcStrategyRow ConstRow(size_t y, size_t x_prefix = 0) const {
    return AcStrategyRow(layers_.ConstRow(y) + x_prefix);
  }

  AcStrategyRow ConstRow(const Rect& rect, size_t y) const {
    return ConstRow(rect.y0() + y, rect.x0());
  }

  size_t PixelsPerRow() const { return layers_.PixelsPerRow(); }

  size_t xsize() const { return layers_.xsize(); }
  size_t ysize() const { return layers_.ysize(); }

  // Count the number of blocks of a given type.
  size_t CountBlocks(AcStrategy::Type type) const;

 private:
  ImageB layers_;
  uint8_t* JXL_RESTRICT row_;
  size_t stride_;

  // A value that does not represent a valid combined AC strategy
  // value. Used as a sentinel.
  static constexpr uint8_t INVALID = 0xFF;
};

}  // namespace jxl

#endif  // JXL_AC_STRATEGY_H_
