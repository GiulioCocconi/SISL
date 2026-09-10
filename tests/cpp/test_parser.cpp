#include "test_helpers.hpp"

#include <sisl/sisl.hpp>

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

using sisl::DiagnosticCode;
using sisl::test::diagnostic_for;
using sisl::test::diagnostics_for;
using sisl::test::indexed_parameter_name;

TEST(Parser, PreservesDiagnosticsAndSourceLocations) {
  const auto diagnostics = diagnostics_for("arch Broken = {");
  ASSERT_FALSE(diagnostics.empty());
  EXPECT_TRUE(diagnostics.front().source.has_value());

  const auto utf8 = diagnostics_for("arch T = {\n// café\n@\n};");
  ASSERT_FALSE(utf8.empty());
  ASSERT_TRUE(utf8.front().source);
  EXPECT_EQ(utf8.front().source->source, "isa-string");
  EXPECT_EQ(utf8.front().source->line, 3);
  EXPECT_EQ(utf8.front().source->column, 0);
  EXPECT_EQ(utf8.front().source->span, 1);
}

TEST(Parser, AcceptsCommentsEscapesAndUppercaseBases) {
  const auto commented = sisl::Isa::load_string(R"(
    arch Comments = {
      // a line comment
      /* and a block comment */
      instr escaped = { width = 1; [0] = bit;
        assembly = "escaped\t{bit}"; };
    };
  )");
  EXPECT_EQ(commented.assemble("escaped\t1"), 1);

  const auto bases = sisl::Isa::load_string(R"(
    arch Bases = {
      instr bin = { width = 8; [7:0] = value;
        encoding = { value = 0B1; }; assembly = "bin"; };
      instr oct = { width = 8; [7:0] = value;
        encoding = { value = 0O2; }; assembly = "oct"; };
      instr hex = { width = 8; [7:0] = value;
        encoding = { value = 0XA; }; assembly = "hex"; };
    };
  )");
  EXPECT_EQ(bases.assemble("bin"), 1);
  EXPECT_EQ(bases.assemble("oct"), 2);
  EXPECT_EQ(bases.assemble("hex"), 10);
}

class KeywordTest : public testing::TestWithParam<std::string_view> {};

TEST_P(KeywordTest, ReservesOnlyTheExactKeyword) {
  const auto keyword = GetParam();
  EXPECT_TRUE(diagnostic_for("arch " + std::string(keyword) + " = { };",
                             DiagnosticCode::invalid_identifier));
  EXPECT_NO_THROW((void)sisl::Isa::load_string("arch " + std::string(keyword) +
                                               "-extension = { };"));
}

INSTANTIATE_TEST_SUITE_P(ParserKeywords, KeywordTest,
                         testing::Values<std::string_view>(
                             "arch", "endianness", "enum", "instr-format",
                             "instr", "alias", "encoding", "assembly", "width",
                             "bits", "uint", "sint"));

class InvalidUnsignedValueTest
    : public testing::TestWithParam<std::string_view> {};

TEST_P(InvalidUnsignedValueTest, RejectsNegativeValue) {
  EXPECT_TRUE(diagnostic_for(GetParam(), DiagnosticCode::expected_token));
}

INSTANTIATE_TEST_SUITE_P(ParserNegativeValues, InvalidUnsignedValueTest,
                         testing::Values<std::string_view>(
                             "arch T = { instr x = { width = -32; }; };",
                             "arch T = { instr x = { value : sint<-12>; }; };",
                             "arch T = { instr x = { [-1:0] = value; }; };"),
                         indexed_parameter_name<std::string_view>);

class InvalidIntegerTest : public testing::TestWithParam<std::string_view> {};

TEST_P(InvalidIntegerTest, RejectsMalformedInteger) {
  const auto source =
      "arch Bad = { enum E : bits<8> = { X = " + std::string(GetParam()) +
      "; }; };";
  EXPECT_TRUE(diagnostic_for(source, DiagnosticCode::invalid_integer_literal));
}

INSTANTIATE_TEST_SUITE_P(ParserIntegerLiterals, InvalidIntegerTest,
                         testing::Values<std::string_view>("0b102", "0o8", "0x",
                                                           "12foo"));

struct ParserDiagnosticCase {
  std::string_view source;
  DiagnosticCode code;
};

class ParserDiagnosticTest
    : public testing::TestWithParam<ParserDiagnosticCase> {};

TEST_P(ParserDiagnosticTest, ReportsCategory) {
  const auto &test = GetParam();
  const auto diagnostics = diagnostics_for(test.source);
  ASSERT_EQ(diagnostics.size(), 1);
  EXPECT_EQ(diagnostics.front().code, test.code);
}

INSTANTIATE_TEST_SUITE_P(
    ParserDiagnostics, ParserDiagnosticTest,
    testing::Values(
        ParserDiagnosticCase{"arch = { };", DiagnosticCode::expected_token},
        ParserDiagnosticCase{"architecture T = { };",
                             DiagnosticCode::expected_token},
        ParserDiagnosticCase{"arch T = { instr x = { width = ; }; };",
                             DiagnosticCode::expected_token},
        ParserDiagnosticCase{"arch T = { instr x = { width = 1 } };",
                             DiagnosticCode::expected_token},
        ParserDiagnosticCase{"arch T = { instr-format F : = { }; };",
                             DiagnosticCode::expected_token},
        ParserDiagnosticCase{
            "arch T = { instr x = { width = 8; [7:0] = x; encoding = { x = "
            "0x; }; assembly = \"x\"; }; };",
            DiagnosticCode::invalid_integer_literal},
        ParserDiagnosticCase{"arch T = { instr arch = { }; };",
                             DiagnosticCode::invalid_identifier},
        ParserDiagnosticCase{
            "arch T = { instr x = { width = 1; assembly = \"unterminated; }; "
            "};",
            DiagnosticCode::invalid_string_literal},
        ParserDiagnosticCase{
            "arch T = { instr x = { width = 1; [0] = x; assembly = \"bad\\q\"; "
            "}; };",
            DiagnosticCode::invalid_string_literal},
        ParserDiagnosticCase{"arch T = { @ };",
                             DiagnosticCode::unexpected_token},
        ParserDiagnosticCase{"arch T = {",
                             DiagnosticCode::unexpected_end_of_file},
        ParserDiagnosticCase{"arch T = { /* unterminated",
                             DiagnosticCode::unexpected_end_of_file},
        ParserDiagnosticCase{"arch T = { }; trailing",
                             DiagnosticCode::trailing_input}),
    indexed_parameter_name<ParserDiagnosticCase>);

} // namespace
