#include "test_helpers.hpp"

#include <sisl/sisl.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using sisl::DiagnosticCode;
using sisl::Integer;
using sisl::Operands;

class RiscvTest : public testing::Test {
protected:
  static const sisl::Isa &isa() {
    static const auto value = sisl::Isa::load_file(SISL_RISCV_PATH);
    return value;
  }

  static const Operands &add_operands() {
    static const Operands value{{"rd", std::string("x1")},
                                {"rs1", std::string("x2")},
                                {"rs2", std::string("x3")}};
    return value;
  }
};

TEST_F(RiscvTest, EncodesDecodesAndAssemblesInstructions) {
  const auto add = isa().encode("add", add_operands());
  EXPECT_EQ(add, Integer(0x003100b3));

  const auto decoded = isa().decode(add);
  EXPECT_EQ(decoded.name, "add");
  EXPECT_EQ(std::get<std::string>(decoded.operands.at("rd")), "x1");
  EXPECT_EQ(isa().disassemble(add), "add x1, x2, x3");

  const auto addi = isa().assemble("  addi   x5,x6, -4 ");
  EXPECT_EQ(addi, Integer(0xffc30293));
  EXPECT_EQ(isa().disassemble(addi), "addi x5, x6, -4");
  EXPECT_EQ(isa().disassemble(isa().assemble("nop")), "nop");
  EXPECT_EQ(isa().disassemble(isa().assemble("beq x1, x2, -4096")),
            "beq x1, x2, -4096");
}

TEST_F(RiscvTest, CoversEveryInstructionInFixture) {
  struct InstructionCase {
    std::string_view name;
    std::string_view assembly;
    Operands operands;
    Integer encoding;
  };

  const std::array cases{
      InstructionCase{"add",
                      "add x1, x2, x3",
                      {{"rd", std::string("x1")},
                       {"rs1", std::string("x2")},
                       {"rs2", std::string("x3")}},
                      0x003100b3},
      InstructionCase{"sub",
                      "sub x4, x5, x6",
                      {{"rd", std::string("x4")},
                       {"rs1", std::string("x5")},
                       {"rs2", std::string("x6")}},
                      0x40628233},
      InstructionCase{"and",
                      "and x7, x8, x9",
                      {{"rd", std::string("x7")},
                       {"rs1", std::string("x8")},
                       {"rs2", std::string("x9")}},
                      0x009473b3},
      InstructionCase{"addi",
                      "addi x5, x6, -4",
                      {{"rd", std::string("x5")},
                       {"rs1", std::string("x6")},
                       {"imm", Integer(-4)}},
                      0xffc30293},
      InstructionCase{"slli",
                      "slli x10, x11, 31",
                      {{"rd", std::string("x10")},
                       {"rs1", std::string("x11")},
                       {"shamt", Integer(31)}},
                      0x01f59513},
      InstructionCase{"beq",
                      "beq x1, x2, -4096",
                      {{"rs1", std::string("x1")},
                       {"rs2", std::string("x2")},
                       {"imm", Integer(-4096)}},
                      0x80208063},
      InstructionCase{"bne",
                      "bne x3, x4, 4094",
                      {{"rs1", std::string("x3")},
                       {"rs2", std::string("x4")},
                       {"imm", Integer(4094)}},
                      0x7e419fe3},
  };

  for (const auto &test : cases) {
    SCOPED_TRACE(test.name);
    EXPECT_EQ(isa().encode(test.name, test.operands), test.encoding);
    EXPECT_EQ(isa().assemble(test.assembly), test.encoding);
    EXPECT_EQ(isa().decode(test.encoding).name, test.name);
    EXPECT_EQ(isa().disassemble(test.encoding), test.assembly);
  }
}

TEST_F(RiscvTest, CoversFixtureAliasAndOperandBoundaries) {
  EXPECT_EQ(isa().assemble("nop"), 0x13);
  EXPECT_EQ(isa().disassemble(0x13), "nop");

  const auto expect_encode_error = [&](std::string_view instruction,
                                       const Operands &operands,
                                       DiagnosticCode code) {
    try {
      (void)isa().encode(instruction, operands);
      FAIL() << "expected encode to reject invalid operands";
    } catch (const sisl::Error &error) {
      ASSERT_FALSE(error.diagnostics().empty());
      EXPECT_EQ(error.diagnostics().front().code, code);
    }
  };

  expect_encode_error("slli",
                      {{"rd", std::string("x1")},
                       {"rs1", std::string("x2")},
                       {"shamt", Integer(32)}},
                      DiagnosticCode::operand_out_of_range);
  expect_encode_error("beq",
                      {{"rs1", std::string("x1")},
                       {"rs2", std::string("x2")},
                       {"imm", Integer(3)}},
                      DiagnosticCode::unrepresentable_operand);
}

TEST_F(RiscvTest, ConvertsLittleEndianBytes) {
  const auto bytes = isa().assemble_bytes("addi x5, x6, -4");
  const std::vector<std::byte> expected{std::byte{0x93}, std::byte{0x02},
                                        std::byte{0xc3}, std::byte{0xff}};
  EXPECT_EQ(bytes, expected);
  EXPECT_EQ(isa().disassemble_bytes(bytes), "addi x5, x6, -4");
}

TEST(Runtime, ConvertsBigEndianBytes) {
  const auto isa = sisl::Isa::load_string(R"(
    arch Test = {
      endianness = Big;
      instr word = { width = 16; [15:0] = value : uint<16>;
                     assembly = "word {value}"; };
    };
  )");
  const std::vector<std::byte> expected{std::byte{0x12}, std::byte{0x34}};
  EXPECT_EQ(isa.assemble_bytes("word 0x1234"), expected);
}

TEST(Runtime, SupportsArbitraryWidthInstructions) {
  const auto isa = sisl::Isa::load_string(R"(
    arch Wide = {
      instr raw = { width = 80; [79:0] = value : bits<80>;
                    assembly = "raw {value}"; };
    };
  )");
  const Integer huge = (Integer(1) << 79) - 1;
  EXPECT_EQ(isa.encode("raw", Operands{{"value", huge}}), huge);
  EXPECT_EQ(std::get<Integer>(isa.decode(huge).operands.at("value")), huge);
}

TEST_F(RiscvTest, CopiesMovesAndSupportsConcurrentReads) {
  auto copied = isa();
  const auto add = copied.encode("add", add_operands());
  std::atomic<bool> concurrent_ok = true;
  std::vector<std::thread> callers;
  for (int thread = 0; thread < 8; ++thread) {
    callers.emplace_back([&] {
      try {
        for (int iteration = 0; iteration < 25; ++iteration) {
          if (copied.encode("add", add_operands()) != add ||
              copied.disassemble(add) != "add x1, x2, x3") {
            concurrent_ok = false;
          }
        }
      } catch (...) {
        concurrent_ok = false;
      }
    });
  }
  for (auto &caller : callers) {
    caller.join();
  }
  EXPECT_TRUE(concurrent_ok);

  auto moved = std::move(copied);
  try {
    (void)copied.decode(add);
    FAIL() << "a moved-from ISA should reject operations";
  } catch (const sisl::Error &error) {
    ASSERT_FALSE(error.diagnostics().empty());
    EXPECT_EQ(error.diagnostics().front().code,
              DiagnosticCode::invalid_isa_handle);
  }
  EXPECT_EQ(moved.decode(add).name, "add");
}

} // namespace
