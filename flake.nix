{
  description = "Scenefx development environment";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixpkgs-unstable";

    systems.url = "github:nix-systems/default-linux";

	flake-utils = {
	  url = "github:numtide/flake-utils";
	  inputs.systems.follows = "systems";
	};
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
	    pkgs = import nixpkgs { inherit system; };

      in {
        packages = rec {
          scenefx-git = pkgs.stdenv.mkDerivation {
            pname = "scenefx";
            version = "git";
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
              libgbm
              libxcb
              libxcb-wm
            ];

            meta = with pkgs.lib; {
              description = "A drop-in replacement for the wlroots scene API that allows wayland compositors to render surfaces with eye-candy effects";
              homepage = "https://github.com/wlrfx/scenefx";
              license = licenses.mit;
              platforms = platforms.linux;
            };
          };

          default = scenefx-git;
		};

        devShells.default = pkgs.mkShell {
          name = "scenefx-shell";
	      inputsFrom = [ self.packages.${system}.scenefx-git ];
		  hardeningDisable = [ "fortify" ];
	    };

        formatter = pkgs.nixfmt;
      }
    );
}

