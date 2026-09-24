# SISL

SISL is a portable C++23 library and declarative language for describing CPU architecture instruction formats, it is designed to work with [SILICON](https://github.com/GiulioCocconi/SILICON) but could be used stand-alone
.
An ISA definition declares logical fields, their encoded bit ranges, fixed instruction values, assembly syntax, aliases, and byte order.

The implementation is native C++. It uses
[foonathan/lexy](https://lexy.foonathan.net/) for the complete declarative,
source-preserving grammar and
Boost.Multiprecision for arbitrary-width instruction values. Neither lexy nor
any parser type appears in the public API.

## Example

```sisl
arch Tiny = {
  endianness = Little;

  enum R8 : bits<3> = {
    B = 0b000;
    C = 0b001;
    A = 0b111;
  };

  instr-format unary = {
    width = 8;
    [7:6] = opcode;
    [5:3] = reserved;
    [2:0] = reg : R8;
  };

  instr load : unary = {
    encoding = { opcode = 1; reserved = 0; };
    assembly = "load {reg}";
  };

  alias load-a = {
    instr = load;
    encoding = { reg = A; };
    assembly = "load-a";
  };
};
```

`load B` encodes as `0b01000000`.

## Language

Each file contains one `arch Name = { ... };` declaration. Architecture
entries may occur in any order and references can point forward.

- `endianness = Little;` or `Big` controls only integer/byte conversion and
  defaults to `Little`.
- `enum Name : bits<N> = { Member = value; ... };` declares a symbolic,
  possibly sparse operand type.
- Field types are `bits<N>`, `uint<N>`, `sint<N>`, or a named enum.
- `instr-format child : parent = { ... };` inherits width, fields, and bit
  mappings. Cycles, duplicate fields, and overlaps are diagnosed.
- `[31:20] = imm : sint<12>;` declares and maps a complete field.
- A scattered field is declared first, then mapped in slices such as
  `[31] = imm[12];` and `[30:25] = imm[10:5];`.
- `encoding = { opcode = 0x13; ... };` fixes identifying fields.
- `assembly = "addi {rd}, {rs1}, {imm}";` declares text syntax. Template
  whitespace is flexible; punctuation is literal.
- `alias` declarations target an instruction, fix more operands, and take
  precedence during disassembly in declaration order.

## C++ API

```cpp
#include <sisl/sisl.hpp>

auto isa = sisl::Isa::load_file("riscv.isa");
auto encoded = isa.encode(
    "add", {{"rd", std::string("x1")},
            {"rs1", std::string("x2")},
            {"rs2", std::string("x3")}});

auto decoded = isa.decode(encoded);
auto text = isa.disassemble(encoded); // "add x1, x2, x3"

// Read resolved formats, fields, enums, instructions, and aliases.
auto description = isa.describe();
```

## Building

SISL requires CMake 3.24, a C++23 compiler, and Boost headers. Building the
tests also requires an installed GoogleTest package. SISL first looks for lexy
2025.05 as a CMake package. If none is available,
`SISL_FETCH_LEXY=ON` (the default) downloads the pinned 2025.05.0 archive with
hash verification.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /desired/prefix
```

Useful options are `SISL_BUILD_TESTS`, `SISL_INSTALL`,
`SISL_BUILD_SHARED_LIBS`, and `SISL_FETCH_LEXY`. Installed consumers use either
compatible target spelling:

```cmake
find_package(Sisl 0.1 CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE Sisl::Sisl)
# sisl::sisl is retained for compatibility.
```

lexy is a private build dependency and is not required by an installed SISL
consumer.

## Nix

The locked flake exposes `lexy`, `sisl`, and the default package on Linux and
Darwin. Its check runs the complete CTest suite, including installation and
downstream `find_package` and `add_subdirectory` consumers.

```sh
nix build
nix build .#lexy
nix flake check
nix develop
```
