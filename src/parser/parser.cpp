#include "common/diagnostics.hpp"
#include "parser/ast.hpp"
#include "parser/diagnostics.hpp"
#include "parser/support.hpp"

#include <lexy/action/parse.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace lexy {
// Teach lexy how to build the arbitrary-width integer used by the public API.
// Parsing is unbounded, so range checks remain a semantic-compiler concern.
template <> struct integer_traits<sisl::Integer> {
  using type = sisl::Integer;
  static constexpr auto is_bounded = false;

  template <int Radix>
  static void add_digit_unchecked(type &result, unsigned digit) {
    result *= Radix;
    result += digit;
  }
};
} // namespace lexy

namespace sisl::detail {
namespace {
namespace grammar {

// A production's rule describes syntax and its value callback builds the AST.
// Shared source, callback, and diagnostic plumbing lives in the two private
// parser headers so the declarations below remain focused on the SISL DSL.

// identifier := [A-Za-z_][A-Za-z0-9_-]*
// Keywords use the same token boundary, so `instr-extra` remains one identifier
// rather than the keyword `instr` followed by a suffix.
// Examples: `add`, `R_type`, `compressed-instr`.
constexpr auto identifier_syntax =
    dsl::identifier(dsl::ascii::alpha_underscore,
                    dsl::ascii::alpha_digit_underscore / dsl::hyphen);

constexpr auto kw_arch = LEXY_KEYWORD("arch", identifier_syntax);
constexpr auto kw_endianness = LEXY_KEYWORD("endianness", identifier_syntax);
constexpr auto kw_enum = LEXY_KEYWORD("enum", identifier_syntax);
constexpr auto kw_instr_format =
    LEXY_KEYWORD("instr-format", identifier_syntax);
constexpr auto kw_instr = LEXY_KEYWORD("instr", identifier_syntax);
constexpr auto kw_alias = LEXY_KEYWORD("alias", identifier_syntax);
constexpr auto kw_encoding = LEXY_KEYWORD("encoding", identifier_syntax);
constexpr auto kw_assembly = LEXY_KEYWORD("assembly", identifier_syntax);
constexpr auto kw_width = LEXY_KEYWORD("width", identifier_syntax);
constexpr auto kw_bits = LEXY_KEYWORD("bits", identifier_syntax);
constexpr auto kw_uint = LEXY_KEYWORD("uint", identifier_syntax);
constexpr auto kw_sint = LEXY_KEYWORD("sint", identifier_syntax);

constexpr auto keywords = dsl::literal_set(
    kw_arch, kw_endianness, kw_enum, kw_instr_format, kw_instr, kw_alias,
    kw_encoding, kw_assembly, kw_width, kw_bits, kw_uint, kw_sint);
constexpr auto identifier = identifier_syntax.reserve(keywords);

constexpr auto source_string =
    state_callback<SourceString>([](const ParseState &state, auto lexeme) {
      return SourceString{lexy::as_string<std::string, Encoding>(lexeme),
                          state.span(lexeme.begin(), lexeme.end())};
    });

struct Identifier {
  static constexpr auto rule = identifier;
  static constexpr auto value = source_string;
};

constexpr auto declaration_end = dsl::capture(dsl::semicolon);
constexpr auto optional_format_reference = dsl::opt(
    dsl::colon >>
    dsl::must(dsl::p<Identifier>).error<ExpectedInstructionFormatName>);

// Used where every identifier-shaped value is syntactically valid and semantic
// validation owns the accepted vocabulary, such as an endianness value.
struct Word {
  static constexpr auto rule = identifier_syntax;
  static constexpr auto value = source_string;
};

// integer := decimal | 0[bB]binary | 0[oO]octal | 0[xX]hexadecimal
// The trailing negative lookahead rejects a valid numeric prefix followed by
// identifier characters, making inputs such as `12foo` one malformed token.
// Examples: `42`, `0b1010`, `0o52`, `0x2A`.
struct IntegerLiteral : lexy::token_production {
  static constexpr auto digits = [] {
    auto prefixed = [](auto prefix, auto value) {
      return dsl::ascii::case_folding(prefix) >> value;
    };
    return prefixed(LEXY_LIT("0b"),
                    dsl::integer<Integer>(dsl::digits<dsl::binary>)) |
           prefixed(LEXY_LIT("0o"),
                    dsl::integer<Integer>(dsl::digits<dsl::octal>)) |
           prefixed(LEXY_LIT("0x"),
                    dsl::integer<Integer>(dsl::digits<dsl::hex>)) |
           dsl::integer<Integer>(dsl::digits<>);
  }();
  static constexpr auto identifier_character =
      dsl::ascii::alpha_digit_underscore / dsl::hyphen;
  static constexpr auto
      rule = dsl::peek(dsl::ascii::digit) >>
             dsl::position + digits +
                 dsl::peek_not(identifier_character) + dsl::position;
  static constexpr auto value = state_callback<SourceInteger>(
      [](const ParseState &state, Iterator begin, Integer value, Iterator end) {
        return SourceInteger{std::move(value), state.span(begin, end)};
      });
};

constexpr auto integer_literal =
    // Once a digit starts an integer, failure must be an integer diagnostic;
    // it must not backtrack and reinterpret the token as another production.
    dsl::must(dsl::p<IntegerLiteral>).error<ExpectedIntegerLiteral>;

// string := '"' (code-point | supported-escape)* '"'
// A physical newline ends the token, allowing the error sink to distinguish an
// unterminated literal from other syntax errors.
// Example: `"add {rd}, {rs1}, {rs2}"`.
struct StringLiteral : lexy::token_production {
  static constexpr auto escapes = lexy::symbol_table<char>
                                      .map<'"'>('"')
                                      .map<'\\'>('\\')
                                      .map<'n'>('\n')
                                      .map<'r'>('\r')
                                      .map<'t'>('\t');
  static constexpr auto rule =
      dsl::position +
      dsl::quoted.limit(dsl::ascii::newline)(
          dsl::code_point, dsl::backslash_escape.symbol<escapes>()) +
      dsl::position;
  static constexpr auto value =
      lexy::as_string<std::string, Encoding> >>
      state_callback<SourceString>([](const ParseState &state, Iterator begin,
                                      std::string value, Iterator end) {
        return SourceString{std::move(value), state.span(begin, end)};
      });
};

// type := (bits | uint | sint) '<' integer '>' | identifier
// Named types are resolved against enum declarations by the semantic compiler.
// Examples: `bits<7>`, `uint<12>`, `sint<12>`, `Register`.
struct PrimitiveTypeName {
  static constexpr auto symbols =
      lexy::symbol_table<PrimitiveType>
          .map<LEXY_SYMBOL("bits")>(PrimitiveType::bits)
          .map<LEXY_SYMBOL("uint")>(PrimitiveType::unsigned_integer)
          .map<LEXY_SYMBOL("sint")>(PrimitiveType::signed_integer);
  static constexpr auto rule = dsl::symbol<symbols>(identifier_syntax);
  static constexpr auto value = lexy::forward<PrimitiveType>;
};

struct PrimitiveTypeAst {
  static constexpr auto rule = dsl::position(dsl::p<PrimitiveTypeName>) >>
                               dsl::angle_bracketed.open() + integer_literal
                                   + dsl::capture(dsl::angle_bracketed.close());
  static constexpr auto value = state_callback<TypeAst>(
      [](const ParseState &state, Iterator begin, PrimitiveType kind,
         SourceInteger width, Lexeme close) {
        return TypeAst{.kind = kind,
                       .width = std::move(width.value),
                       .source = state.span(begin, close.end())};
      });
};

struct NamedTypeAst {
  static constexpr auto rule = dsl::p<Identifier>;
  static constexpr auto value = lexy::callback<TypeAst>([](SourceString name) {
    return TypeAst{.kind = PrimitiveType::enumeration,
                   .name = std::move(name.value),
                   .source = name.source};
  });
};

struct Type : lexy::transparent_production {
  static constexpr auto rule = dsl::p<PrimitiveTypeAst> | dsl::p<NamedTypeAst>;
  static constexpr auto value = lexy::forward<TypeAst>;
};

// range := '[' msb (':' lsb)? ']'; `[n]` is normalized to `[n:n]` in the AST.
// Examples: `[31:20]`, `[7:0]`, `[3]`.
struct Range {
  static constexpr auto rule = dsl::position(dsl::square_bracketed.open()) >>
                               integer_literal +
                                   dsl::opt(dsl::colon >> integer_literal) +
                                   dsl::capture(dsl::square_bracketed.close());
  static constexpr auto value = state_callback<RangeAst>(
      [](const ParseState &state, Iterator begin, SourceInteger msb,
         lexy::nullopt, Lexeme close) {
        auto lsb = msb.value;
        return RangeAst{std::move(msb.value), std::move(lsb),
                        state.span(begin, close.end())};
      },
      [](const ParseState &state, Iterator begin, SourceInteger msb,
         SourceInteger lsb, Lexeme close) {
        return RangeAst{std::move(msb.value), std::move(lsb.value),
                        state.span(begin, close.end())};
      });
};

// value := '-'? integer | identifier
// The leading lookahead chooses the numeric branch without consuming input;
// after that choice, malformed numbers receive numeric diagnostics.
// Examples: `-4`, `0x13`, `x0`.
struct NumericValue {
  static constexpr auto rule =
      dsl::position + dsl::minus_sign + integer_literal;
  static constexpr auto value = state_callback<ValueAst>(
      [](const ParseState &state, Iterator begin, SourceInteger number) {
        return ValueAst{std::move(number.value),
                        state.span(begin, state.begin + number.source.end)};
      },
      [](const ParseState &state, Iterator begin, lexy::minus_sign,
         SourceInteger number) {
        return ValueAst{-std::move(number.value),
                        state.span(begin, state.begin + number.source.end)};
      });
};

struct SymbolicValue {
  static constexpr auto rule = dsl::p<Identifier>;
  static constexpr auto value = lexy::callback<ValueAst>([](SourceString name) {
    return ValueAst{std::move(name.value), name.source};
  });
};

struct Value : lexy::transparent_production {
  static constexpr auto rule =
      dsl::peek(dsl::hyphen / dsl::ascii::digit) >> dsl::p<NumericValue> |
      dsl::else_ >> dsl::p<SymbolicValue>;
  static constexpr auto value = lexy::forward<ValueAst>;
};

using LayoutEntry = std::variant<WidthDeclarationAst, FieldAst, MappingAst>;

// layout-entry := width '=' integer ';'
//               | identifier ':' type ';'
//               | range '=' identifier (range | ':' type)? ';'
// A mapping can declare an inferred field, slice a declared field, or annotate
// the inferred field's type.
// Examples: `width = 32;`, `imm : sint<12>;`, `[31:20] = imm[11:0];`,
// `[6:0] = opcode : bits<7>;`.

void append(LayoutAst &layout, WidthDeclarationAst width) {
  layout.width.add(std::move(width));
}

void append(LayoutAst &layout, FieldAst field) {
  layout.fields.push_back(std::move(field));
}

void append(LayoutAst &layout, MappingAst mapping) {
  layout.mappings.push_back(std::move(mapping));
}

void append(LayoutAst &layout, LayoutEntry entry) {
  std::visit([&](auto value) { append(layout, std::move(value)); },
             std::move(entry));
}

constexpr auto layout_sink = entry_sink<LayoutAst, LayoutEntry>(
    [](LayoutAst &layout, auto value) { append(layout, std::move(value)); });

struct Width {
  static constexpr auto rule = dsl::position(kw_width) >> dsl::equal_sign +
                                                              integer_literal +
                                                              declaration_end;
  static constexpr auto value =
      spanned_declaration<LayoutEntry, WidthDeclarationAst>();
};

struct Field {
  static constexpr auto rule =
      dsl::p<Identifier> >> dsl::colon + dsl::p<Type> + declaration_end;
  static constexpr auto value =
      state_callback<LayoutEntry>([](const ParseState &state, SourceString name,
                                     TypeAst type, Lexeme semicolon) {
        return FieldAst{std::move(name.value), std::move(type),
                        state.through(name.source, semicolon.end())};
      });
};

struct Mapping {
  static constexpr auto rule = dsl::p<Range> >>
                               dsl::equal_sign + dsl::p<Identifier> +
                                   dsl::opt(dsl::p<Range> |
                                            dsl::colon >> dsl::p<Type>) +
                                   declaration_end;
  static constexpr auto value = state_callback<LayoutEntry>(
      [](const ParseState &state, RangeAst instruction, SourceString name,
         lexy::nullopt, Lexeme semicolon) {
        const auto source = state.through(instruction.source, semicolon.end());
        return MappingAst{std::move(instruction), std::move(name.value),
                          std::nullopt, std::nullopt, source};
      },
      [](const ParseState &state, RangeAst instruction, SourceString name,
         RangeAst field, Lexeme semicolon) {
        const auto source = state.through(instruction.source, semicolon.end());
        return MappingAst{std::move(instruction), std::move(name.value),
                          std::move(field), std::nullopt, source};
      },
      [](const ParseState &state, RangeAst instruction, SourceString name,
         TypeAst type, Lexeme semicolon) {
        const auto source = state.through(instruction.source, semicolon.end());
        return MappingAst{std::move(instruction), std::move(name.value),
                          std::nullopt, std::move(type), source};
      });
};

struct LayoutEntryProduction : lexy::transparent_production {
  static constexpr auto rule = dsl::p<Width> | dsl::p<Mapping> | dsl::p<Field>;
  static constexpr auto value = lexy::forward<LayoutEntry>;
};

// enum := 'enum' identifier ':' 'bits' '<' integer '>'
//         '=' '{' (identifier '=' integer ';')* '}' ';'
// Example: `enum Register : bits<5> = { x0 = 0; x1 = 1; };`.
struct EnumMember {
  static constexpr auto rule =
      dsl::p<Identifier> >> dsl::equal_sign + integer_literal + declaration_end;
  static constexpr auto value =
      state_callback<BindingAst>([](const ParseState &state, SourceString name,
                                    SourceInteger value, Lexeme semicolon) {
        return BindingAst{std::move(name.value),
                          ValueAst{value.value, value.source},
                          state.through(name.source, semicolon.end())};
      });
};

struct Enum {
  static constexpr auto rule =
      dsl::position(kw_enum) >>
      dsl::p<Identifier> + dsl::colon + kw_bits +
          dsl::angle_bracketed(integer_literal) + dsl::equal_sign
          + dsl::curly_bracketed.opt_list(dsl::p<EnumMember>) + declaration_end;
  static constexpr auto value =
      lexy::as_list<std::vector<BindingAst>> >>
      state_callback<EnumAst>([](const ParseState &state, Iterator begin,
                                 SourceString name, SourceInteger width,
                                 auto members, Lexeme semicolon) {
        return EnumAst{std::move(name.value), std::move(width),
                       or_empty<std::vector<BindingAst>>(std::move(members)),
                       state.span(begin, semicolon.end())};
      });
};

// encoding := 'encoding' '=' '{' (identifier '=' value ';')* '}' ';'
// Whether a symbolic or numeric value is valid depends on the bound field and
// is therefore checked after parsing.
// Example: `encoding = { opcode = 0x13; funct3 = 0; };`.
struct EncodingBinding {
  static constexpr auto rule =
      dsl::p<Identifier> >> dsl::equal_sign + dsl::p<Value> + declaration_end;
  static constexpr auto value =
      state_callback<BindingAst>([](const ParseState &state, SourceString name,
                                    ValueAst value, Lexeme semicolon) {
        return BindingAst{std::move(name.value), std::move(value),
                          state.through(name.source, semicolon.end())};
      });
};

struct EncodingDeclaration {
  static constexpr auto
      rule = dsl::position(kw_encoding) >>
             dsl::equal_sign +
                 dsl::curly_bracketed.opt_list(dsl::p<EncodingBinding>) +
                 declaration_end;
  static constexpr auto value =
      lexy::as_list<std::vector<BindingAst>> >>
      state_callback<EncodingAst>([](const ParseState &state, Iterator begin,
                                     auto bindings, Lexeme semicolon) {
        return EncodingAst{
            .bindings = or_empty<std::vector<BindingAst>>(std::move(bindings)),
            .source = state.span(begin, semicolon.end())};
      });
};

// assembly := 'assembly' '=' string ';'
// Placeholders inside the string are interpreted by the semantic compiler.
// Example: `assembly = "addi {rd}, {rs1}, {imm}";`.
struct Assembly {
  static constexpr auto rule =
      dsl::position(kw_assembly) >>
      dsl::equal_sign + dsl::p<StringLiteral> + declaration_end;
  static constexpr auto value = spanned_declaration<AssemblyDeclarationAst>();
};

// format := 'instr-format' identifier (':' parent)?
//           '=' '{' layout-entry* '}' ';'
// `must` after ':' turns a missing parent into a targeted error instead of a
// later, less useful "expected '='" diagnostic.
// Examples: `instr-format Base = { width = 32; };` and
// `instr-format Immediate : Base = { imm : sint<12>; };`.
struct Format {
  static constexpr auto
      rule = dsl::position(kw_instr_format) >>
             dsl::p<Identifier> + optional_format_reference + dsl::equal_sign +
                 dsl::curly_bracketed.opt_list(dsl::p<LayoutEntryProduction>) +
                 declaration_end;
  static constexpr auto value =
      layout_sink >>
      state_callback<FormatAst>([](const ParseState &state, Iterator begin,
                                   SourceString name, auto parent, auto layout,
                                   Lexeme semicolon) {
        FormatAst result{.name = std::move(name.value),
                         .layout = or_empty<LayoutAst>(std::move(layout)),
                         .source = state.span(begin, semicolon.end())};
        if constexpr (!std::is_same_v<std::remove_cvref_t<decltype(parent)>,
                                      lexy::nullopt>) {
          result.parent = std::move(parent);
        }
        return result;
      });
};

using InstructionEntry =
    std::variant<LayoutEntry, EncodingAst, AssemblyDeclarationAst>;

struct InstructionEntryProduction : lexy::transparent_production {
  static constexpr auto rule = dsl::p<EncodingDeclaration> | dsl::p<Assembly> |
                               dsl::p<LayoutEntryProduction>;
  // Wrap the selected AST type in InstructionEntry; the surrounding sink then
  // visits it and appends it to the correct part of InstructionAst.
  static constexpr auto value = lexy::construct<InstructionEntry>;
};

void append(InstructionAst &instruction, LayoutEntry entry) {
  append(instruction.layout, std::move(entry));
}

void append(InstructionAst &instruction, EncodingAst encoding) {
  instruction.encoding.add(std::move(encoding));
}

void append(InstructionAst &instruction, AssemblyDeclarationAst assembly) {
  instruction.assembly.add(std::move(assembly));
}

constexpr auto instruction_sink = entry_sink<InstructionAst, InstructionEntry>(
    [](InstructionAst &instruction, auto value) {
      append(instruction, std::move(value));
    });

// instruction := 'instr' identifier (':' format)?
//                '=' '{' (layout-entry | encoding | assembly)* '}' ';'
// Example: `instr nop = { width = 32; assembly = "nop"; };`.
struct Instruction {
  static constexpr auto rule =
      dsl::position(kw_instr) >>
      dsl::p<Identifier> + optional_format_reference + dsl::equal_sign +
          dsl::curly_bracketed.opt_list(dsl::p<InstructionEntryProduction>) +
          declaration_end;
  static constexpr auto value =
      instruction_sink >>
      state_callback<InstructionAst>([](const ParseState &state, Iterator begin,
                                        SourceString name, auto format,
                                        auto body, Lexeme semicolon) {
        auto result = or_empty<InstructionAst>(std::move(body));
        result.name = std::move(name.value);
        if constexpr (!std::is_same_v<std::remove_cvref_t<decltype(format)>,
                                      lexy::nullopt>) {
          result.format = std::move(format);
        }
        result.source = state.span(begin, semicolon.end());
        return result;
      });
};

// alias-target := 'instr' '=' identifier ';'
// alias := 'alias' identifier
//          '=' '{' (alias-target | encoding | assembly)* '}' ';'
// Example: `alias nop = { instr = addi; assembly = "nop"; };`.
struct Target {
  static constexpr auto rule =
      dsl::position(kw_instr) >>
      dsl::equal_sign + dsl::p<Identifier> + declaration_end;
  static constexpr auto value = spanned_declaration<AliasTargetAst>();
};

using AliasEntry =
    std::variant<AliasTargetAst, EncodingAst, AssemblyDeclarationAst>;

struct AliasEntryProduction : lexy::transparent_production {
  static constexpr auto rule =
      dsl::p<Target> | dsl::p<EncodingDeclaration> | dsl::p<Assembly>;
  static constexpr auto value = lexy::construct<AliasEntry>;
};

void append(AliasAst &alias, AliasTargetAst target) {
  alias.target.add(std::move(target));
}

void append(AliasAst &alias, EncodingAst encoding) {
  alias.encoding.add(std::move(encoding));
}

void append(AliasAst &alias, AssemblyDeclarationAst assembly) {
  alias.assembly.add(std::move(assembly));
}

constexpr auto alias_sink = entry_sink<AliasAst, AliasEntry>(
    [](AliasAst &alias, auto value) { append(alias, std::move(value)); });

struct Alias {
  static constexpr auto
      rule = dsl::position(kw_alias) >>
             dsl::p<Identifier> + dsl::equal_sign +
                 dsl::curly_bracketed.opt_list(dsl::p<AliasEntryProduction>) +
                 declaration_end;
  static constexpr auto
      value = alias_sink >>
              state_callback<AliasAst>([](const ParseState &state,
                                          Iterator begin, SourceString name,
                                          auto body, Lexeme semicolon) {
                auto result = or_empty<AliasAst>(std::move(body));
                result.name = std::move(name.value);
                result.source = state.span(begin, semicolon.end());
                return result;
              });
};

// endianness := 'endianness' '=' word ';'
// `word` intentionally accepts keyword-shaped values so the compiler can issue
// the clearer `invalid_endianness` diagnostic for anything except Big/Little.
// Examples: `endianness = Little;`, `endianness = Big;`.
struct Endianness {
  static constexpr auto rule = dsl::position(kw_endianness) >>
                               dsl::equal_sign + dsl::p<Word> + declaration_end;
  static constexpr auto value = spanned_declaration<EndiannessDeclarationAst>();
};

using ArchitectureEntry = std::variant<EndiannessDeclarationAst, EnumAst,
                                       FormatAst, InstructionAst, AliasAst>;

struct ArchitectureEntryProduction : lexy::transparent_production {
  static constexpr auto rule = dsl::p<Endianness> | dsl::p<Enum> |
                               dsl::p<Format> | dsl::p<Instruction> |
                               dsl::p<Alias>;
  static constexpr auto value = lexy::construct<ArchitectureEntry>;
};

void append(ArchitectureAst &architecture,
            EndiannessDeclarationAst endianness) {
  architecture.endianness.add(std::move(endianness));
}

void append(ArchitectureAst &architecture, EnumAst enumeration) {
  architecture.enums.push_back(std::move(enumeration));
}

void append(ArchitectureAst &architecture, FormatAst format) {
  architecture.formats.push_back(std::move(format));
}

void append(ArchitectureAst &architecture, InstructionAst instruction) {
  architecture.instructions.push_back(std::move(instruction));
}

void append(ArchitectureAst &architecture, AliasAst alias) {
  architecture.aliases.push_back(std::move(alias));
}

constexpr auto architecture_sink =
    entry_sink<ArchitectureAst, ArchitectureEntry>(
        [](ArchitectureAst &architecture, auto value) {
          append(architecture, std::move(value));
        });

// architecture := 'arch' identifier
//                 '=' '{' architecture-entry* '}' ';' EOF
// Whitespace is defined on the root production and is automatically skipped by
// all child productions, including line and non-nesting block comments.
// Example: `arch Minimal = { endianness = Little; };`.
struct Architecture {
  static constexpr auto line_comment = LEXY_LIT("//") >>
                                       dsl::until(dsl::ascii::newline).or_eof();
  static constexpr auto block_comment = LEXY_LIT("/*") >>
                                        dsl::until(LEXY_LIT("*/"));
  static constexpr auto whitespace =
      dsl::ascii::space | line_comment | block_comment;
  static constexpr auto rule =
      dsl::position(kw_arch) >>
      dsl::p<Identifier> + dsl::equal_sign +
          dsl::curly_bracketed.opt_list(dsl::p<ArchitectureEntryProduction>) +
          declaration_end + dsl::eof;
  static constexpr auto value =
      architecture_sink >>
      state_callback<ArchitectureAst>([](const ParseState &state,
                                         Iterator begin, SourceString name,
                                         auto body, Lexeme semicolon) {
        auto result = or_empty<ArchitectureAst>(std::move(body));
        result.name = std::move(name.value);
        result.source = state.span(begin, semicolon.end());
        return result;
      });
};

} // namespace grammar
} // namespace

ArchitectureAst parse_source(std::string_view text,
                             const SourceMap &source_map) {
  DiagnosticSink diagnostics(source_map);
  grammar::ParseState state{.begin = text.data(),
                            .end = text.data() + text.size()};
  const auto input =
      lexy::string_input<grammar::Encoding>(text.data(), text.size());
  // Architecture is the root production; ParseError converts any failure into
  // sisl::Error, so a successful result always contains an ArchitectureAst.
  auto result = lexy::parse<grammar::Architecture>(
      input, state, grammar::ParseError{state, diagnostics});
  return std::move(result).value();
}

} // namespace sisl::detail
