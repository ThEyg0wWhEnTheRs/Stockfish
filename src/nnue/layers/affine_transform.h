/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2021 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Definition of layer AffineTransform of NNUE evaluation function

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#define NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED

#include <iostream>
#include "../nnue_common.h"

namespace Eval::NNUE::Layers {

  // Affine transformation layer
  template <typename PreviousLayer, IndexType OutputDimensions, bool UseRelu>
  class AffineTransform {
   public:
    // Input/output type
    using InputType = typename PreviousLayer::OutputType;
    using OutputType = typename std::conditional<UseRelu, std::uint8_t, std::int32_t>::type;
    static_assert(std::is_same<InputType, std::uint8_t>::value, "");

    // Number of input/output dimensions
    static constexpr IndexType kInputDimensions =
        PreviousLayer::kOutputDimensions;
    static constexpr IndexType kOutputDimensions = OutputDimensions;
    static constexpr IndexType kPaddedInputDimensions =
        CeilToMultiple<IndexType>(kInputDimensions, kMaxSimdWidth);

    // Size of forward propagation buffer used in this layer
    static constexpr std::size_t kSelfBufferSize =
        CeilToMultiple(kOutputDimensions * sizeof(OutputType), kCacheLineSize);

    // Size of the forward propagation buffer used from the input layer to this layer
    static constexpr std::size_t kBufferSize =
        PreviousLayer::kBufferSize + kSelfBufferSize;

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t GetHashValue() {
      std::uint32_t hash_value = 0xCC03DAE4u;
      hash_value += kOutputDimensions;
      hash_value ^= PreviousLayer::GetHashValue() >> 1;
      hash_value ^= PreviousLayer::GetHashValue() << 31;
      return hash_value;
    }

   // Read network parameters
    bool ReadParameters(std::istream& stream) {
      if (!previous_layer_.ReadParameters(stream)) return false;

      scale_ = read_little_endian<std::int32_t>(stream);
      scale_bits_ = read_little_endian<std::int32_t>(stream);
      weight_zero_point_ = read_little_endian<std::int32_t>(stream);

      for (std::size_t i = 0; i < kOutputDimensions; ++i)
        biases_[i] = read_little_endian<BiasType>(stream);
      for (std::size_t i = 0; i < kOutputDimensions * kPaddedInputDimensions; ++i)
        weights_[i] = read_little_endian<WeightType>(stream);

      return !stream.fail();
    }

    // Forward propagation
    const OutputType* Propagate(
        const TransformedFeatureType* transformed_features, char* buffer) const {
      const auto input = previous_layer_.Propagate(
          transformed_features, buffer + kSelfBufferSize);
      auto output = reinterpret_cast<OutputType*>(buffer);

      std::int32_t zero_point_sum = 0;
      for (IndexType j = 0; j < kInputDimensions; ++j)
        zero_point_sum += input[j];
      for (IndexType i = 0; i < kOutputDimensions; ++i) {
        const IndexType offset = i * kPaddedInputDimensions;

        std::int32_t sum = biases_[i];
        for (IndexType j = 0; j < kInputDimensions; ++j) {
          sum += weights_[offset + j] * input[j];
        }
        sum -= zero_point_sum * weight_zero_point_;
        if (UseRelu) {
          sum = rounding_shift(static_cast<std::int64_t>(sum) * scale_, scale_bits_);
          sum = std::max(std::min(sum, 255), 0);
        } else {
          sum = rounding_shift(static_cast<std::int64_t>(sum * 600) * scale_, scale_bits_);
        }
        output[i] = static_cast<OutputType>(sum);
      }

      return output;
    }

   private:
    using BiasType = std::int32_t;
    using WeightType = std::int8_t;

    PreviousLayer previous_layer_;

    // Quantization parameters
    std::int32_t scale_;
    std::int32_t scale_bits_;
    std::int32_t weight_zero_point_;

    alignas(kCacheLineSize) BiasType biases_[kOutputDimensions];
    alignas(kCacheLineSize) WeightType weights_[kOutputDimensions * kPaddedInputDimensions];
  };

}  // namespace Eval::NNUE::Layers

#endif // #ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
