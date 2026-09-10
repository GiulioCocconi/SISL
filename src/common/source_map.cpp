#include "common/source_map.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace sisl::detail {
namespace {

// Count UTF-8 code points by ignoring continuation bytes. This converts the
// parser's byte offsets to the character-oriented positions editors display.
std::size_t code_point_count(std::string_view text) {
  return static_cast<std::size_t>(std::ranges::count_if(
      text, [](unsigned char byte) { return (byte & 0xc0u) != 0x80u; }));
}

} // namespace

SourceMap::SourceMap(std::string_view text, std::string source_name)
    : text_(text), source_name_(std::move(source_name)), line_starts_{0} {
  // Precompute line starts once so every diagnostic can locate its line with
  // a binary search instead of rescanning all preceding lines.
  for (std::size_t index = 0; index < text_.size(); ++index) {
    if (text_[index] == '\n') {
      line_starts_.push_back(index + 1);
    }
  }
}

SourceLocation SourceMap::location(SourceSpan span) const {
  // Clamp parser spans defensively before taking string views.
  const auto begin = std::min(span.begin, text_.size());
  const auto end = std::min(std::max(span.end, begin), text_.size());

  const auto line_end =
      std::upper_bound(line_starts_.begin(), line_starts_.end(), begin);
  const auto line =
      static_cast<std::size_t>(std::distance(line_starts_.begin(), line_end));
  const auto line_start = line_starts_[line - 1];

  return SourceLocation{
      .source = source_name_,
      .line = line,
      .column = code_point_count(text_.substr(line_start, begin - line_start)),
      .position = code_point_count(text_.substr(0, begin)) + 1,
      .span = std::max<std::size_t>(
          1, code_point_count(text_.substr(begin, end - begin))),
  };
}

} // namespace sisl::detail
