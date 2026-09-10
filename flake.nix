{
  description = "SISL native C++ instruction-set description library";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forAllSystems = function:
        nixpkgs.lib.genAttrs systems (system:
          function (import nixpkgs { inherit system; }));
    in
    {
      packages = forAllSystems (pkgs:
        let
          lexy = pkgs.stdenv.mkDerivation {
            pname = "lexy";
            version = "2025.05.0";

            src = pkgs.fetchFromGitHub {
              owner = "foonathan";
              repo = "lexy";
              rev = "982f36fe0f663153463657a166e4f173099dea6f";
              hash = "sha256-ONoMGos5Xo2JqvXwLmq6B7XH1eG25FVkSbgYKvr5QpI=";
            };

            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            cmakeFlags = [
              "-DLEXY_ENABLE_INSTALL=ON"
              "-DLEXY_BUILD_BENCHMARKS=OFF"
              "-DLEXY_BUILD_EXAMPLES=OFF"
              "-DLEXY_BUILD_TESTS=OFF"
              "-DLEXY_BUILD_DOCS=OFF"
              "-DLEXY_BUILD_PACKAGE=OFF"
            ];

            meta = {
              description = "C++ parsing DSL";
              homepage = "https://lexy.foonathan.net/";
              license = pkgs.lib.licenses.boost;
              platforms = pkgs.lib.platforms.unix;
            };
          };

          sisl = pkgs.stdenv.mkDerivation {
            pname = "sisl";
            version = "0.1.0";
	    src = ./.;

            strictDeps = true;
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            buildInputs = [ pkgs.boost pkgs.gtest lexy ];
            cmakeFlags = [
              "-DSISL_BUILD_TESTS=ON"
              "-DSISL_FETCH_LEXY=OFF"
            ];
            doCheck = true;
            checkPhase = ''
              runHook preCheck
              ctest --test-dir . --output-on-failure \
                --exclude-regex '^sisl\.cpp\.(install|consumer\.)'
              runHook postCheck
            '';
            doInstallCheck = true;
            installCheckPhase = ''
              runHook preInstallCheck
              cmake \
                -S ${./tests/cpp/consumer} \
                -B consumer-install-check \
                "-DSisl_DIR=$out/lib/cmake/Sisl"
              cmake --build consumer-install-check
              ./consumer-install-check/sisl_consumer
              runHook postInstallCheck
            '';
          };
        in
        {
          inherit lexy sisl;
          default = sisl;
        });

      checks = forAllSystems (pkgs: {
        sisl = self.packages.${pkgs.stdenv.hostPlatform.system}.sisl;
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = [
            pkgs.boost
            pkgs.clang-tools
            pkgs.cmake
            pkgs.gtest
            pkgs.ninja
            pkgs.stdenv.cc
            self.packages.${pkgs.stdenv.hostPlatform.system}.lexy
          ];
        };
      });
    };
}
