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

#include "common/diagnostics.hpp"
#include "compiler/compiler.hpp"
#include "parser/ast.hpp"
#include "runtime/model.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <utility>

namespace sisl {
namespace {

using detail::Alias;
using detail::AssemblyPart;
using detail::AssemblyTemplate;
using detail::Field;
using detail::Instruction;
using detail::Model;
using detail::PrimitiveType;

const Model &require_model(const std::shared_ptr<const Model> &model) {
  if (!model) {
    detail::raise_error(DiagnosticCode::invalid_isa_handle,
                        "the ISA object has been moved from");
  }
  return *model;
}

[[noreturn]] void runtime_error(DiagnosticCode code, std::string message) {
  detail::raise_error(code, std::move(message));
}

std::string_view trim(std::string_view text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  return text;
}

std::optional<Integer> parse_number(std::string_view text) {
  auto spelling = trim(text);
  bool negative = false;
  if (!spelling.empty() && spelling.front() == '-') {
    negative = true;
    spelling.remove_prefix(1);
  }
  if (spelling.empty()) {
    return std::nullopt;
  }

  unsigned radix = 10;
  std::size_t digits_begin = 0;
  if (spelling.size() > 2 && spelling.front() == '0') {
    switch (spelling[1]) {
    case 'b':
    case 'B':
      radix = 2;
      break;
    case 'o':
    case 'O':
      radix = 8;
      break;
    case 'x':
    case 'X':
      radix = 16;
      break;
    default:
      break;
    }
    if (radix != 10) {
      digits_begin = 2;
    }
  }
  if (digits_begin == spelling.size()) {
    return std::nullopt;
  }

  Integer value = 0;
  for (std::size_t index = digits_begin; index < spelling.size(); ++index) {
    const char character = spelling[index];
    unsigned digit = radix;
    if (character >= '0' && character <= '9') {
      digit = static_cast<unsigned>(character - '0');
    } else if (character >= 'a' && character <= 'f') {
      digit = static_cast<unsigned>(character - 'a' + 10);
    } else if (character >= 'A' && character <= 'F') {
      digit = static_cast<unsigned>(character - 'A' + 10);
    }
    if (digit >= radix) {
      return std::nullopt;
    }
    value *= radix;
    value += digit;
  }
  return negative ? -value : value;
}

Integer enum_member_value(const Field &field, const Operand &operand) {
  if (!std::holds_alternative<std::string>(operand)) {
    runtime_error(DiagnosticCode::invalid_enum_value,
                  "enum operand " + field.name + " requires a member name");
  }

  const auto &name = std::get<std::string>(operand);
  const auto member = std::find_if(
      field.type.enumeration->members.begin(),
      field.type.enumeration->members.end(),
      [&](const auto &candidate) { return candidate.first == name; });
  if (member == field.type.enumeration->members.end()) {
    runtime_error(DiagnosticCode::invalid_enum_value,
                  "invalid enum member " + name + " for operand " + field.name);
  }
  return member->second;
}

Integer operand_to_logical(const Field &field, const Operand &operand) {
  Integer logical = 0;
  if (field.type.kind == PrimitiveType::enumeration) {
    logical = enum_member_value(field, operand);
  } else {
    if (!std::holds_alternative<Integer>(operand)) {
      runtime_error(DiagnosticCode::invalid_operand,
                    "operand " + field.name + " requires an integer");
    }
    logical = std::get<Integer>(operand);

    if (field.type.kind == PrimitiveType::signed_integer) {
      const auto limit = Integer{1} << (field.type.width - 1);
      if (logical < -limit || logical >= limit) {
        runtime_error(DiagnosticCode::operand_out_of_range,
                      "signed operand " + field.name + " is out of range");
      }
      if (logical < 0) {
        logical += Integer{1} << field.type.width;
      }
    } else if (logical < 0 || logical > detail::low_mask(field.type.width)) {
      runtime_error(DiagnosticCode::operand_out_of_range,
                    "unsigned operand " + field.name + " is out of range");
    }
  }

  if ((logical & ~field.mapped_mask) != 0) {
    runtime_error(DiagnosticCode::unrepresentable_operand,
                  "operand " + field.name +
                      " sets bits not represented by its mappings");
  }
  return logical;
}

struct EncodedInstruction {
  Integer value;
  std::size_t width;
};

EncodedInstruction encode_model(const Model &model, std::string_view name,
                                const Operands &operands) {
  const Instruction *instruction = nullptr;
  Integer initial = 0;
  const std::vector<std::string> *variable_fields = nullptr;

  if (const auto alias = model.alias_index.find(std::string(name));
      alias != model.alias_index.end()) {
    instruction = alias->second->target.get();
    initial = alias->second->match;
    variable_fields = &alias->second->variable_fields;
  } else if (const auto found = model.instruction_index.find(std::string(name));
             found != model.instruction_index.end()) {
    instruction = found->second.get();
    initial = instruction->match;
    variable_fields = &instruction->variable_fields;
  } else {
    runtime_error(DiagnosticCode::unknown_instruction,
                  "unknown instruction or alias " + std::string(name));
  }

  for (const auto &field_name : *variable_fields) {
    if (!operands.contains(field_name)) {
      runtime_error(DiagnosticCode::missing_operand,
                    "missing operand " + field_name);
    }
  }
  for (const auto &[field_name, ignored] : operands) {
    if (std::find(variable_fields->begin(), variable_fields->end(),
                  field_name) == variable_fields->end()) {
      runtime_error(DiagnosticCode::unknown_operand,
                    "unknown or fixed operand " + field_name);
    }
  }

  auto value = initial;
  for (const auto &field_name : *variable_fields) {
    const auto *field = detail::find_field(instruction->fields, field_name);
    const auto logical = operand_to_logical(*field, operands.at(field_name));
    value = detail::scatter(*field, logical, value);
  }
  return {value, instruction->width};
}

std::pair<DecodedInstruction, const Instruction *>
decode_model(const Model &model, const Integer &value,
             std::optional<std::size_t> byte_size = std::nullopt) {
  if (value < 0) {
    runtime_error(DiagnosticCode::invalid_instruction_value,
                  "instruction value must be nonnegative");
  }

  const Instruction *candidate = nullptr;
  for (const auto &instruction : model.instructions) {
    const bool fits_width = value < (Integer{1} << instruction->width);
    const bool fits_byte_size =
        !byte_size || *byte_size == (instruction->width + 7) / 8;
    const bool fixed_bits_match =
        (value & instruction->mask) == instruction->match;
    if (fits_width && fits_byte_size && fixed_bits_match) {
      if (candidate != nullptr) {
        runtime_error(DiagnosticCode::ambiguous_encoding,
                      "multiple instructions match value " + value.str());
      }
      candidate = instruction.get();
    }
  }

  if (candidate == nullptr) {
    runtime_error(DiagnosticCode::unknown_encoding,
                  "no instruction matches value " + value.str());
  }
  const auto *instruction = candidate;
  DecodedInstruction decoded;
  decoded.name = instruction->name;
  for (const auto &field_name : instruction->variable_fields) {
    const auto *field = detail::find_field(instruction->fields, field_name);
    auto logical = detail::gather(*field, value);

    if (field->type.kind == PrimitiveType::enumeration) {
      const auto member = std::find_if(
          field->type.enumeration->members.begin(),
          field->type.enumeration->members.end(),
          [&](const auto &candidate) { return candidate.second == logical; });
      if (member == field->type.enumeration->members.end()) {
        runtime_error(DiagnosticCode::invalid_encoded_enum_value,
                      "encoded value is not an enum member");
      }
      decoded.operands.emplace(field_name, member->first);
    } else {
      const bool negative =
          field->type.kind == PrimitiveType::signed_integer &&
          (logical & (Integer{1} << (field->type.width - 1))) != 0;
      if (negative) {
        logical -= Integer{1} << field->type.width;
      }
      decoded.operands.emplace(field_name, logical);
    }
  }
  return {std::move(decoded), instruction};
}

Operands parse_captures(
    const Instruction &instruction, const AssemblyTemplate &assembly,
    const std::match_results<std::string_view::const_iterator> &captures) {
  Operands operands;
  std::size_t capture_index = 1;

  for (const auto &part : assembly.parts) {
    if (part.kind != AssemblyPart::Kind::operand) {
      continue;
    }
    const auto *field = detail::find_field(instruction.fields, part.text);
    Operand value;
    if (field->type.kind == PrimitiveType::enumeration) {
      value = std::string(trim(captures[capture_index].str()));
    } else {
      const auto number = parse_number(captures[capture_index].str());
      if (!number) {
        runtime_error(DiagnosticCode::invalid_numeric_operand,
                      "invalid number for operand " + part.text);
      }
      value = *number;
    }

    if (const auto previous = operands.find(part.text);
        previous != operands.end() && previous->second != value) {
      runtime_error(DiagnosticCode::inconsistent_repeated_operand,
                    "repeated operand has inconsistent values");
    }
    operands[part.text] = std::move(value);
    ++capture_index;
  }
  return operands;
}

template <typename Callable>
std::optional<EncodedInstruction>
try_assembly_candidate(const Model &model, std::string_view source,
                       const Callable &callable, const Instruction &instruction,
                       const AssemblyTemplate &assembly,
                       std::optional<Error> &first_error) {
  std::match_results<std::string_view::const_iterator> captures;
  if (!std::regex_match(source.begin(), source.end(), captures,
                        assembly.matcher)) {
    return std::nullopt;
  }

  try {
    return encode_model(model, callable.name,
                        parse_captures(instruction, assembly, captures));
  } catch (const Error &error) {
    if (!first_error) {
      first_error = error;
    }
    return std::nullopt;
  }
}

EncodedInstruction assemble_model(const Model &model, std::string_view source) {
  std::optional<Error> first_error;
  for (const auto &alias : model.aliases) {
    if (const auto result =
            try_assembly_candidate(model, source, *alias, *alias->target,
                                   alias->assembly, first_error)) {
      return *result;
    }
  }
  for (const auto &instruction : model.instructions) {
    if (const auto result =
            try_assembly_candidate(model, source, *instruction, *instruction,
                                   instruction->assembly, first_error)) {
      return *result;
    }
  }
  if (first_error) {
    throw *first_error;
  }
  runtime_error(DiagnosticCode::invalid_assembly,
                "source does not match any instruction");
}

std::string render_assembly(const AssemblyTemplate &assembly,
                            const Operands &operands) {
  std::string result;
  for (const auto &part : assembly.parts) {
    if (part.kind == AssemblyPart::Kind::literal) {
      result += part.text;
      continue;
    }
    const auto &operand = operands.at(part.text);
    if (std::holds_alternative<std::string>(operand)) {
      result += std::get<std::string>(operand);
    } else {
      result += std::get<Integer>(operand).str();
    }
  }
  return result;
}

std::string
disassemble_model(const Model &model, const Integer &value,
                  std::optional<std::size_t> byte_size = std::nullopt) {
  auto [decoded, instruction] = decode_model(model, value, byte_size);
  for (const auto &alias : model.aliases) {
    const bool same_instruction = alias->target->name == instruction->name;
    const bool alias_matches = (value & alias->mask) == alias->match;
    if (same_instruction && alias_matches) {
      auto operands = decoded.operands;
      for (const auto &[name, ignored] : alias->bindings) {
        operands.erase(name);
      }
      return render_assembly(alias->assembly, operands);
    }
  }
  return render_assembly(instruction->assembly, decoded.operands);
}

std::shared_ptr<const Model> load_model(std::string_view source,
                                        std::string source_name) {
  detail::SourceMap source_map(source, std::move(source_name));
  const auto ast = detail::parse_source(source, source_map);
  return detail::compile(ast, source_map);
}

} // namespace

Isa::Isa(std::shared_ptr<const detail::Model> model) noexcept
    : model_(std::move(model)) {}

Isa Isa::load_string(std::string_view source) {
  return Isa(load_model(source, "isa-string"));
}

Isa Isa::load_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    detail::raise_error(DiagnosticCode::io_error,
                        "cannot open ISA file " + path.string());
  }
  const std::string source{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
  return Isa(load_model(source, path.string()));
}

Integer Isa::encode(std::string_view instruction,
                    const Operands &operands) const {
  return encode_model(require_model(model_), instruction, operands).value;
}

DecodedInstruction Isa::decode(const Integer &value) const {
  return decode_model(require_model(model_), value).first;
}

Integer Isa::assemble(std::string_view source) const {
  return assemble_model(require_model(model_), source).value;
}

std::string Isa::disassemble(const Integer &value) const {
  return disassemble_model(require_model(model_), value);
}

std::vector<std::byte> Isa::assemble_bytes(std::string_view source) const {
  const auto &model = require_model(model_);
  const auto encoded = assemble_model(model, source);
  const auto size = (encoded.width + 7) / 8;
  std::vector<std::byte> result(size);

  for (std::size_t index = 0; index < size; ++index) {
    const auto source_index = model.big_endian ? size - index - 1 : index;
    const auto byte =
        ((encoded.value >> (8 * source_index)) & 0xff).convert_to<unsigned>();
    result[index] = static_cast<std::byte>(byte);
  }
  return result;
}

std::string Isa::disassemble_bytes(std::span<const std::byte> bytes) const {
  const auto &model = require_model(model_);
  Integer value = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const auto shift =
        8 * (model.big_endian ? bytes.size() - index - 1 : index);
    value |= Integer{std::to_integer<unsigned>(bytes[index])} << shift;
  }
  return disassemble_model(model, value, bytes.size());
}

} // namespace sisl
