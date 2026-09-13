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

#include <sisl/sisl.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sisl::test {

template <typename T>
std::string indexed_parameter_name(const testing::TestParamInfo<T> &info) {
  return "Case" + std::to_string(info.index + 1);
}

inline std::vector<Diagnostic> diagnostics_for(std::string_view source) {
  try {
    (void)Isa::load_string(source);
  } catch (const Error &error) {
    return error.diagnostics();
  }
  return {};
}

inline std::optional<Diagnostic> diagnostic_for(std::string_view source,
                                                DiagnosticCode code) {
  auto diagnostics = diagnostics_for(source);
  const auto diagnostic = std::find_if(
      diagnostics.begin(), diagnostics.end(),
      [&](const Diagnostic &candidate) { return candidate.code == code; });
  return diagnostic == diagnostics.end()
             ? std::nullopt
             : std::optional<Diagnostic>{std::move(*diagnostic)};
}

inline std::string architecture(std::string_view body) {
  return "arch Test = { " + std::string(body) + " };";
}

inline std::string_view source_text(std::string_view source,
                                    const Diagnostic &diagnostic) {
  if (!diagnostic.source || !diagnostic.source->position ||
      !diagnostic.source->span) {
    return {};
  }
  const auto begin = static_cast<std::size_t>(*diagnostic.source->position - 1);
  return source.substr(begin,
                       static_cast<std::size_t>(*diagnostic.source->span));
}

} // namespace sisl::test
