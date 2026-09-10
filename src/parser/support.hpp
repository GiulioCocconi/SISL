#pragma once

#include "parser/ast.hpp"

#include <lexy/callback.hpp>
#include <lexy/dsl.hpp>
#include <lexy/input/string_input.hpp>

#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace sisl::detail {
namespace {
namespace grammar {

namespace dsl = lexy::dsl;

using Encoding = lexy::utf8_char_encoding;
using Iterator = const char *;
using Lexeme = lexy::string_lexeme<Encoding>;

struct ParseState {
  Iterator begin;
  Iterator end;

  SourceSpan span(Iterator first, Iterator last) const {
    return {static_cast<std::size_t>(first - begin),
            static_cast<std::size_t>(last - begin)};
  }

  SourceSpan through(SourceSpan first, Iterator last) const {
    return {first.begin, static_cast<std::size_t>(last - begin)};
  }

  SourceSpan token_span(Iterator position) const {
    if (position == end)
      return span(end, end);

    const auto is_boundary = [](char character) {
      return std::string_view{" \t\n\r\f\v{}[]<>;:=,-"}.contains(character);
    };
    auto token_end = position;
    if (is_boundary(*token_end)) {
      ++token_end;
    } else {
      while (token_end != end && !is_boundary(*token_end))
        ++token_end;
    }
    return span(position, token_end);
  }
};

struct ExpectedInstructionFormatName {};
struct ExpectedIntegerLiteral {};

template <typename Result, typename... Callbacks>
constexpr auto state_callback(Callbacks... callbacks) {
  return lexy::bind(lexy::callback<Result>(std::move(callbacks)...),
                    lexy::parse_state, lexy::values);
}

template <typename Result, typename Value> Result or_empty(Value value) {
  if constexpr (std::is_same_v<std::remove_cvref_t<Value>, lexy::nullopt>)
    return {};
  else
    return std::move(value);
}

template <typename Result, typename Declaration = Result>
constexpr auto spanned_declaration() {
  return state_callback<Result>(
      [](const ParseState &state, Iterator begin, auto value, Lexeme end) {
        return Declaration{std::move(value), state.span(begin, end.end())};
      });
}

template <typename Result, typename Entry, typename Append>
constexpr auto entry_sink(Append append) {
  return lexy::fold_inplace<Result>(
      [] { return Result{}; },
      [append](Result &result, Entry entry) {
        std::visit([&](auto value) { append(result, std::move(value)); },
                   std::move(entry));
      });
}

} // namespace grammar
} // namespace
} // namespace sisl::detail
