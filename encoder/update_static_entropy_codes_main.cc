// Copyright (c) the JPEG XL Project Authors.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "encoder/base/printf_macros.h"
#include "encoder/enc_entropy_code.h"
#include "encoder/enc_huffman_tree.h"
#include "encoder/entropy_code.h"
#include "encoder/static_entropy_codes.h"

namespace jxl {

struct DynamicPrefixCode {
  std::vector<uint8_t> depths;
  std::vector<uint16_t> bits;
};

void OutputCodes(const char* type,
                 const std::vector<DynamicPrefixCode>& prefix_codes) {
  printf("static constexpr size_t kNum%sPrefixCodes = %" PRIuS ";\n", type,
         prefix_codes.size());
  printf("static constexpr PrefixCode k%sPrefixCodes[kNum%sPrefixCodes] = {\n",
         type, type);
  for (const auto& prefix_code : prefix_codes) {
    size_t alphabet_size = prefix_code.depths.size();
    printf("    {{\n");
    for (size_t j = 0; j < alphabet_size; ++j) {
      printf("%s%2d,%s", j % 16 == 0 ? "         " : " ", prefix_code.depths[j],
             (j % 16 == 15 || j + 1 == alphabet_size) ? "\n" : "");
    }
    printf("     },\n");
    printf("     {\n");
    for (size_t j = 0; j < alphabet_size; ++j) {
      printf("%s0x%04x,%s", j % 8 == 0 ? "         " : " ", prefix_code.bits[j],
             (j % 8 == 7 || j + 1 == alphabet_size) ? "\n" : "");
    }
    printf("     }},\n");
  }
  printf("};\n");
}

bool ExtendPrefixCode(DynamicPrefixCode& prefix_code,
                      size_t new_alphabet_size) {
  static const int kTreeLimit = 15;

  const size_t alphabet_size = prefix_code.depths.size();
  if (prefix_code.bits.size() != alphabet_size) {
    return false;
  }

  // Step 1. Convert bit depths to population counts.
  std::vector<uint32_t> counts(new_alphabet_size);
  uint32_t total_count = 0;
  for (size_t i = 0; i < alphabet_size; ++i) {
    counts[i] = 1 << (kTreeLimit - prefix_code.depths[i]);
    total_count += counts[i];
  }
  if (total_count != 1 << kTreeLimit) {
    return false;
  }

  // Step 2. Extend population counts by 1s.
  for (size_t i = alphabet_size; i < new_alphabet_size; ++i) {
    counts[i] = 1;
  }

  // Step 3. Regenerate depths and bits from new population counts.
  prefix_code.depths.resize(new_alphabet_size);
  prefix_code.bits.resize(new_alphabet_size);
  CreateHuffmanTree(&counts[0], new_alphabet_size, kTreeLimit,
                    &prefix_code.depths[0]);
  ConvertBitDepthsToSymbols(&prefix_code.depths[0], new_alphabet_size,
                            &prefix_code.bits[0]);
  return true;
}

enum PrefixCodeType {
  DC = 0,
  AC = 1,
};

std::vector<DynamicPrefixCode> ConvertToDynamicCodes(
    const PrefixCode* prefix_codes, size_t num_codes) {
  std::vector<DynamicPrefixCode> out;
  for (size_t i = 0; i < num_codes; ++i) {
    const uint8_t* depths = prefix_codes[i].depths;
    const uint16_t* bits = prefix_codes[i].bits;
    DynamicPrefixCode code = {
        {depths, depths + kAlphabetSize},
        {bits, bits + kAlphabetSize},
    };
    out.emplace_back(std::move(code));
  }
  return out;
}

bool GenerateNewPrefixCodes(PrefixCodeType type, size_t new_alphabet_size) {
  const char* type_name;
  std::vector<DynamicPrefixCode> prefix_codes;
  if (type == PrefixCodeType::DC) {
    type_name = "DC";
    prefix_codes = ConvertToDynamicCodes(kDCPrefixCodes, kNumDCPrefixCodes);
  } else if (type == PrefixCodeType::AC) {
    type_name = "AC";
    prefix_codes = ConvertToDynamicCodes(kACPrefixCodes, kNumACPrefixCodes);
  }
  for (auto& prefix_code : prefix_codes) {
    if (!ExtendPrefixCode(prefix_code, new_alphabet_size)) {
      return false;
    }
  }
  OutputCodes(type_name, prefix_codes);
  return true;
}

}  // namespace jxl

void PrintHelp(char* arg0) {
  fprintf(stderr,
          "Usage: %s <type> <new alphabet size>\n\n"
          "Prints the updated entropy codes of the given type to stdout.\n"
          "  <type> can be either 'DC' or 'AC'\n",
          arg0);
};

int main(int argc, char** argv) {
  if (argc != 3) {
    PrintHelp(argv[0]);
    return EXIT_FAILURE;
  }
  jxl::PrefixCodeType type = jxl::PrefixCodeType::DC;
  if (strcmp(argv[1], "DC") == 0) {
    type = jxl::PrefixCodeType::DC;
  } else if (strcmp(argv[1], "AC") == 0) {
    type = jxl::PrefixCodeType::AC;
  } else {
    PrintHelp(argv[0]);
    return EXIT_FAILURE;
  }
  size_t new_alphabet_size = std::stoi(argv[2]);
  if (new_alphabet_size <= jxl::kAlphabetSize) {
    fprintf(stderr,
            "New alphabet size must be greater than current alphabet size, "
            "which is %" PRIuS ".\n",
            jxl::kAlphabetSize);
    return EXIT_FAILURE;
  }
  if (!jxl::GenerateNewPrefixCodes(type, new_alphabet_size)) {
    fprintf(stderr, "Failed to extend prefix codes (internal error)\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
