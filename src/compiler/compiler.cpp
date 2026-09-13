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

#include "compiler/internal.hpp"

#include <limits>
#include <ranges>
#include <unordered_set>
#include <utility>

namespace sisl::detail {

std::optional<std::size_t> native_index(const Integer &value, SourceSpan source,
                                        std::string_view description,
                                        DiagnosticCode invalid_code,
                                        DiagnosticSink &diagnostics,
                                        bool positive) {
  // Check the semantic lower bound before converting from arbitrary precision.
  if (value < 0 || (positive && value == 0)) {
    diagnostics.report(invalid_code, source,
                       positive ? "must be > 0" : "must be >= 0");
    return std::nullopt;
  }

  // Reject values the implementation cannot use as native container indexes.
  if (value > std::numeric_limits<std::size_t>::max()) {
    diagnostics.report(DiagnosticCode::native_index_overflow, source,
                       description);
    return std::nullopt;
  }
  return value.convert_to<std::size_t>();
}

std::optional<BitRange> compile_range(const RangeAst &range,
                                      DiagnosticSink &diagnostics) {
  const auto msb = native_index(range.msb, range.source, "bit index",
                                DiagnosticCode::invalid_bit_range, diagnostics);
  const auto lsb = native_index(range.lsb, range.source, "bit index",
                                DiagnosticCode::invalid_bit_range, diagnostics);

  // Return early when either endpoint is invalid; later range checks require
  // two safe native indexes.
  if (!msb || !lsb) {
    return std::nullopt;
  }

  // Check the inclusive range ordering before computing its width.
  if (*msb < *lsb) {
    diagnostics.report(DiagnosticCode::invalid_bit_range, range.source,
                       "msb < lsb");
    return std::nullopt;
  }
  return BitRange{*msb, *lsb};
}

FieldType resolve_type(const TypeAst &type, const EnumIndex &enums,
                       DiagnosticSink &diagnostics) {
  // Resolve named enums from the enum pass; use a harmless one-bit fallback
  // after an error so the containing declaration can complete its independent
  // checks.
  if (type.kind == PrimitiveType::enumeration) {
    const auto found = enums.find(type.name);
    if (found == enums.end()) {
      diagnostics.report(DiagnosticCode::unknown_enum_type, type.source,
                         type.name);
      return {};
    }
    return FieldType{.kind = PrimitiveType::enumeration,
                     .width = found->second->width,
                     .enumeration = found->second};
  }

  // Primitive widths must be positive native indexes. One bit is the recovery
  // default used only when diagnostics will prevent the model from escaping.
  const auto width =
      native_index(type.width, type.source, "primitive width",
                   DiagnosticCode::invalid_primitive_width, diagnostics, true);
  return FieldType{
      .kind = type.kind, .width = width.value_or(1), .enumeration = nullptr};
}

// ENUM PASS
//
// Validate symbolic operand types and publish each unique enum for layout type
// resolution. Invalid members are skipped so other members and enums are still
// checked; a one-bit width is the recovery default for an invalid declaration.
EnumIndex compile_enums(const std::vector<EnumAst> &source_enums,
                        DiagnosticSink &diagnostics) {
  EnumIndex result;
  std::unordered_set<std::string> names;
  for (const auto &source_enum : source_enums) {
    // Check declaration names before compilation and ignore duplicate bodies.
    if (!names.insert(source_enum.name).second) {
      diagnostics.report(DiagnosticCode::duplicate_enum, source_enum.source,
                         source_enum.name);
      continue;
    }

    auto compiled = std::make_shared<CompiledEnum>();
    compiled->width =
        native_index(source_enum.width.value, source_enum.width.source,
                     "enum width", DiagnosticCode::invalid_enum_width,
                     diagnostics, true)
            .value_or(1);

    // An enum must define at least one representable symbolic value.
    if (source_enum.members.empty()) {
      diagnostics.report(DiagnosticCode::empty_enum, source_enum.source,
                         source_enum.name);
    }

    std::unordered_set<std::string> member_names;
    for (const auto &member : source_enum.members) {
      const auto value = std::get<Integer>(member.value.value);

      // Check member names first; the first occurrence remains authoritative.
      if (!member_names.insert(member.name).second) {
        diagnostics.report(DiagnosticCode::duplicate_enum_member, member.source,
                           member.name);
        continue;
      }

      // Check encoded values for a one-to-one decode mapping.
      const bool duplicate_value =
          std::ranges::any_of(compiled->members, [&](const auto &previous) {
            return previous.second == value;
          });
      if (duplicate_value) {
        diagnostics.report(DiagnosticCode::duplicate_enum_value, member.source);
        continue;
      }

      // Check that the unsigned enum value fits its declared bit width.
      if (value < 0 || value > low_mask(compiled->width)) {
        diagnostics.report(DiagnosticCode::enum_value_out_of_range,
                           member.source);
        continue;
      }

      // Compile the validated member in declaration order for stable decoding.
      compiled->members.emplace_back(member.name, value);
    }

    // Publish the enum even after recoverable member errors so later passes can
    // validate all references before the accumulated diagnostics are thrown.
    result.emplace(source_enum.name, std::move(compiled));
  }
  return result;
}

// COMPILATION PIPELINE
//
// Each pass receives only the indexes it depends on and returns the data needed
// by later passes. Diagnostics are shared solely to accumulate independent
// errors; the semantic products themselves have no compiler-wide mutable owner.
std::shared_ptr<const Model> compile(const ArchitectureAst &ast,
                                     const SourceMap &source_map) {
  DiagnosticSink diagnostics(source_map);

  // Default architecture byte order to Little when no declaration is present.
  const DeclarationOwner architecture{"architecture", ast.name, ast.source};
  const auto *endianness_declaration =
      declaration(ast.endianness, architecture, "endianness", diagnostics);
  const auto endianness =
      endianness_declaration
          ? std::string_view{endianness_declaration->value.value}
          : std::string_view{"Little"};

  // Check the only two supported spellings before publishing the byte order.
  bool big_endian = false;
  if (endianness == "Big") {
    big_endian = true;
  } else if (endianness != "Little") {
    diagnostics.report(DiagnosticCode::invalid_endianness,
                       endianness_declaration->value.source, endianness);
  }

  // Compile in dependency order. Failed dependencies remain absent from pass
  // indexes, while independent declarations continue to be checked.
  auto enums = compile_enums(ast.enums, diagnostics);
  auto formats = compile_formats(ast.formats, enums, diagnostics);
  auto instructions =
      compile_instructions(ast.instructions, enums, formats, diagnostics);
  check_instruction_overlaps(instructions, diagnostics);
  auto aliases = compile_aliases(ast.aliases, instructions, diagnostics);

  // Return an immutable model only after every semantic check succeeds.
  if (!diagnostics.empty()) {
    throw Error(diagnostics.take());
  }

  auto model = std::make_shared<Model>();
  model->big_endian = big_endian;
  model->instruction_index = std::move(instructions.index);
  model->alias_index = std::move(aliases.index);
  for (auto &instruction : instructions.ordered) {
    model->instructions.push_back(std::move(instruction.value));
  }
  model->aliases = std::move(aliases.ordered);
  return model;
}

} // namespace sisl::detail
