#pragma once

// Compatibility umbrella for internal users. Production translation units use
// the narrower headers directly so parser, compiler, and runtime dependencies
// remain visible at their include sites.
#include "common/diagnostics.hpp"
#include "common/source_map.hpp"
#include "compiler/compiler.hpp"
#include "parser/ast.hpp"
#include "runtime/model.hpp"
