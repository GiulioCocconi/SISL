#pragma once

#include "parser/ast.hpp"
#include "runtime/model.hpp"

#include <memory>

namespace sisl::detail {

[[nodiscard]] std::shared_ptr<const Model> compile(const ArchitectureAst &ast,
                                                   const SourceMap &source_map);

} // namespace sisl::detail
