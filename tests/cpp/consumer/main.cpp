#include <sisl/sisl.hpp>

#include <iostream>

int main() {
  const auto isa = sisl::Isa::load_string(R"(
    arch Tiny = {
      instr bit = { width = 1; [0] = value; assembly = "bit {value}"; };
    };
  )");
  if (isa.disassemble(isa.assemble("bit 1")) != "bit 1") {
    return 1;
  }
  std::cout << "SISL installed consumer passed\n";
  return 0;
}
