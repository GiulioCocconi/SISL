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
