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

#include "test_helpers.hpp"

#include <sisl/sisl.hpp>

#include <gtest/gtest.h>

#include <string_view>

namespace {

using sisl::DiagnosticCode;
using sisl::test::architecture;
using sisl::test::diagnostic_for;
using sisl::test::diagnostics_for;
using sisl::test::indexed_parameter_name;
using sisl::test::source_text;

struct DeclarationDiagnosticCase {
  std::string_view source;
  DiagnosticCode code;
  std::string_view highlighted;
};

class DeclarationDiagnosticTest
    : public testing::TestWithParam<DeclarationDiagnosticCase> {};

TEST_P(DeclarationDiagnosticTest, ReportsOwnerAndDeclaration) {
  const auto &test = GetParam();
  const auto diagnostic = diagnostic_for(test.source, test.code);
  ASSERT_TRUE(diagnostic);
  EXPECT_EQ(source_text(test.source, *diagnostic), test.highlighted);
}

INSTANTIATE_TEST_SUITE_P(
    SemanticDeclarations, DeclarationDiagnosticTest,
    testing::Values(
        DeclarationDiagnosticCase{
            "arch Span = { instr x = { width = 1; width = 2; [0] = value; "
            "assembly = \"x {value}\"; }; };",
            DiagnosticCode::duplicate_declaration, "width = 2;"},
        DeclarationDiagnosticCase{
            "arch Span = { instr x = { width = 1; [0] = value; assembly = "
            "\"x {value}\"; assembly = \"y {value}\"; }; };",
            DiagnosticCode::duplicate_declaration, "assembly = \"y {value}\";"},
        DeclarationDiagnosticCase{
            "arch Span = { instr base = { width = 1; [0] = value; assembly = "
            "\"base {value}\"; }; alias x = { instr = base; instr = base; "
            "assembly = \"x {value}\"; }; };",
            DiagnosticCode::duplicate_declaration, "instr = base;"},
        DeclarationDiagnosticCase{
            "arch Span = { endianness = Little; endianness = Big; };",
            DiagnosticCode::duplicate_declaration, "endianness = Big;"},
        DeclarationDiagnosticCase{
            "arch Span = { alias x = { assembly = \"x\"; }; };",
            DiagnosticCode::missing_declaration,
            "alias x = { assembly = \"x\"; };"},
        DeclarationDiagnosticCase{"arch Span = { instr x = { width = 1; }; };",
                                  DiagnosticCode::missing_declaration,
                                  "instr x = { width = 1; };"},
        DeclarationDiagnosticCase{
            "arch Span = { instr x = { assembly = \"x\"; }; };",
            DiagnosticCode::missing_declaration,
            "instr x = { assembly = \"x\"; };"}),
    indexed_parameter_name<DeclarationDiagnosticCase>);

TEST(SemanticCompiler, IgnoresDuplicateDeclarationContents) {
  const auto source = "arch Test = { instr x = { width = 1; [0] = bit; "
                      "encoding = { bit = 0; }; encoding = { missing = 1; }; "
                      "assembly = \"x\"; }; };";
  const auto diagnostics = diagnostics_for(source);
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, DiagnosticCode::duplicate_declaration);
  EXPECT_EQ(source_text(source, diagnostics.front()),
            "encoding = { missing = 1; };");
}

TEST(SemanticCompiler, ReportsOutOfBoundsMappingOnceAtMapping) {
  const auto source = architecture("instr bad = { width = 4; [4] = value; "
                                   "assembly = \"bad {value}\"; };");

  const auto diagnostics = diagnostics_for(source);
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, DiagnosticCode::mapping_out_of_bounds);
  EXPECT_EQ(source_text(source, diagnostics.front()), "[4] = value;");
}

TEST(SemanticCompiler, DefersWidthlessFormatBoundsToEachBoundedChild) {
  const auto source =
      architecture("instr-format Base = { [7:0] = value; }; "
                   "instr-format Narrow : Base = { width = 4; }; "
                   "instr-format Wide : Base = { width = 8; };");

  const auto diagnostics = diagnostics_for(source);
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, DiagnosticCode::mapping_out_of_bounds);
  EXPECT_EQ(source_text(source, diagnostics.front()), "[7:0] = value;");
}

TEST(SemanticCompiler, DoesNotMisreportFailedFormatAsCycle) {
  const auto diagnostics = diagnostics_for(
      architecture("instr-format Broken : Missing = { width = 8; }; "
                   "instr-format Child : Broken = { };"));

  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, DiagnosticCode::unknown_format_parent);
}

TEST(SemanticCompiler, ReportsFormatCycleOnce) {
  const auto diagnostics =
      diagnostics_for(architecture("instr-format A : B = { }; "
                                   "instr-format B : C = { }; "
                                   "instr-format C : A = { };"));

  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, DiagnosticCode::format_inheritance_cycle);
}

struct SemanticDiagnosticCase {
  std::string_view body;
  DiagnosticCode code;
};

class SemanticDiagnosticTest
    : public testing::TestWithParam<SemanticDiagnosticCase> {};

TEST_P(SemanticDiagnosticTest, ReportsExpectedCode) {
  const auto &test = GetParam();
  EXPECT_TRUE(diagnostic_for(architecture(test.body), test.code))
      << "expected diagnostic " << sisl::to_string(test.code);
}

INSTANTIATE_TEST_SUITE_P(
    SemanticErrors, SemanticDiagnosticTest,
    testing::Values(
        SemanticDiagnosticCase{
            "instr bad : Missing = { width = 8; assembly = \"bad\"; };",
            DiagnosticCode::unknown_instruction_format},
        SemanticDiagnosticCase{"instr-format A : B = { width = 8; }; "
                               "instr-format B : A = { };",
                               DiagnosticCode::format_inheritance_cycle},
        SemanticDiagnosticCase{
            "instr bad = { width = 5; [4:0] = rd : Missing; assembly = \"bad "
            "{rd}\"; };",
            DiagnosticCode::unknown_enum_type},
        SemanticDiagnosticCase{
            "instr bad = { width = 8; [0:1] = x; assembly = \"bad {x}\"; };",
            DiagnosticCode::invalid_bit_range},
        SemanticDiagnosticCase{
            "instr bad = { width = 8; [7:4] = x : uint<3>; assembly = \"bad "
            "{x}\"; };",
            DiagnosticCode::mapping_width_mismatch},
        SemanticDiagnosticCase{
            "instr bad = { width = 4; [4] = x; assembly = \"bad {x}\"; };",
            DiagnosticCode::mapping_out_of_bounds},
        SemanticDiagnosticCase{
            "instr bad = { width = 4; x : bits<4>; [3:0] = x[4:1]; assembly "
            "= \"bad {x}\"; };",
            DiagnosticCode::field_slice_outside_width},
        SemanticDiagnosticCase{
            "instr bad = { width = 8; [7:4] = a; [5:2] = b; assembly = \"bad "
            "{a}, {b}\"; };",
            DiagnosticCode::overlapping_instruction_mapping},
        SemanticDiagnosticCase{
            "instr bad = { width = 8; x : bits<4>; [7:6] = x[3:2]; [5:4] = "
            "x[3:2]; assembly = \"bad {x}\"; };",
            DiagnosticCode::overlapping_field_slice},
        SemanticDiagnosticCase{
            "instr bad = { width = 4; x : uint<4>; [3:0] = x : sint<4>; "
            "assembly = \"bad {x}\"; };",
            DiagnosticCode::inconsistent_field_type},
        SemanticDiagnosticCase{
            "instr bad = { width = 1; [0] = x : bits<0>; assembly = \"bad "
            "{x}\"; };",
            DiagnosticCode::invalid_primitive_width},
        SemanticDiagnosticCase{
            "instr bad = { width = 3; [2:0] = opcode; encoding = { opcode = "
            "8; }; assembly = \"bad\"; };",
            DiagnosticCode::fixed_value_out_of_range},
        SemanticDiagnosticCase{
            "instr bad = { width = 3; [2:0] = opcode; encoding = { missing = "
            "0; }; assembly = \"bad {opcode}\"; };",
            DiagnosticCode::unknown_encoding_field},
        SemanticDiagnosticCase{
            "instr bad = { width = 3; [2:0] = opcode; encoding = { opcode = "
            "symbolic; }; assembly = \"bad\"; };",
            DiagnosticCode::invalid_symbolic_value},
        SemanticDiagnosticCase{
            "instr bad = { width = 8; [7:4] = op; [3:0] = x; encoding = { op "
            "= 0; }; assembly = \"bad {missing}\"; };",
            DiagnosticCode::invalid_assembly_placeholder},
        SemanticDiagnosticCase{
            "instr bad = { width = 8; [7:4] = a; [3:0] = b; assembly = \"bad "
            "{a}\"; };",
            DiagnosticCode::missing_assembly_operand},
        SemanticDiagnosticCase{
            "instr one = { width = 4; [3:0] = x; encoding = { x = 1; }; "
            "assembly = \"one\"; }; instr two = { width = 4; [3:0] = x; "
            "encoding = { x = 1; }; assembly = \"two\"; };",
            DiagnosticCode::instruction_encoding_overlap},
        SemanticDiagnosticCase{
            "alias bad = { instr = missing; assembly = \"bad\"; };",
            DiagnosticCode::invalid_alias_target},
        SemanticDiagnosticCase{
            "instr base = { width = 4; [3:0] = x; assembly = \"base {x}\"; "
            "}; alias bad = { instr = base; encoding = { nope = 0; }; "
            "assembly = \"bad {x}\"; };",
            DiagnosticCode::invalid_alias_binding},
        SemanticDiagnosticCase{
            "enum E : bits<1> = { A = 0; }; enum E : bits<1> = { B = 1; };",
            DiagnosticCode::duplicate_enum},
        SemanticDiagnosticCase{"enum E : bits<1> = { };",
                               DiagnosticCode::empty_enum},
        SemanticDiagnosticCase{"enum E : bits<0> = { A = 0; };",
                               DiagnosticCode::invalid_enum_width},
        SemanticDiagnosticCase{"enum E : bits<1> = { A = 0; A = 1; };",
                               DiagnosticCode::duplicate_enum_member},
        SemanticDiagnosticCase{"enum E : bits<2> = { A = 1; B = 1; };",
                               DiagnosticCode::duplicate_enum_value},
        SemanticDiagnosticCase{"enum E : bits<2> = { A = 4; };",
                               DiagnosticCode::enum_value_out_of_range},
        SemanticDiagnosticCase{
            "enum E : bits<1> = { A = 0; }; instr bad = { width = 1; [0] = "
            "value : E; encoding = { value = 0; }; assembly = \"bad\"; };",
            DiagnosticCode::invalid_enum_value},
        SemanticDiagnosticCase{
            "instr-format F = { width = 1; [0] = x; }; instr-format F = { "
            "width = 1; [0] = y; };",
            DiagnosticCode::duplicate_format},
        SemanticDiagnosticCase{
            "instr x = { width = 1; [0] = b; assembly = \"x {b}\"; }; instr "
            "x = { width = 1; [0] = b; assembly = \"x {b}\"; };",
            DiagnosticCode::duplicate_instruction},
        SemanticDiagnosticCase{
            "instr x = { width = 1; [0] = b; assembly = \"x {b}\"; }; alias "
            "a = { instr = x; encoding = { b = 0; }; assembly = \"a\"; }; "
            "alias a = { instr = x; encoding = { b = 1; }; assembly = "
            "\"aa\"; };",
            DiagnosticCode::duplicate_alias}),
    indexed_parameter_name<SemanticDiagnosticCase>);

} // namespace
