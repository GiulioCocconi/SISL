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

#pragma once

#include <boost/multiprecision/cpp_int.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#if defined(_WIN32)
#if defined(SISL_STATIC)
#define SISL_API
#elif defined(SISL_BUILDING_LIBRARY)
#define SISL_API __declspec(dllexport)
#else
#define SISL_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define SISL_API __attribute__((visibility("default")))
#else
#define SISL_API
#endif

namespace sisl {

using Integer = boost::multiprecision::cpp_int;
using Operand = std::variant<Integer, std::string>;
using Operands = std::unordered_map<std::string, Operand>;

// Machine-readable diagnostic identity. Messages may become clearer over time;
// callers should branch on this value instead of matching message text.
enum class DiagnosticCode {
  ambiguous_encoding,
  duplicate_alias,
  duplicate_callable_name,
  duplicate_declaration,
  duplicate_encoding_field,
  duplicate_enum,
  duplicate_enum_member,
  duplicate_enum_value,
  duplicate_field,
  duplicate_format,
  duplicate_instruction,
  empty_enum,
  enum_value_out_of_range,
  field_slice_outside_width,
  fixed_value_out_of_range,
  format_inheritance_cycle,
  inconsistent_field_type,
  inconsistent_repeated_operand,
  instruction_encoding_overlap,
  invalid_alias_binding,
  invalid_alias_target,
  invalid_assembly,
  invalid_assembly_placeholder,
  invalid_assembly_template,
  invalid_bit_range,
  invalid_encoded_enum_value,
  invalid_endianness,
  invalid_enum_value,
  invalid_enum_width,
  invalid_instruction_value,
  invalid_instruction_width,
  invalid_isa_handle,
  invalid_numeric_operand,
  invalid_operand,
  invalid_primitive_width,
  invalid_symbolic_value,
  io_error,
  mapping_out_of_bounds,
  mapping_width_mismatch,
  missing_assembly_operand,
  missing_declaration,
  missing_operand,
  native_index_overflow,
  operand_out_of_range,
  overlapping_field_slice,
  overlapping_instruction_mapping,
  // Syntax diagnostics are category-level so callers can react without
  // matching parser-library-specific message text.
  expected_token,
  invalid_identifier,
  invalid_integer_literal,
  invalid_string_literal,
  unexpected_token,
  unexpected_end_of_file,
  trailing_input,
  invalid_syntax,
  unknown_encoding,
  unknown_encoding_field,
  unknown_enum_type,
  unknown_format_parent,
  unknown_instruction,
  unknown_instruction_format,
  unknown_operand,
  unknown_sliced_field,
  unmapped_field,
  unrepresentable_field_value,
  unrepresentable_operand,
};

// Returns the stable kebab-case spelling used in logs and serialized output.
[[nodiscard]] SISL_API std::string_view to_string(DiagnosticCode code) noexcept;

struct SISL_API SourceLocation {
  std::string source;
  std::optional<std::uint64_t> line;
  std::optional<std::uint64_t> column;
  std::optional<std::uint64_t> position;
  std::optional<std::uint64_t> span;

  friend bool operator==(const SourceLocation &,
                         const SourceLocation &) = default;
};

struct SISL_API Diagnostic {
  DiagnosticCode code;
  std::string message;
  std::optional<SourceLocation> source;
  std::vector<SourceLocation> related;

  friend bool operator==(const Diagnostic &, const Diagnostic &) = default;
};

class SISL_API Error : public std::runtime_error {
public:
  explicit Error(std::vector<Diagnostic> diagnostics);

  [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept;

private:
  std::vector<Diagnostic> diagnostics_;
};

struct SISL_API DecodedInstruction {
  std::string name;
  Operands operands;

  friend bool operator==(const DecodedInstruction &,
                         const DecodedInstruction &) = default;
};

namespace detail {
struct Model;
}

class SISL_API Isa {
public:
  [[nodiscard]] static Isa load_file(const std::filesystem::path &path);
  [[nodiscard]] static Isa load_string(std::string_view source);

  [[nodiscard]] Integer encode(std::string_view instruction,
                               const Operands &operands) const;
  [[nodiscard]] DecodedInstruction decode(const Integer &value) const;
  [[nodiscard]] Integer assemble(std::string_view source) const;
  [[nodiscard]] std::string disassemble(const Integer &value) const;
  [[nodiscard]] std::vector<std::byte>
  assemble_bytes(std::string_view source) const;
  [[nodiscard]] std::string
  disassemble_bytes(std::span<const std::byte> bytes) const;

private:
  explicit Isa(std::shared_ptr<const detail::Model> model) noexcept;

  std::shared_ptr<const detail::Model> model_;
};

} // namespace sisl
