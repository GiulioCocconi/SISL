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

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sisl::detail {

struct SourceSpan {
  std::size_t begin = 0;
  std::size_t end = 0;
};

class SourceMap {
public:
  SourceMap(std::string_view text, std::string source_name);

  [[nodiscard]] SourceLocation location(SourceSpan span) const;

private:
  std::string_view text_;
  std::string source_name_;
  std::vector<std::size_t> line_starts_;
};

} // namespace sisl::detail
