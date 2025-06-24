{
  description = "Scenefx development environment";
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixpkgs-unstable";
  outputs =
    { self, nixpkgs, ... }:
    let
      mkPackages = pkgs: {
        scenefx = pkgs.callPackage (
          { wlroots_0_19, ... }:
          pkgs.stdenv.mkDerivation {
            pname = "scenefx";
            version = "0.4.0-git";
            src = ./.;
            outputs = [
              "out"
              "lib"
            ];

            nativeBuildInputs = with pkgs; [
              pkg-config
              meson
              cmake
              ninja
              scdoc
              wayland-scanner
            ];

            buildInputs = with pkgs; [
              libdrm
              libxkbcommon
              pixman
              libGL # egl
              mesa # gbm
              wayland # wayland-server
              wayland-protocols
              wlroots_0_19
            ];

            meta = with pkgs.lib; {
              description = "A drop-in replacement for the wlroots scene API that allows wayland compositors to render surfaces with eye-candy effects";
              homepage = "https://github.com/wlrfx/scenefx";
              license = licenses.mit;
              platforms = platforms.linux;
            };
          }
        ) { };
      };

      targetSystems = [
        "aarch64-linux"
        "x86_64-linux"
      ];
      pkgsFor = system: import nixpkgs { inherit system; };
      forEachSystem = f: nixpkgs.lib.genAttrs targetSystems (system: f (pkgsFor system));
    in
    {
      overlays = rec {
        default = insert;
        override = _: prev: mkPackages prev;
        insert = _: prev: mkPackages (pkgsFor prev.system);
      };

      packages = forEachSystem (
        pkgs: (mkPackages pkgs) // { default = self.packages.${pkgs.system}.scenefx; }
      );

      devShells = forEachSystem (pkgs: {
        default = pkgs.mkShell {
          name = "scenefx-shell";
          inputsFrom = [
            self.packages.${pkgs.system}.scenefx
            pkgs.wlroots_0_19
          ];
          shellHook = ''
            (
              # Copy the nix version of wlroots into the project
              mkdir -p "$PWD/subprojects" && cd "$PWD/subprojects"
              cp -R --no-preserve=mode,ownership ${pkgs.wlroots_0_19.src} wlroots
            )'';
        };
      });

      formatter = forEachSystem (pkgs: pkgs.nixfmt-rfc-style);
    };
}
