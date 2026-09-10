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
