#pragma once

#include "common/primitive_type.hpp"

#include <sisl/sisl.hpp>

#include <cstddef>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sisl::detail {

struct BitRange {
  std::size_t msb = 0;
  std::size_t lsb = 0;
};

struct CompiledEnum;

struct FieldType {
  PrimitiveType kind = PrimitiveType::bits;
  std::size_t width = 1;

  // If kind != enumeration, then it is nullptr.
  std::shared_ptr<const CompiledEnum> enumeration;
};

struct Mapping {
  BitRange instruction_range;
  BitRange field_range;
};

struct Field {
  std::string name;
  FieldType type;
  std::vector<Mapping> mappings;
  Integer mapped_mask = 0;
};

struct CompiledEnum {
  std::size_t width = 1;
  std::vector<std::pair<std::string, Integer>> members;
};

struct AssemblyPart {
  enum class Kind { literal, operand } kind = Kind::literal;
  std::string text;
};

struct AssemblyTemplate {
  std::vector<AssemblyPart> parts;
  std::regex matcher;
};

struct Instruction {
  std::string name;
  std::size_t width = 1;
  Integer mask = 0;
  Integer match = 0;
  std::vector<Field> fields;
  std::vector<std::string> variable_fields;
  AssemblyTemplate assembly;
};

struct Alias {
  std::string name;
  std::shared_ptr<const Instruction> target;
  std::unordered_map<std::string, Integer> bindings;
  Integer mask = 0;
  Integer match = 0;
  std::vector<std::string> variable_fields;
  AssemblyTemplate assembly;
};

struct Model {
  bool big_endian = false;
  std::vector<std::shared_ptr<const Instruction>> instructions;
  std::vector<std::shared_ptr<const Alias>> aliases;
  std::unordered_map<std::string, std::shared_ptr<const Instruction>>
      instruction_index;
  std::unordered_map<std::string, std::shared_ptr<const Alias>> alias_index;
};

[[nodiscard]] Integer low_mask(std::size_t width);
[[nodiscard]] Integer range_mask(BitRange range);
[[nodiscard]] Integer gather(const Field &field, const Integer &instruction);
[[nodiscard]] Integer scatter(const Field &field, const Integer &logical,
                              Integer instruction = 0);
[[nodiscard]] const Field *find_field(const std::vector<Field> &fields,
                                      std::string_view name);

} // namespace sisl::detail
