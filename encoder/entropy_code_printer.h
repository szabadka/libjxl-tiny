// Copyright (c) the JPEG XL Project Authors.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef ENCODER_ENTROPY_CODE_PRINTER_H_
#define ENCODER_ENTROPY_CODE_PRINTER_H_

namespace jxl {

static inline void PrintAnnotatedACContextMap(const uint8_t* context_map) {
  static const size_t num_block_ctxs = 4;
  printf("static constexpr uint8_t kACContextMap[] = {\n");
  printf("    // Context map for number of nonzeros\n");
  printf("    //   8x8   8x16  8x8   8x16\n");
  printf("    //    Y     Y    X,B   X,B\n");
  for (int i = 0; i < 37 * num_block_ctxs; ++i) {
    int bctx = i % num_block_ctxs;
    int pred_ctx = i / num_block_ctxs;
    if (bctx == 0) printf("      ");
    printf(" %4d,", context_map[i]);
    if (bctx + 1 == num_block_ctxs) {
      if (pred_ctx < 8) {
        printf("    // pred: %2d\n", pred_ctx);
      } else if (pred_ctx < 36) {
        printf("    // pred: %2d - %2d\n", 2 * pred_ctx - 8, 2 * pred_ctx - 7);
      } else {
        printf("    // pred: 64 -\n");
      }
    }
  }
  static const int kCtxPerNZBucket[] = {31, 31, 31, 30, 29, 28, 26, 23};
  static const int kLastLineFrom[] = {48, 48, 48, 48, 48, 48, 32, 24};
  static const int kLastLineTo[] = {63, 63, 63, 59, 55, 51, 43, 31};
  static const int kNonZerosLeft[] = {1, 2, 3, 5, 9, 13, 21, 33, 64};
  static constexpr const char* kLineCom[] = {
      "  // k:  1 -  3\n", "  // k:  4 -  7\n", "  // k:  8 - 11\n",
      "  // k: 12 - 15\n", "  // k: 16 - 23\n", "  // k: 24 - 31\n",
      "  // k: 32 - 47\n", "  // k: 48 - 63\n",
  };
  static constexpr const char* kBlockContexts[] = {
      "8x8 Y",
      "8x16 and 16x8 Y",
      "8x8 X and B",
      "8x16 and 16x8 X and B",
  };
  for (int bctx = 0; bctx < num_block_ctxs; ++bctx) {
    printf("\n");
    printf("    //\n");
    printf("    // Zero density context map for %s blocks\n",
           kBlockContexts[bctx]);
    printf("    //\n");
    for (int nzctx = 0, i = 0; nzctx < 8; ++nzctx) {
      int nzleftmin = kNonZerosLeft[nzctx];
      int nzleftmax = kNonZerosLeft[nzctx + 1] - 1;
      if (nzleftmin == nzleftmax) {
        printf("    // Nonzeros left: %d\n", nzleftmin);
      } else {
        printf("    // Nonzeros left: %d - %d\n", nzleftmin, nzleftmax);
      }
      for (int kctx = 0; kctx <= kCtxPerNZBucket[nzctx]; ++kctx, ++i) {
        if (kctx % 4 == 0) printf("   ");
        if (kctx == 0) {
          printf("          ");
          ++kctx;
        }
        for (int p = 0; p < 2; ++p) {
          int ctx = 37 * num_block_ctxs + bctx * 458 + i * 2 + p;
          printf(" %2d,", context_map[ctx]);
        }
        if (kctx == kCtxPerNZBucket[nzctx]) {
          while ((kctx % 4) != 3) {
            printf("          ");
            ++kctx;
          }
          printf("  // k: %2d - %2d\n", kLastLineFrom[nzctx],
                 kLastLineTo[nzctx]);
        } else {
          printf("%s", (kctx % 4 == 3) ? kLineCom[kctx / 4] : "  ");
        }
      }
    }
  }
  printf("};\n");
}

}  // namespace jxl
#endif  // ENCODER_ENTROPY_CODE_PRINTER_H_
