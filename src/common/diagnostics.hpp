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

#include "common/source_map.hpp"

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sisl::detail {

[[nodiscard]] std::string_view diagnostic_message(DiagnosticCode code);

// Diagnostics with catalogued messages share formatting, source mapping, and
// accumulation. The compiler accumulates errors; the parser raises the first.
class DiagnosticSink {
public:
  explicit DiagnosticSink(const SourceMap &source_map)
      : source_map_(source_map) {}

  template <typename... Args>
  void report(DiagnosticCode code, SourceSpan source, Args &&...args) {
    auto message =
        std::vformat(diagnostic_message(code), std::make_format_args(args...));
    diagnostics_.push_back(
        Diagnostic{code, std::move(message), source_map_.location(source), {}});
  }

  template <typename... Args>
  [[noreturn]] void raise(DiagnosticCode code, SourceSpan source,
                          Args &&...args) {
    report(code, source, std::forward<Args>(args)...);
    throw Error(take());
  }

  [[nodiscard]] bool empty() const noexcept { return diagnostics_.empty(); }
  [[nodiscard]] std::vector<Diagnostic> take() {
    return std::move(diagnostics_);
  }

private:
  const SourceMap &source_map_;
  std::vector<Diagnostic> diagnostics_;
};

[[noreturn]] void
raise_error(DiagnosticCode code, std::string message,
            std::optional<SourceLocation> source = std::nullopt);

} // namespace sisl::detail
