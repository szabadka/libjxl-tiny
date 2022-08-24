// Copyright (c) the JPEG XL Project Authors.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef ENCODER_IMAGE_OPS_H_
#define ENCODER_IMAGE_OPS_H_

// Operations on images.

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

#include "encoder/base/status.h"
#include "encoder/common.h"
#include "encoder/image.h"

namespace jxl {

template <typename T>
void CopyImageTo(const Plane<T>& from, Plane<T>* JXL_RESTRICT to) {
  JXL_ASSERT(SameSize(from, *to));
  if (from.ysize() == 0 || from.xsize() == 0) return;
  for (size_t y = 0; y < from.ysize(); ++y) {
    const T* JXL_RESTRICT row_from = from.ConstRow(y);
    T* JXL_RESTRICT row_to = to->Row(y);
    memcpy(row_to, row_from, from.xsize() * sizeof(T));
  }
}

// DEPRECATED - prefer to preallocate result.
template <typename T>
Plane<T> CopyImage(const Plane<T>& from) {
  Plane<T> to(from.xsize(), from.ysize());
  CopyImageTo(from, &to);
  return to;
}

// Copies `from:rect_from` to `to:rect_to`.
template <typename T>
void CopyImageTo(const Rect& rect_from, const Plane<T>& from,
                 const Rect& rect_to, Plane<T>* JXL_RESTRICT to) {
  JXL_DASSERT(SameSize(rect_from, rect_to));
  JXL_DASSERT(rect_from.IsInside(from));
  JXL_DASSERT(rect_to.IsInside(*to));
  if (rect_from.xsize() == 0) return;
  for (size_t y = 0; y < rect_from.ysize(); ++y) {
    const T* JXL_RESTRICT row_from = rect_from.ConstRow(from, y);
    T* JXL_RESTRICT row_to = rect_to.Row(to, y);
    memcpy(row_to, row_from, rect_from.xsize() * sizeof(T));
  }
}

// DEPRECATED - Returns a copy of the "image" pixels that lie in "rect".
template <typename T>
Plane<T> CopyImage(const Rect& rect, const Plane<T>& image) {
  Plane<T> copy(rect.xsize(), rect.ysize());
  CopyImageTo(rect, image, &copy);
  return copy;
}

// Copies `from:rect_from` to `to:rect_to`.
template <typename T>
void CopyImageTo(const Rect& rect_from, const Image3<T>& from,
                 const Rect& rect_to, Image3<T>* JXL_RESTRICT to) {
  JXL_ASSERT(SameSize(rect_from, rect_to));
  for (size_t c = 0; c < 3; c++) {
    CopyImageTo(rect_from, from.Plane(c), rect_to, &to->Plane(c));
  }
}

template <typename T, typename U>
void ConvertPlaneAndClamp(const Rect& rect_from, const Plane<T>& from,
                          const Rect& rect_to, Plane<U>* JXL_RESTRICT to) {
  JXL_ASSERT(SameSize(rect_from, rect_to));
  using M = decltype(T() + U());
  for (size_t y = 0; y < rect_to.ysize(); ++y) {
    const T* JXL_RESTRICT row_from = rect_from.ConstRow(from, y);
    U* JXL_RESTRICT row_to = rect_to.Row(to, y);
    for (size_t x = 0; x < rect_to.xsize(); ++x) {
      row_to[x] =
          std::min<M>(std::max<M>(row_from[x], std::numeric_limits<U>::min()),
                      std::numeric_limits<U>::max());
    }
  }
}

// Copies `from` to `to`.
template <typename T>
void CopyImageTo(const T& from, T* JXL_RESTRICT to) {
  return CopyImageTo(Rect(from), from, Rect(*to), to);
}

// Copies `from:rect_from` to `to`.
template <typename T>
void CopyImageTo(const Rect& rect_from, const T& from, T* JXL_RESTRICT to) {
  return CopyImageTo(rect_from, from, Rect(*to), to);
}

// Copies `from` to `to:rect_to`.
template <typename T>
void CopyImageTo(const T& from, const Rect& rect_to, T* JXL_RESTRICT to) {
  return CopyImageTo(Rect(from), from, rect_to, to);
}

template <typename T>
void FillImage(const T value, Plane<T>* image) {
  for (size_t y = 0; y < image->ysize(); ++y) {
    T* const JXL_RESTRICT row = image->Row(y);
    for (size_t x = 0; x < image->xsize(); ++x) {
      row[x] = value;
    }
  }
}

template <typename T>
void ZeroFillImage(Plane<T>* image) {
  if (image->xsize() == 0) return;
  for (size_t y = 0; y < image->ysize(); ++y) {
    T* const JXL_RESTRICT row = image->Row(y);
    memset(row, 0, image->xsize() * sizeof(T));
  }
}

// Mirrors out of bounds coordinates and returns valid coordinates unchanged.
// We assume the radius (distance outside the image) is small compared to the
// image size, otherwise this might not terminate.
// The mirror is outside the last column (border pixel is also replicated).
static inline int64_t Mirror(int64_t x, const int64_t xsize) {
  JXL_DASSERT(xsize != 0);

  // TODO(janwas): replace with branchless version
  while (x < 0 || x >= xsize) {
    if (x < 0) {
      x = -x - 1;
    } else {
      x = 2 * xsize - 1 - x;
    }
  }
  return x;
}

// Wrap modes for ensuring X/Y coordinates are in the valid range [0, size):

// Mirrors (repeating the edge pixel once). Useful for convolutions.
struct WrapMirror {
  JXL_INLINE int64_t operator()(const int64_t coord, const int64_t size) const {
    return Mirror(coord, size);
  }
};

// Returns the same coordinate: required for TFNode with Border(), or useful
// when we know "coord" is already valid (e.g. interior of an image).
struct WrapUnchanged {
  JXL_INLINE int64_t operator()(const int64_t coord, int64_t /*size*/) const {
    return coord;
  }
};

// Similar to Wrap* but for row pointers (reduces Row() multiplications).

class WrapRowMirror {
 public:
  template <class ImageOrView>
  WrapRowMirror(const ImageOrView& image, size_t ysize)
      : first_row_(image.ConstRow(0)), last_row_(image.ConstRow(ysize - 1)) {}

  const float* operator()(const float* const JXL_RESTRICT row,
                          const int64_t stride) const {
    if (row < first_row_) {
      const int64_t num_before = first_row_ - row;
      // Mirrored; one row before => row 0, two before = row 1, ...
      return first_row_ + num_before - stride;
    }
    if (row > last_row_) {
      const int64_t num_after = row - last_row_;
      // Mirrored; one row after => last row, two after = last - 1, ...
      return last_row_ - num_after + stride;
    }
    return row;
  }

 private:
  const float* const JXL_RESTRICT first_row_;
  const float* const JXL_RESTRICT last_row_;
};

struct WrapRowUnchanged {
  JXL_INLINE const float* operator()(const float* const JXL_RESTRICT row,
                                     int64_t /*stride*/) const {
    return row;
  }
};

// Initializes all planes to the same "value".
template <typename T>
void FillImage(const T value, Image3<T>* image) {
  for (size_t c = 0; c < 3; ++c) {
    for (size_t y = 0; y < image->ysize(); ++y) {
      T* JXL_RESTRICT row = image->PlaneRow(c, y);
      for (size_t x = 0; x < image->xsize(); ++x) {
        row[x] = value;
      }
    }
  }
}

template <typename T>
void FillPlane(const T value, Plane<T>* image) {
  for (size_t y = 0; y < image->ysize(); ++y) {
    T* JXL_RESTRICT row = image->Row(y);
    for (size_t x = 0; x < image->xsize(); ++x) {
      row[x] = value;
    }
  }
}

template <typename T>
void FillImage(const T value, Image3<T>* image, Rect rect) {
  for (size_t c = 0; c < 3; ++c) {
    for (size_t y = 0; y < rect.ysize(); ++y) {
      T* JXL_RESTRICT row = rect.PlaneRow(image, c, y);
      for (size_t x = 0; x < rect.xsize(); ++x) {
        row[x] = value;
      }
    }
  }
}

template <typename T>
void FillPlane(const T value, Plane<T>* image, Rect rect) {
  for (size_t y = 0; y < rect.ysize(); ++y) {
    T* JXL_RESTRICT row = rect.Row(image, y);
    for (size_t x = 0; x < rect.xsize(); ++x) {
      row[x] = value;
    }
  }
}

template <typename T>
void ZeroFillImage(Image3<T>* image) {
  for (size_t c = 0; c < 3; ++c) {
    for (size_t y = 0; y < image->ysize(); ++y) {
      T* JXL_RESTRICT row = image->PlaneRow(c, y);
      if (image->xsize() != 0) memset(row, 0, image->xsize() * sizeof(T));
    }
  }
}

template <typename T>
void ZeroFillPlane(Plane<T>* image, Rect rect) {
  for (size_t y = 0; y < rect.ysize(); ++y) {
    T* JXL_RESTRICT row = rect.Row(image, y);
    memset(row, 0, rect.xsize() * sizeof(T));
  }
}

// Same as above, but operates in-place. Assumes that the `in` image was
// allocated large enough.
void PadImageToBlockMultipleInPlace(Image3F* JXL_RESTRICT in);

}  // namespace jxl

#endif  // ENCODER_IMAGE_OPS_H_