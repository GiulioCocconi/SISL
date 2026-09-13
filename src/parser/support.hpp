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
