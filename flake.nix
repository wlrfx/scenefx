{
  description = "scenefx development environment";

  inputs = {
    flake-compat = {
      url = "github:edolstra/flake-compat";
      flake = false;
    };

    nixpkgs.url = "github:nixos/nixpkgs/nixpkgs-unstable";
  };

  outputs = { self, nixpkgs, flake-compat, ... }:
    let
      pkgsFor = system:
        import nixpkgs {
          inherit system;
          overlays = [ ];
        };

      targetSystems = [ "aarch64-linux" "x86_64-linux" ];
    in {
      devShells = nixpkgs.lib.genAttrs targetSystems (system:
        let pkgs = pkgsFor system;
        in {
          default = pkgs.mkShell {
            name = "scenefx-shell";

            inputsFrom = [ pkgs.wlroots_0_16 ];

            nativeBuildInputs = with pkgs; [
              cmake
              meson
              ninja
              scdoc
              pkg-config
            ];

            shellHook = with pkgs; ''(
              mkdir -p "$PWD/subprojects"
              cd "$PWD/subprojects"
              cp -R --no-preserve=mode,ownership ${wlroots_0_16.src} wlroots
            )'';
          };
        });
    };
}

