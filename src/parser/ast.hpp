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

#include "common/primitive_type.hpp"
#include "common/source_map.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace sisl::detail {

template <typename T> struct Sourced {
  T value;
  SourceSpan source;
};

using SourceInteger = Sourced<Integer>;
using SourceString = Sourced<std::string>;

// Singleton declarations preserve the first usable value plus one duplicate
// location. More duplicates do not add information to the eventual diagnostic.
template <typename T> struct DeclarationAst {
  void add(T declaration) {
    if (!first) {
      first = std::move(declaration);
    } else if (!duplicate) {
      duplicate = declaration.source;
    }
  }

  std::optional<T> first;
  std::optional<SourceSpan> duplicate;
};

struct TypeAst {
  PrimitiveType kind = PrimitiveType::bits;
  Integer width = 1;
  std::string name;
  SourceSpan source;
};

struct RangeAst {
  Integer msb;
  Integer lsb;
  SourceSpan source;
};

struct ValueAst {
  std::variant<Integer, std::string> value;
  SourceSpan source;
};

struct BindingAst {
  std::string name;
  ValueAst value;
  SourceSpan source;
};

struct FieldAst {
  std::string name;
  TypeAst type;
  SourceSpan source;
};

struct MappingAst {
  RangeAst instruction_range;
  std::string field_name;
  std::optional<RangeAst> field_range;
  std::optional<TypeAst> explicit_type;
  SourceSpan source;
};

struct WidthDeclarationAst {
  SourceInteger value;
  SourceSpan source;
};

struct LayoutAst {
  DeclarationAst<WidthDeclarationAst> width;
  std::vector<FieldAst> fields;
  std::vector<MappingAst> mappings;
};

struct EnumAst {
  std::string name;
  SourceInteger width;
  std::vector<BindingAst> members;
  SourceSpan source;
};

struct FormatAst {
  std::string name;
  std::optional<SourceString> parent;
  LayoutAst layout;
  SourceSpan source;
};

struct AssemblyDeclarationAst {
  SourceString value;
  SourceSpan source;
};

struct EncodingAst {
  std::vector<BindingAst> bindings;
  SourceSpan source;
};

struct InstructionAst {
  std::string name;
  std::optional<SourceString> format;
  LayoutAst layout;
  DeclarationAst<EncodingAst> encoding;
  DeclarationAst<AssemblyDeclarationAst> assembly;
  SourceSpan source;
};

struct AliasTargetAst {
  SourceString value;
  SourceSpan source;
};

struct AliasAst {
  std::string name;
  DeclarationAst<AliasTargetAst> target;
  DeclarationAst<EncodingAst> encoding;
  DeclarationAst<AssemblyDeclarationAst> assembly;
  SourceSpan source;
};

struct EndiannessDeclarationAst {
  SourceString value;
  SourceSpan source;
};

struct ArchitectureAst {
  std::string name;
  DeclarationAst<EndiannessDeclarationAst> endianness;
  std::vector<EnumAst> enums;
  std::vector<FormatAst> formats;
  std::vector<InstructionAst> instructions;
  std::vector<AliasAst> aliases;
  SourceSpan source;
};

[[nodiscard]] ArchitectureAst parse_source(std::string_view text,
                                           const SourceMap &source_map);

} // namespace sisl::detail
