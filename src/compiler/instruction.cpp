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

#include <algorithm>
#include <cctype>
#include <ranges>
#include <unordered_set>

namespace sisl::detail {
namespace {

bool is_identifier(std::string_view value) {
  const auto start = [](unsigned char character) {
    return std::isalpha(character) || character == '_';
  };
  const auto rest = [&](unsigned char character) {
    return start(character) || std::isdigit(character) || character == '-';
  };
  return !value.empty() && start(static_cast<unsigned char>(value.front())) &&
         std::ranges::all_of(value.begin() + 1, value.end(), rest);
}

bool regex_word_character(char character) {
  return std::isalnum(static_cast<unsigned char>(character)) ||
         character == '_';
}

std::string escape_regex_character(char character) {
  constexpr std::string_view special = R"(.^$|()[]*+?{}\-)";
  if (special.contains(character)) {
    return std::string{"\\"} + character;
  }
  return std::string(1, character);
}

std::string
build_assembly_regex_pattern(const std::vector<AssemblyPart> &parts) {
  std::string pattern = R"(^\s*)";
  for (std::size_t part_index = 0; part_index < parts.size(); ++part_index) {
    const auto &part = parts[part_index];

    // Compile operands as lazy captures so following punctuation bounds them.
    if (part.kind == AssemblyPart::Kind::operand) {
      pattern += "(.+?)";
      continue;
    }

    for (std::size_t literal_index = 0; literal_index < part.text.size();) {
      if (!std::isspace(static_cast<unsigned char>(part.text[literal_index]))) {
        pattern += escape_regex_character(part.text[literal_index++]);
        continue;
      }

      // Collapse each template whitespace run into a flexible regex boundary.
      const auto whitespace_begin = literal_index;
      while (
          literal_index < part.text.size() &&
          std::isspace(static_cast<unsigned char>(part.text[literal_index]))) {
        ++literal_index;
      }

      // Require whitespace only when omitting it would merge two word-like
      // tokens; around punctuation, zero or more whitespace remains valid.
      const bool left_is_word =
          whitespace_begin > 0
              ? regex_word_character(part.text[whitespace_begin - 1])
              : part_index > 0 &&
                    parts[part_index - 1].kind == AssemblyPart::Kind::operand;
      const bool right_is_word =
          literal_index < part.text.size()
              ? regex_word_character(part.text[literal_index])
              : part_index + 1 < parts.size() &&
                    parts[part_index + 1].kind == AssemblyPart::Kind::operand;
      pattern += left_is_word && right_is_word ? R"(\s+)" : R"(\s*)";
    }
  }
  pattern += R"(\s*$)";
  return pattern;
}

AssemblyTemplate
compile_assembly(const AssemblyDeclarationAst *occurrence,
                 const std::vector<std::string> &variable_fields,
                 const std::vector<Field> &fields,
                 DiagnosticSink &diagnostics) {
  // A missing required declaration has already been diagnosed. Return a
  // never-matching default so recovery models cannot accidentally assemble it.
  if (!occurrence) {
    return {{}, std::regex("a^")};
  }

  AssemblyTemplate result;
  const auto source = occurrence->value.source;
  const auto &text = occurrence->value.value;
  std::size_t cursor = 0;

  // Compile the template into alternating literal and operand parts.
  while (cursor < text.size()) {
    const auto open = text.find('{', cursor);
    const auto unmatched_close = text.find('}', cursor);

    // Check stray closing braces and continue after each one to find more
    // independent template errors.
    if (unmatched_close != std::string::npos &&
        (open == std::string::npos || unmatched_close < open)) {
      diagnostics.report(DiagnosticCode::invalid_assembly_template, source,
                         "unmatched }");
      cursor = unmatched_close + 1;
      continue;
    }

    // With no remaining placeholder, compile the remaining text as a literal.
    if (open == std::string::npos) {
      result.parts.push_back(
          {AssemblyPart::Kind::literal, text.substr(cursor)});
      break;
    }
    if (open > cursor) {
      result.parts.push_back(
          {AssemblyPart::Kind::literal, text.substr(cursor, open - cursor)});
    }

    // An unterminated placeholder consumes the remainder, so return to later
    // validation after reporting it instead of manufacturing an operand.
    const auto close = text.find('}', open + 1);
    if (close == std::string::npos) {
      diagnostics.report(DiagnosticCode::invalid_assembly_template, source,
                         "unterminated placeholder");
      break;
    }

    const auto name = text.substr(open + 1, close - open - 1);
    if (!is_identifier(name)) {
      diagnostics.report(DiagnosticCode::invalid_assembly_template, source,
                         "invalid placeholder name");
    }
    result.parts.push_back({AssemblyPart::Kind::operand, name});
    cursor = close + 1;
  }

  // Check that every placeholder names a variable field. Fixed and unknown
  // fields cannot receive values from assembly text.
  for (const auto &part : result.parts) {
    if (part.kind != AssemblyPart::Kind::operand) {
      continue;
    }
    const bool field_exists = find_field(fields, part.text) != nullptr;
    const bool variable =
        std::ranges::find(variable_fields, part.text) != variable_fields.end();
    if (!field_exists || !variable) {
      diagnostics.report(DiagnosticCode::invalid_assembly_placeholder, source,
                         part.text);
    }
  }

  // Check the inverse invariant: every variable field must be supplied by at
  // least one placeholder. Repeated placeholders remain supported.
  for (const auto &variable : variable_fields) {
    const bool present =
        std::ranges::any_of(result.parts, [&](const AssemblyPart &part) {
          return part.kind == AssemblyPart::Kind::operand &&
                 part.text == variable;
        });
    if (!present) {
      diagnostics.report(DiagnosticCode::missing_assembly_operand, source,
                         variable);
    }
  }

  // Compile the validated parts into the runtime matcher. Escaping should make
  // regex errors unreachable, but a never-match fallback keeps recovery safe.
  try {
    result.matcher = std::regex(build_assembly_regex_pattern(result.parts));
  } catch (const std::regex_error &) {
    result.matcher = std::regex("a^");
  }
  return result;
}

std::optional<Integer> compile_binding_value(const Field &field,
                                             const ValueAst &value,
                                             std::string_view context,
                                             DiagnosticSink &diagnostics) {
  Integer logical = 0;

  if (field.type.kind == PrimitiveType::enumeration) {
    // Enum bindings require a symbolic member rather than a raw integer.
    if (!std::holds_alternative<std::string>(value.value)) {
      diagnostics.report(DiagnosticCode::invalid_enum_value, value.source,
                         "unknown", context);
      return std::nullopt;
    }

    const auto &name = std::get<std::string>(value.value);
    const auto found = std::ranges::find_if(
        field.type.enumeration->members,
        [&](const auto &member) { return member.first == name; });
    if (found == field.type.enumeration->members.end()) {
      diagnostics.report(DiagnosticCode::invalid_enum_value, value.source, name,
                         context);
      return std::nullopt;
    }
    logical = found->second;
  } else {
    // Primitive bindings require numeric input.
    if (!std::holds_alternative<Integer>(value.value)) {
      diagnostics.report(DiagnosticCode::invalid_symbolic_value, value.source);
      return std::nullopt;
    }
    logical = std::get<Integer>(value.value);

    // Signed fields use two's-complement encoding after range validation.
    if (field.type.kind == PrimitiveType::signed_integer) {
      const auto limit = Integer{1} << (field.type.width - 1);
      if (logical < -limit || logical >= limit) {
        diagnostics.report(DiagnosticCode::fixed_value_out_of_range,
                           value.source, "signed");
        return std::nullopt;
      }
      if (logical < 0) {
        logical += Integer{1} << field.type.width;
      }
    } else if (logical < 0 || logical > low_mask(field.type.width)) {
      diagnostics.report(DiagnosticCode::fixed_value_out_of_range, value.source,
                         "unsigned");
      return std::nullopt;
    }
  }

  // Check scattered fields for logical bits that have no physical mapping.
  if ((logical & ~field.mapped_mask) != 0) {
    diagnostics.report(DiagnosticCode::unrepresentable_field_value,
                       value.source);
    return std::nullopt;
  }
  return logical;
}

std::vector<Field> lower_fields(std::vector<LayoutField> source_fields) {
  std::vector<Field> result;
  result.reserve(source_fields.size());
  for (auto &source_field : source_fields) {
    std::vector<Mapping> mappings;
    mappings.reserve(source_field.mappings.size());
    for (const auto &mapping : source_field.mappings) {
      mappings.push_back(
          Mapping{mapping.instruction_range, mapping.field_range});
    }
    result.push_back(Field{.name = std::move(source_field.name),
                           .type = std::move(source_field.type),
                           .mappings = std::move(mappings),
                           .mapped_mask = std::move(source_field.mapped_mask)});
  }
  return result;
}

} // namespace

// INSTRUCTION PASS
//
// Resolve each instruction's optional format, compile its completed layout and
// fixed encoding, then publish it for decoding and alias resolution. Duplicate
// declarations are skipped; recoverable local errors continue accumulating.
InstructionPass
compile_instructions(const std::vector<InstructionAst> &source_instructions,
                     const EnumIndex &enums, const FormatIndex &formats,
                     DiagnosticSink &diagnostics) {
  InstructionPass result;
  std::unordered_set<std::string> names;
  for (const auto &source_instruction : source_instructions) {
    // Check names before any expensive layout or regex compilation.
    if (!names.insert(source_instruction.name).second) {
      diagnostics.report(DiagnosticCode::duplicate_instruction,
                         source_instruction.source, source_instruction.name);
      continue;
    }

    // A missing format means an empty base by default. An explicit unresolved
    // format is diagnosed, then local declarations are still checked.
    const ResolvedLayout *base = nullptr;
    if (source_instruction.format) {
      const auto &reference = *source_instruction.format;
      const auto found = formats.find(reference.value);
      if (found == formats.end()) {
        diagnostics.report(DiagnosticCode::unknown_instruction_format,
                           reference.source, reference.value);
      } else {
        base = &found->second;
      }
    }

    auto layout =
        compile_layout(DeclarationOwner{"instruction", source_instruction.name,
                                        source_instruction.source},
                       base, source_instruction.layout, enums, diagnostics);

    // Every declared logical field must own physical bits before runtime use.
    for (const auto &field : layout.fields) {
      if (field.mappings.empty()) {
        diagnostics.report(DiagnosticCode::unmapped_field, field.source,
                           field.name);
      }
    }

    auto instruction = std::make_shared<Instruction>();
    instruction->name = source_instruction.name;
    instruction->width = layout.width.value_or(1);
    instruction->fields = lower_fields(std::move(layout.fields));

    std::unordered_map<std::string, Integer> fixed_values;
    const DeclarationOwner owner{"instruction", source_instruction.name,
                                 source_instruction.source};
    const auto *encoding = declaration(source_instruction.encoding, owner,
                                       "encoding", diagnostics);
    if (encoding) {
      for (const auto &binding : encoding->bindings) {
        // Check that the binding targets one known field.
        const auto *target = find_field(instruction->fields, binding.name);
        if (!target) {
          diagnostics.report(DiagnosticCode::unknown_encoding_field,
                             binding.source, binding.name);
          continue;
        }
        if (fixed_values.contains(binding.name)) {
          diagnostics.report(DiagnosticCode::duplicate_encoding_field,
                             binding.source);
          continue;
        }

        // Compile representable binding values; invalid values remain variable
        // only in the recovery model, which will never be returned.
        if (const auto compiled = compile_binding_value(
                *target, binding.value, source_instruction.name, diagnostics)) {
          fixed_values.emplace(binding.name, *compiled);
        }
      }
    }

    // Compile fixed logical values into the physical match value and mask.
    for (const auto &[name, value] : fixed_values) {
      const auto *target = find_field(instruction->fields, name);
      instruction->match = scatter(*target, value, instruction->match);
      for (const auto &mapping : target->mappings) {
        instruction->mask |= range_mask(mapping.instruction_range);
      }
    }

    // Fields without fixed encodings become caller-supplied operands by
    // default.
    for (const auto &field : instruction->fields) {
      if (!fixed_values.contains(field.name)) {
        instruction->variable_fields.push_back(field.name);
      }
    }

    const auto *assembly = declaration(source_instruction.assembly, owner,
                                       "assembly", diagnostics, true);
    instruction->assembly =
        compile_assembly(assembly, instruction->variable_fields,
                         instruction->fields, diagnostics);

    // Publish in declaration order for stable decode overlap behavior.
    result.ordered.push_back({instruction, source_instruction.source});
    result.index.emplace(instruction->name, std::move(instruction));
  }
  return result;
}

// INSTRUCTION OVERLAP VALIDATION
//
// Compare every compiled instruction pair after the instruction pass. Width
// differences are represented as fixed high bits so the test mirrors runtime
// width filtering.
void check_instruction_overlaps(const InstructionPass &instructions,
                                DiagnosticSink &diagnostics) {
  for (std::size_t left_index = 0; left_index < instructions.ordered.size();
       ++left_index) {
    for (std::size_t right_index = left_index + 1;
         right_index < instructions.ordered.size(); ++right_index) {
      const auto &left = *instructions.ordered[left_index].value;
      const auto &right = *instructions.ordered[right_index].value;
      const auto width = std::max(left.width, right.width);

      // Default bits above a narrower instruction's width to fixed zeroes.
      const auto all_bits = low_mask(width);
      const auto left_mask = left.mask | (all_bits ^ low_mask(left.width));
      const auto right_mask = right.mask | (all_bits ^ low_mask(right.width));
      const auto common = left_mask & right_mask;

      // If all commonly fixed bits agree, at least one value matches both.
      if (((left.match ^ right.match) & common) == 0) {
        diagnostics.report(DiagnosticCode::instruction_encoding_overlap,
                           instructions.ordered[right_index].source, left.name,
                           right.name);
      }
    }
  }
}

// ALIAS PASS
//
// Resolve aliases against the instruction index, add stricter fixed bindings,
// compile the remaining assembly operands, and publish aliases in declaration
// order so disassembly precedence remains stable.
AliasPass compile_aliases(const std::vector<AliasAst> &source_aliases,
                          const InstructionPass &instructions,
                          DiagnosticSink &diagnostics) {
  AliasPass result;
  std::unordered_set<std::string> names;
  for (const auto &source_alias : source_aliases) {
    // Check alias namespace collisions before target or binding compilation.
    if (!names.insert(source_alias.name).second) {
      diagnostics.report(DiagnosticCode::duplicate_alias, source_alias.source,
                         source_alias.name);
      continue;
    }
    if (instructions.index.contains(source_alias.name)) {
      diagnostics.report(DiagnosticCode::duplicate_callable_name,
                         source_alias.source);
    }

    const DeclarationOwner owner{"alias", source_alias.name,
                                 source_alias.source};
    const auto *target_declaration = declaration(
        source_alias.target, owner, "instruction target", diagnostics, true);

    // Return early when no target can be resolved; bindings require its fields.
    if (!target_declaration) {
      continue;
    }
    const auto target =
        instructions.index.find(target_declaration->value.value);
    if (target == instructions.index.end()) {
      diagnostics.report(DiagnosticCode::invalid_alias_target,
                         target_declaration->value.source);
      continue;
    }

    auto alias = std::make_shared<Alias>();
    alias->name = source_alias.name;
    alias->target = target->second;

    // Inherit the target encoding as the alias's default match constraint.
    alias->mask = alias->target->mask;
    alias->match = alias->target->match;

    const auto *encoding =
        declaration(source_alias.encoding, owner, "encoding", diagnostics);
    if (encoding) {
      for (const auto &binding : encoding->bindings) {
        const auto *target_field =
            find_field(alias->target->fields, binding.name);
        const bool variable =
            std::ranges::find(alias->target->variable_fields, binding.name) !=
            alias->target->variable_fields.end();

        // Aliases may bind each variable target field at most once; unknown or
        // already-fixed fields cannot further specialize the target.
        if (!target_field || !variable ||
            alias->bindings.contains(binding.name)) {
          diagnostics.report(DiagnosticCode::invalid_alias_binding,
                             binding.source, binding.name);
          continue;
        }

        const auto value = compile_binding_value(*target_field, binding.value,
                                                 alias->name, diagnostics);
        if (!value) {
          continue;
        }

        // Compile each valid alias binding into stricter match and mask bits.
        alias->bindings.emplace(binding.name, *value);
        alias->match = scatter(*target_field, *value, alias->match);
        for (const auto &mapping : target_field->mappings) {
          alias->mask |= range_mask(mapping.instruction_range);
        }
      }
    }

    // Unbound target operands remain variable by default.
    for (const auto &field_name : alias->target->variable_fields) {
      if (!alias->bindings.contains(field_name)) {
        alias->variable_fields.push_back(field_name);
      }
    }

    const auto *assembly = declaration(source_alias.assembly, owner, "assembly",
                                       diagnostics, true);
    alias->assembly = compile_assembly(assembly, alias->variable_fields,
                                       alias->target->fields, diagnostics);

    // Publish in declaration order, which is the documented alias priority.
    result.ordered.push_back(alias);
    result.index.emplace(alias->name, std::move(alias));
  }
  return result;
}

} // namespace sisl::detail
