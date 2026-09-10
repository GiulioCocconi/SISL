#pragma once

#include "common/diagnostics.hpp"
#include "parser/support.hpp"

#include <lexy/error.hpp>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace sisl::detail {
namespace {
namespace grammar {

struct ParseError {
  const ParseState &state;
  DiagnosticSink &diagnostics;

  struct Sink {
    const ParseState &state;
    DiagnosticSink &diagnostics;
    using return_type = std::size_t;

    SourceSpan range_or_token(Iterator begin, Iterator end) const {
      return begin == end ? state.token_span(begin) : state.span(begin, end);
    }

    [[noreturn]] void expected(std::string expectation, Iterator position,
                               SourceSpan source) const {
      if (position == state.end) {
        diagnostics.raise(DiagnosticCode::unexpected_end_of_file, source,
                          expectation);
      }
      diagnostics.raise(DiagnosticCode::expected_token, source, expectation);
    }

    static bool is_production(std::string_view actual,
                              std::string_view expected) {
      return actual == expected ||
             (actual.ends_with(expected) && actual.size() > expected.size() &&
              actual[actual.size() - expected.size() - 1] == ':');
    }

    template <typename Input, typename Reader, typename Tag>
    [[noreturn]] void operator()(const lexy::error_context<Input> &context,
                                 const lexy::error<Reader, Tag> &error) const {
      const auto production = std::string_view{context.production()};

      if constexpr (std::is_same_v<Tag, lexy::expected_literal>) {
        const auto literal = std::string_view{error.string(), error.length()};
        const auto mismatch = error.position() + error.index();
        const auto range_end = mismatch == state.end ? mismatch : mismatch + 1;
        auto expectation = "'" + std::string(literal) + "'";
        if (literal == ";" && is_production(production, "Width"))
          expectation = "';' after width declaration";
        expected(std::move(expectation), mismatch,
                 state.span(error.position(), range_end));
      } else if constexpr (std::is_same_v<Tag, lexy::expected_keyword>) {
        auto expectation =
            "keyword '" + std::string(error.string(), error.length()) + "'";
        expected(std::move(expectation), error.position(),
                 range_or_token(error.begin(), error.end()));
      } else if constexpr (std::is_same_v<Tag, lexy::expected_char_class>) {
        const auto character_class = std::string_view{error.name()};
        if (is_production(production, "Identifier") ||
            is_production(production, "Word") ||
            character_class == "ASCII.alpha-underscore") {
          expected("identifier", error.position(),
                   state.token_span(error.position()));
        }
        if (character_class.starts_with("digit.") ||
            (is_production(production, "IntegerLiteral") &&
             error.position() != context.position())) {
          diagnostics.raise(DiagnosticCode::invalid_integer_literal,
                            state.token_span(error.position()));
        }
        if (is_production(production, "IntegerLiteral")) {
          expected("integer literal", error.position(),
                   state.token_span(error.position()));
        }
        expected(std::string(error.name()), error.position(),
                 state.token_span(error.position()));
      } else {
        auto source = range_or_token(error.begin(), error.end());
        if (error.is(ExpectedInstructionFormatName{})) {
          expected("instruction format name after ':'", error.position(),
                   source);
        }
        if (error.is(ExpectedIntegerLiteral{})) {
          expected("integer literal", error.position(), source);
        }
        if (is_production(production, "IntegerLiteral")) {
          if (error.position() == context.position())
            expected("integer literal", error.position(),
                     state.token_span(context.position()));
          diagnostics.raise(DiagnosticCode::invalid_integer_literal,
                            state.token_span(context.position()));
        }
        if (is_production(production, "StringLiteral")) {
          if (error.is(lexy::missing_delimiter{}))
            diagnostics.raise(DiagnosticCode::invalid_string_literal,
                              state.span(context.position(), error.end()),
                              "unterminated");
          if (error.is(lexy::invalid_escape_sequence{}))
            diagnostics.raise(DiagnosticCode::invalid_string_literal, source,
                              "invalid escape");
          diagnostics.raise(DiagnosticCode::invalid_string_literal, source,
                            "malformed");
        }
        if (error.is(lexy::reserved_identifier{}))
          diagnostics.raise(DiagnosticCode::invalid_identifier, source);
        if (error.is(lexy::expected_eof{}))
          diagnostics.raise(DiagnosticCode::trailing_input, source);
        if (error.is(lexy::exhausted_choice{})) {
          if (error.position() == state.end) {
            const auto expects_brace =
                is_production(production, "Architecture") ||
                is_production(production, "Enum") ||
                is_production(production, "EncodingDeclaration") ||
                is_production(production, "Format") ||
                is_production(production, "Instruction") ||
                is_production(production, "Alias");
            expected(expects_brace ? "'}'" : "valid syntax", error.position(),
                     source);
          }
          diagnostics.raise(DiagnosticCode::unexpected_token, source);
        }
        if (error.is(lexy::missing_delimiter{}))
          expected("delimiter", error.position(), source);
        if (error.position() == state.end)
          expected("valid syntax", error.position(), source);
        diagnostics.raise(DiagnosticCode::invalid_syntax, source,
                          error.message());
      }
    }

    static std::size_t finish() { return 0; }
  };

  Sink sink() const { return {state, diagnostics}; }
};

} // namespace grammar
} // namespace
} // namespace sisl::detail
