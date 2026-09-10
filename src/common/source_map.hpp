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
