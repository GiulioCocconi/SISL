#pragma once

#include "common/diagnostics.hpp"
#include "compiler/compiler.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sisl::detail {

struct DeclarationOwner {
  std::string_view kind;
  std::string_view name;
  SourceSpan source;
};

template <typename T>
const T *declaration(const DeclarationAst<T> &declarations,
                     DeclarationOwner owner, std::string_view name,
                     DiagnosticSink &diagnostics, bool required = false) {
  // Check duplicate singleton declarations, but keep compiling from the first
  // occurrence so invalid duplicate contents cannot cascade.
  if (declarations.duplicate) {
    diagnostics.report(DiagnosticCode::duplicate_declaration,
                       *declarations.duplicate, owner.kind, owner.name, name);
  }

  // Check required declarations at their owner when no value is available.
  if (!declarations.first && required) {
    diagnostics.report(DiagnosticCode::missing_declaration, owner.source,
                       owner.kind, owner.name, name);
  }

  // Compile from the first declaration by default; optional declarations
  // return null and let their caller select the semantic default.
  return declarations.first ? &*declarations.first : nullptr;
}

using EnumIndex =
    std::unordered_map<std::string, std::shared_ptr<const CompiledEnum>>;

// Layouts retain compiler-only provenance and validation state while formats
// are inherited. Lowering removes these fields before runtime model creation.
struct LayoutMapping {
  BitRange instruction_range;
  BitRange field_range;
  SourceSpan source;
  std::optional<std::size_t> validated_width;
};

struct LayoutField {
  std::string name;
  FieldType type;
  std::vector<LayoutMapping> mappings;
  Integer mapped_mask = 0;
  SourceSpan source;
};

struct ResolvedLayout {
  std::optional<std::size_t> width;
  std::vector<LayoutField> fields;
};

using FormatIndex = std::unordered_map<std::string, ResolvedLayout>;

struct CompiledInstruction {
  std::shared_ptr<const Instruction> value;
  SourceSpan source;
};

struct InstructionPass {
  std::vector<CompiledInstruction> ordered;
  std::unordered_map<std::string, std::shared_ptr<const Instruction>> index;
};

struct AliasPass {
  std::vector<std::shared_ptr<const Alias>> ordered;
  std::unordered_map<std::string, std::shared_ptr<const Alias>> index;
};

[[nodiscard]] std::optional<std::size_t>
native_index(const Integer &value, SourceSpan source,
             std::string_view description, DiagnosticCode invalid_code,
             DiagnosticSink &diagnostics, bool positive = false);
[[nodiscard]] std::optional<BitRange>
compile_range(const RangeAst &range, DiagnosticSink &diagnostics);
[[nodiscard]] FieldType resolve_type(const TypeAst &type,
                                     const EnumIndex &enums,
                                     DiagnosticSink &diagnostics);

[[nodiscard]] EnumIndex compile_enums(const std::vector<EnumAst> &enums,
                                      DiagnosticSink &diagnostics);
[[nodiscard]] FormatIndex compile_formats(const std::vector<FormatAst> &formats,
                                          const EnumIndex &enums,
                                          DiagnosticSink &diagnostics);
[[nodiscard]] ResolvedLayout compile_layout(DeclarationOwner owner,
                                            const ResolvedLayout *base,
                                            const LayoutAst &layout,
                                            const EnumIndex &enums,
                                            DiagnosticSink &diagnostics);

[[nodiscard]] InstructionPass
compile_instructions(const std::vector<InstructionAst> &instructions,
                     const EnumIndex &enums, const FormatIndex &formats,
                     DiagnosticSink &diagnostics);
void check_instruction_overlaps(const InstructionPass &instructions,
                                DiagnosticSink &diagnostics);
[[nodiscard]] AliasPass compile_aliases(const std::vector<AliasAst> &aliases,
                                        const InstructionPass &instructions,
                                        DiagnosticSink &diagnostics);

} // namespace sisl::detail
