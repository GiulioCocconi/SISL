#include "common/diagnostics.hpp"

#include <utility>

namespace sisl {

// Keep stable serialized names independent from the enum's declaration order.
// clang-format off
std::string_view to_string(const DiagnosticCode code) noexcept {
  switch (code) {
    case DiagnosticCode::ambiguous_encoding: return "ambiguous-encoding";
    case DiagnosticCode::duplicate_alias: return "duplicate-alias";
    case DiagnosticCode::duplicate_callable_name: return "duplicate-callable-name";
    case DiagnosticCode::duplicate_declaration: return "duplicate-declaration";
    case DiagnosticCode::duplicate_encoding_field: return "duplicate-encoding-field";
    case DiagnosticCode::duplicate_enum: return "duplicate-enum";
    case DiagnosticCode::duplicate_enum_member: return "duplicate-enum-member";
    case DiagnosticCode::duplicate_enum_value: return "duplicate-enum-value";
    case DiagnosticCode::duplicate_field: return "duplicate-field";
    case DiagnosticCode::duplicate_format: return "duplicate-format";
    case DiagnosticCode::duplicate_instruction: return "duplicate-instruction";
    case DiagnosticCode::empty_enum: return "empty-enum";
    case DiagnosticCode::enum_value_out_of_range: return "enum-value-out-of-range";
    case DiagnosticCode::field_slice_outside_width: return "field-slice-outside-width";
    case DiagnosticCode::fixed_value_out_of_range: return "fixed-value-out-of-range";
    case DiagnosticCode::format_inheritance_cycle: return "format-inheritance-cycle";
    case DiagnosticCode::inconsistent_field_type: return "inconsistent-field-type";
    case DiagnosticCode::inconsistent_repeated_operand: return "inconsistent-repeated-operand";
    case DiagnosticCode::instruction_encoding_overlap: return "instruction-encoding-overlap";
    case DiagnosticCode::invalid_alias_binding: return "invalid-alias-binding";
    case DiagnosticCode::invalid_alias_target: return "invalid-alias-target";
    case DiagnosticCode::invalid_assembly: return "invalid-assembly";
    case DiagnosticCode::invalid_assembly_placeholder: return "invalid-assembly-placeholder";
    case DiagnosticCode::invalid_assembly_template: return "invalid-assembly-template";
    case DiagnosticCode::invalid_bit_range: return "invalid-bit-range";
    case DiagnosticCode::invalid_encoded_enum_value: return "invalid-encoded-enum-value";
    case DiagnosticCode::invalid_endianness: return "invalid-endianness";
    case DiagnosticCode::invalid_enum_value: return "invalid-enum-value";
    case DiagnosticCode::invalid_enum_width: return "invalid-enum-width";
    case DiagnosticCode::invalid_instruction_value: return "invalid-instruction-value";
    case DiagnosticCode::invalid_instruction_width: return "invalid-instruction-width";
    case DiagnosticCode::invalid_isa_handle: return "invalid-isa-handle";
    case DiagnosticCode::invalid_numeric_operand: return "invalid-numeric-operand";
    case DiagnosticCode::invalid_operand: return "invalid-operand";
    case DiagnosticCode::invalid_primitive_width: return "invalid-primitive-width";
    case DiagnosticCode::invalid_symbolic_value: return "invalid-symbolic-value";
    case DiagnosticCode::io_error: return "io-error";
    case DiagnosticCode::mapping_out_of_bounds: return "mapping-outside-instruction";
    case DiagnosticCode::mapping_width_mismatch: return "mapping-width-mismatch";
    case DiagnosticCode::missing_assembly_operand: return "missing-assembly-operand";
    case DiagnosticCode::missing_declaration: return "missing-declaration";
    case DiagnosticCode::missing_operand: return "missing-operand";
    case DiagnosticCode::native_index_overflow: return "native-index-overflow";
    case DiagnosticCode::operand_out_of_range: return "operand-out-of-range";
    case DiagnosticCode::overlapping_field_slice: return "overlapping-field-slice";
    case DiagnosticCode::overlapping_instruction_mapping: return "overlapping-instruction-mapping";
    case DiagnosticCode::expected_token: return "expected-token";
    case DiagnosticCode::invalid_identifier: return "invalid-identifier";
    case DiagnosticCode::invalid_integer_literal: return "invalid-integer-literal";
    case DiagnosticCode::invalid_string_literal: return "invalid-string-literal";
    case DiagnosticCode::unexpected_token: return "unexpected-token";
    case DiagnosticCode::unexpected_end_of_file: return "unexpected-end-of-file";
    case DiagnosticCode::trailing_input: return "trailing-input";
    case DiagnosticCode::invalid_syntax: return "invalid-syntax";
    case DiagnosticCode::unknown_encoding: return "unknown-encoding";
    case DiagnosticCode::unknown_encoding_field: return "unknown-encoding-field";
    case DiagnosticCode::unknown_enum_type: return "unknown-enum-type";
    case DiagnosticCode::unknown_format_parent: return "unknown-format-parent";
    case DiagnosticCode::unknown_instruction: return "unknown-instruction";
    case DiagnosticCode::unknown_instruction_format: return "unknown-instruction-format";
    case DiagnosticCode::unknown_operand: return "unknown-operand";
    case DiagnosticCode::unknown_sliced_field: return "unknown-sliced-field";
    case DiagnosticCode::unmapped_field: return "unmapped-field";
    case DiagnosticCode::unrepresentable_field_value: return "unrepresentable-field-value";
    case DiagnosticCode::unrepresentable_operand: return "unrepresentable-operand";
  }
  return "unknown-diagnostic";
}
// clang-format on

Error::Error(std::vector<Diagnostic> diagnostics)
    : std::runtime_error(diagnostics.empty() ? "SISL operation failed"
                                             : diagnostics.front().message),
      diagnostics_(std::move(diagnostics)) {}

const std::vector<Diagnostic> &Error::diagnostics() const noexcept {
  return diagnostics_;
}

namespace detail {

// Codes handled by DiagnosticSink have one message template shared by all
// producers. Runtime errors still provide complete messages to raise_error().
// clang-format off
std::string_view diagnostic_message(const DiagnosticCode code) {
  switch (code) {
    case DiagnosticCode::invalid_endianness: return "invalid endianness: got {} should be Big or Little";
    case DiagnosticCode::duplicate_declaration: return "{} '{}' declares '{}' more than once";
    case DiagnosticCode::missing_declaration: return "{} '{}' is missing the required '{}' declaration";
    case DiagnosticCode::native_index_overflow: return "{} cannot be represented by the implementation";
    case DiagnosticCode::invalid_bit_range: return "invalid bit range: {}";
    case DiagnosticCode::invalid_primitive_width: return "invalid primitive width: {}";
    case DiagnosticCode::invalid_enum_width: return "invalid enum width: {}";
    case DiagnosticCode::invalid_instruction_width: return "invalid instruction width: {}";
    case DiagnosticCode::unknown_enum_type: return "unknown enum type: {}";
    case DiagnosticCode::duplicate_enum: return "duplicate enum: {}";
    case DiagnosticCode::empty_enum: return "enum '{}' must declare at least one member";
    case DiagnosticCode::duplicate_enum_member: return "duplicate member: {}";
    case DiagnosticCode::duplicate_enum_value: return "enum value assigned more than once";
    case DiagnosticCode::enum_value_out_of_range: return "enum member value does not fit its width";
    case DiagnosticCode::duplicate_field: return "duplicate field: {}";
    case DiagnosticCode::mapping_out_of_bounds: return "field '{}' maps to bit {}, but {} is only {} bits wide";
    case DiagnosticCode::overlapping_instruction_mapping: return "instruction-side mapping overlaps another mapping";
    case DiagnosticCode::unknown_sliced_field: return "mapping slice refers to undeclared field: {}";
    case DiagnosticCode::inconsistent_field_type: return "mapping gives the field an inconsistent type";
    case DiagnosticCode::field_slice_outside_width: return "field slice is outside the field width";
    case DiagnosticCode::mapping_width_mismatch: return "instruction and field mapping widths differ";
    case DiagnosticCode::overlapping_field_slice: return "field slice overlaps another slice";
    case DiagnosticCode::duplicate_format: return "duplicate format: {}";
    case DiagnosticCode::format_inheritance_cycle: return "format inheritance cycle involving {}";
    case DiagnosticCode::unknown_format_parent: return "unknown parent format: {}";
    case DiagnosticCode::invalid_assembly_template: return "invalid assembly template: {}";
    case DiagnosticCode::invalid_assembly_placeholder: return "invalid or fixed assembly placeholder: {}";
    case DiagnosticCode::missing_assembly_operand: return "assembly omits variable field: {}";
    case DiagnosticCode::invalid_enum_value: return "invalid enum value '{}' for {}";
    case DiagnosticCode::invalid_symbolic_value: return "numeric field requires an integer";
    case DiagnosticCode::fixed_value_out_of_range: return "{} value does not fit field";
    case DiagnosticCode::unrepresentable_field_value: return "value sets unmapped field bits";
    case DiagnosticCode::duplicate_instruction: return "duplicate instruction: {}";
    case DiagnosticCode::unknown_instruction_format: return "unknown instruction format: {}";
    case DiagnosticCode::unmapped_field: return "field '{}' has no mappings";
    case DiagnosticCode::unknown_encoding_field: return "unknown encoding field: {}";
    case DiagnosticCode::duplicate_encoding_field: return "field bound more than once";
    case DiagnosticCode::instruction_encoding_overlap: return "instruction encodings '{}' and '{}' overlap";
    case DiagnosticCode::duplicate_alias: return "duplicate alias: {}";
    case DiagnosticCode::duplicate_callable_name: return "alias conflicts with instruction";
    case DiagnosticCode::invalid_alias_target: return "alias targets unknown instruction";
    case DiagnosticCode::invalid_alias_binding: return "invalid alias binding: {}";
    case DiagnosticCode::expected_token: return "expected {}";
    case DiagnosticCode::invalid_identifier: return "expected identifier; found reserved keyword";
    case DiagnosticCode::invalid_integer_literal: return "malformed integer literal";
    case DiagnosticCode::invalid_string_literal: return "invalid string literal: {}";
    case DiagnosticCode::unexpected_token: return "unexpected token";
    case DiagnosticCode::unexpected_end_of_file: return "unexpected end-of-file: expected {}";
    case DiagnosticCode::trailing_input: return "unexpected input after architecture declaration";
    case DiagnosticCode::invalid_syntax: return "invalid syntax: {}";
    default: std::unreachable();
  }
}
// clang-format on

[[noreturn]] void raise_error(DiagnosticCode code, std::string message,
                              std::optional<SourceLocation> source) {
  throw Error({Diagnostic{code, std::move(message), std::move(source), {}}});
}

} // namespace detail
} // namespace sisl
