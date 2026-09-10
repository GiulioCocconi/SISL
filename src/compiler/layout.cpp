#include "compiler/internal.hpp"

#include <functional>
#include <ranges>
#include <unordered_map>

namespace sisl::detail {
namespace {

enum class FormatState { unvisited, visiting, succeeded, failed };

std::size_t range_width(BitRange range) { return range.msb - range.lsb + 1; }

bool ranges_overlap(BitRange left, BitRange right) {
  return !(left.msb < right.lsb || right.msb < left.lsb);
}

bool same_type(const FieldType &left, const FieldType &right) {
  return left.kind == right.kind && left.width == right.width &&
         left.enumeration == right.enumeration;
}

LayoutField *find_layout_field(std::vector<LayoutField> &fields,
                               std::string_view name) {
  const auto found = std::ranges::find_if(
      fields, [&](const LayoutField &field) { return field.name == name; });
  return found == fields.end() ? nullptr : &*found;
}

void resolve_layout_width(ResolvedLayout &result, DeclarationOwner owner,
                          const LayoutAst &layout,
                          DiagnosticSink &diagnostics) {
  const auto inherited_width = result.width;

  // Instructions require a width unless their format supplies the default.
  // Formats may remain widthless until a descendant establishes the width.
  const bool instruction = owner.kind == "instruction";
  const auto *declared_width =
      declaration(layout.width, owner, "width", diagnostics,
                  instruction && !inherited_width);

  // Check that an inherited width is not redeclared. The inherited value stays
  // authoritative, allowing subsequent bounds checks to use a stable width.
  if (declared_width && inherited_width) {
    diagnostics.report(DiagnosticCode::duplicate_declaration,
                       declared_width->source, owner.kind, owner.name, "width");
    return;
  }

  // Compile a new width when no parent supplied one. An invalid value leaves
  // the layout widthless so downstream checks can avoid unsafe assumptions.
  if (declared_width) {
    result.width = native_index(
        declared_width->value.value, declared_width->value.source,
        "instruction width", DiagnosticCode::invalid_instruction_width,
        diagnostics, true);
  }
}

void compile_explicit_fields(std::vector<LayoutField> &compiled_fields,
                             const std::vector<FieldAst> &source_fields,
                             const EnumIndex &enums,
                             DiagnosticSink &diagnostics) {
  for (const auto &[name, type, source] : source_fields) {
    // Check inherited and local names together; fields cannot be shadowed.
    if (find_layout_field(compiled_fields, name) != nullptr) {
      diagnostics.report(DiagnosticCode::duplicate_field, source, name);
      continue;
    }

    // Compile the declared logical type before any physical mappings refer to
    // the field. A type error uses resolve_type's safe recovery value.
    compiled_fields.push_back(
        LayoutField{.name = name,
                    .type = resolve_type(type, enums, diagnostics),
                    .mappings = {},
                    .mapped_mask = 0,
                    .source = source});
  }
}

LayoutField &get_or_declare_mapped_field(std::vector<LayoutField> &fields,
                                         const MappingAst &source_mapping,
                                         BitRange instruction_range,
                                         std::optional<FieldType> annotation,
                                         DiagnosticSink &diagnostics) {
  // Reuse explicit or inherited declarations by default.
  if (auto *field = find_layout_field(fields, source_mapping.field_name)) {
    return *field;
  }

  // A sliced mapping needs a prior declaration to define the complete field.
  // Continue with an inferred recovery field to find independent errors.
  if (source_mapping.field_range) {
    diagnostics.report(DiagnosticCode::unknown_sliced_field,
                       source_mapping.source, source_mapping.field_name);
  }

  // Prefer an explicit annotation. Otherwise infer an unsigned bit field whose
  // default width is the physical mapping width.
  FieldType inferred;
  if (annotation) {
    inferred = *annotation;
  } else {
    inferred.kind = PrimitiveType::bits;
    inferred.width = range_width(instruction_range);
    if (source_mapping.field_range) {
      if (const auto field_range =
              compile_range(*source_mapping.field_range, diagnostics)) {
        inferred.width = field_range->msb + 1;
      }
    }
  }

  fields.push_back(LayoutField{.name = source_mapping.field_name,
                               .type = inferred,
                               .mappings = {},
                               .mapped_mask = 0,
                               .source = source_mapping.source});
  return fields.back();
}

void compile_mapping(ResolvedLayout &layout, const MappingAst &source_mapping,
                     std::vector<BitRange> &occupied_ranges,
                     const EnumIndex &enums, DiagnosticSink &diagnostics) {
  const auto instruction_range =
      compile_range(source_mapping.instruction_range, diagnostics);

  // Return early because every remaining check requires a valid physical range.
  if (!instruction_range) {
    return;
  }

  // Check that this physical range does not collide with inherited or earlier
  // local mappings. Keep it for recovery so field-side errors are also found.
  if (std::ranges::any_of(occupied_ranges, [&](BitRange previous) {
        return ranges_overlap(previous, *instruction_range);
      })) {
    diagnostics.report(DiagnosticCode::overlapping_instruction_mapping,
                       source_mapping.source);
  }

  // Compile an optional type annotation before choosing or inferring the field.
  const auto annotation =
      source_mapping.explicit_type
          ? std::make_optional(
                resolve_type(*source_mapping.explicit_type, enums, diagnostics))
          : std::nullopt;
  auto &field =
      get_or_declare_mapped_field(layout.fields, source_mapping,
                                  *instruction_range, annotation, diagnostics);

  // Check that mapping annotations agree with prior field declarations.
  if (annotation && !same_type(*annotation, field.type)) {
    diagnostics.report(DiagnosticCode::inconsistent_field_type,
                       source_mapping.source);
  }

  // An omitted logical slice maps the complete field by default.
  const auto field_range =
      source_mapping.field_range
          ? compile_range(*source_mapping.field_range, diagnostics)
          : std::optional<BitRange>{BitRange{field.type.width - 1, 0}};

  // Return early when an explicit logical range could not be represented.
  if (!field_range) {
    return;
  }

  // Check the logical slice before compiling it into the field mapping.
  if (field_range->msb >= field.type.width) {
    diagnostics.report(DiagnosticCode::field_slice_outside_width,
                       source_mapping.source);
  }
  if (range_width(*instruction_range) != range_width(*field_range)) {
    diagnostics.report(DiagnosticCode::mapping_width_mismatch,
                       source_mapping.source);
  }
  if (std::ranges::any_of(field.mappings, [&](const LayoutMapping &previous) {
        return ranges_overlap(previous.field_range, *field_range);
      })) {
    diagnostics.report(DiagnosticCode::overlapping_field_slice,
                       source_mapping.source);
  }

  // Compile the mapping and record both occupied physical and logical bits for
  // later overlap and representability checks.
  field.mappings.push_back(
      LayoutMapping{.instruction_range = *instruction_range,
                    .field_range = *field_range,
                    .source = source_mapping.source,
                    .validated_width = std::nullopt});
  field.mapped_mask |= range_mask(*field_range);
  occupied_ranges.push_back(*instruction_range);
}

void validate_layout_bounds(ResolvedLayout &layout, DeclarationOwner owner,
                            DiagnosticSink &diagnostics) {
  // A widthless format intentionally defers physical bounds checks until a
  // descendant format or instruction supplies the width.
  if (!layout.width) {
    return;
  }

  for (auto &field : layout.fields) {
    for (auto &mapping : field.mappings) {
      // Inherited mappings already checked against the same immutable width do
      // not emit duplicate diagnostics in each descendant.
      if (mapping.validated_width == layout.width) {
        continue;
      }

      if (mapping.instruction_range.msb >= *layout.width) {
        diagnostics.report(
            DiagnosticCode::mapping_out_of_bounds, mapping.source, field.name,
            mapping.instruction_range.msb, owner.name, *layout.width);
      }
      mapping.validated_width = layout.width;
    }
  }
}

} // namespace

ResolvedLayout compile_layout(DeclarationOwner owner,
                              const ResolvedLayout *base,
                              const LayoutAst &layout, const EnumIndex &enums,
                              DiagnosticSink &diagnostics) {
  ResolvedLayout result;

  // Begin from the inherited format when present; otherwise width and fields
  // use their empty defaults until local declarations compile.
  if (base) {
    result = *base;
  }

  resolve_layout_width(result, owner, layout, diagnostics);
  compile_explicit_fields(result.fields, layout.fields, enums, diagnostics);

  // Seed occupied ranges from inherited mappings before compiling local ones.
  std::vector<BitRange> occupied_ranges;
  for (const auto &field : result.fields) {
    for (const auto &mapping : field.mappings) {
      occupied_ranges.push_back(mapping.instruction_range);
    }
  }

  for (const auto &source_mapping : layout.mappings) {
    compile_mapping(result, source_mapping, occupied_ranges, enums,
                    diagnostics);
  }

  // Validate all newly bounded mappings once, after width inheritance and local
  // mapping compilation have established the complete layout.
  validate_layout_bounds(result, owner, diagnostics);
  return result;
}

// FORMAT PASS
//
// Resolve the format inheritance graph in dependency order and publish compiled
// layouts for instructions. Explicit failure state prevents missing parents or
// failed ancestors from being mistaken for cycles during later resolutions.
FormatIndex compile_formats(const std::vector<FormatAst> &source_formats,
                            const EnumIndex &enums,
                            DiagnosticSink &diagnostics) {
  std::unordered_map<std::string, const FormatAst *> source_by_name;
  std::vector<std::string> declaration_order;

  // Check duplicate names while retaining the first declaration as the graph
  // node and preserving source order for deterministic diagnostics.
  for (const auto &format : source_formats) {
    if (!source_by_name.emplace(format.name, &format).second) {
      diagnostics.report(DiagnosticCode::duplicate_format, format.source,
                         format.name);
      continue;
    }
    declaration_order.push_back(format.name);
  }

  FormatIndex result;
  std::unordered_map<std::string, FormatState> states;
  std::function<const ResolvedLayout *(const std::string &)> resolve;

  resolve = [&](const std::string &name) -> const ResolvedLayout * {
    // Return the published layout after a successful earlier resolution.
    if (states[name] == FormatState::succeeded) {
      return &result.at(name);
    }

    // Return early from known failures so descendants do not add cascaded or
    // false cycle diagnostics.
    if (states[name] == FormatState::failed) {
      return nullptr;
    }

    // Revisiting an active node is the only condition that constitutes a cycle.
    if (states[name] == FormatState::visiting) {
      diagnostics.report(DiagnosticCode::format_inheritance_cycle,
                         source_by_name.at(name)->source, name);
      states[name] = FormatState::failed;
      return nullptr;
    }

    states[name] = FormatState::visiting;
    const auto &format_ast = *source_by_name.at(name);
    const ResolvedLayout *base = nullptr;

    if (format_ast.parent) {
      const auto &parent_name = format_ast.parent->value;

      // Check the complete declaration map so forward references are valid.
      if (!source_by_name.contains(parent_name)) {
        diagnostics.report(DiagnosticCode::unknown_format_parent,
                           format_ast.parent->source, parent_name);
        states[name] = FormatState::failed;
        return nullptr;
      }

      // Compile the parent first. A failed parent stops this format without
      // duplicating the ancestor's diagnostic.
      base = resolve(parent_name);
      if (!base) {
        states[name] = FormatState::failed;
        return nullptr;
      }
    }

    // Compile the local layout over the resolved parent, with an empty layout
    // as the default for root formats.
    auto layout = compile_layout(
        DeclarationOwner{"format", format_ast.name, format_ast.source}, base,
        format_ast.layout, enums, diagnostics);
    const auto [compiled, inserted] = result.emplace(name, std::move(layout));
    (void)inserted;

    // Publish only fully resolved graph nodes for instructions and descendants.
    states[name] = FormatState::succeeded;
    return &compiled->second;
  };

  // Trigger resolution in declaration order; recursive calls handle forwards.
  for (const auto &name : declaration_order) {
    resolve(name);
  }
  return result;
}

} // namespace sisl::detail
