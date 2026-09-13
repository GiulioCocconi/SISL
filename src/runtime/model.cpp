/*
 * Copyright (c) 2026 Giulio Cocconi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "runtime/model.hpp"

#include <ranges>

namespace sisl::detail {
namespace {

std::size_t range_width(BitRange range) { return range.msb - range.lsb + 1; }

} // namespace

Integer low_mask(std::size_t width) {
  if (width == 0) {
    return 0;
  }

  Integer mask = 1;
  mask <<= width;
  return mask - 1;
}

Integer range_mask(BitRange range) {
  return low_mask(range_width(range)) << range.lsb;
}

const Field *find_field(const std::vector<Field> &fields,
                        std::string_view name) {
  const auto found = std::ranges::find_if(
      fields, [&](const Field &field) { return field.name == name; });
  return found == fields.end() ? nullptr : &*found;
}

// Gather scattered physical instruction bits into one logical field value.
Integer gather(const Field &field, const Integer &instruction) {
  Integer logical = 0;
  for (const auto &mapping : field.mappings) {
    const auto width = range_width(mapping.instruction_range);
    const auto slice =
        (instruction >> mapping.instruction_range.lsb) & low_mask(width);
    logical |= slice << mapping.field_range.lsb;
  }
  return logical;
}

// Scatter a logical value into its physical instruction ranges, preserving
// every instruction bit that is not owned by the field.
Integer scatter(const Field &field, const Integer &logical,
                Integer instruction) {
  for (const auto &mapping : field.mappings) {
    const auto width = range_width(mapping.field_range);
    const auto instruction_mask = range_mask(mapping.instruction_range);
    const auto slice = (logical >> mapping.field_range.lsb) & low_mask(width);
    instruction &= ~instruction_mask;
    instruction |= slice << mapping.instruction_range.lsb;
  }
  return instruction;
}

} // namespace sisl::detail
