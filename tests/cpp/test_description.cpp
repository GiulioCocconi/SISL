#include <sisl/sisl.hpp>

#include <algorithm>
#include <stdexcept>
#include <string_view>

#include <gtest/gtest.h>

namespace {

template <typename T>
const T &named(const std::vector<T> &items, std::string_view name) {
  const auto it = std::find_if(items.begin(), items.end(), [&](const T &item) {
    return item.name == name;
  });
  if (it == items.end()) throw std::runtime_error("missing description entry");
  return *it;
}

TEST(Description, ResolvedRiscvFormatsAndFixedValues) {
  const auto description = sisl::Isa::load_file(SISL_RISCV_PATH).describe();
  EXPECT_EQ(description.name, "RV32I");
  ASSERT_EQ(description.formats.size(), 5);
  EXPECT_EQ(description.formats.front().name, "default");

  const auto &branch = named(description.formats, "B");
  ASSERT_EQ(branch.width, 32);
  ASSERT_EQ(branch.parent, "default");
  const auto &immediate = named(branch.fields, "imm");
  ASSERT_EQ(immediate.mappings.size(), 4);
  EXPECT_EQ(immediate.mappings[0].instruction_bits.msb, 31);
  EXPECT_EQ(immediate.mappings[0].field_bits.msb, 12);
  EXPECT_EQ(immediate.mappings[3].instruction_bits.lsb, 7);
  EXPECT_EQ(immediate.mappings[3].field_bits.lsb, 11);
  EXPECT_EQ(named(branch.fields, "opcode").mappings[0].instruction_bits.lsb, 0);

  const auto &gpr = named(description.enums, "GPR");
  ASSERT_EQ(gpr.members.size(), 32);
  EXPECT_EQ(gpr.members.front().first, "x0");
  EXPECT_EQ(gpr.members.back().second, 31);

  const auto &addi = named(description.instructions, "addi");
  ASSERT_EQ(addi.format, "I");
  EXPECT_EQ(addi.assembly, "addi {rd}, {rs1}, {imm}");
  EXPECT_EQ(named(addi.fixed_fields, "opcode").value, 0b0010011);
  EXPECT_EQ(named(addi.fixed_fields, "funct3").value, 0);

  const auto &nop = named(description.aliases, "nop");
  EXPECT_EQ(nop.target, "addi");
  EXPECT_EQ(nop.assembly, "nop");
  EXPECT_EQ(named(nop.additional_fixed_fields, "rd").enum_member, "x0");
  EXPECT_EQ(named(nop.additional_fixed_fields, "imm").value, 0);
}

} // namespace
