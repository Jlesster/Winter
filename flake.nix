{
  description = "winter - modal terminal editor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # pinning stable trux revision
    trux = {
      url = "github:Jlesster/trux/v0.2.0";
      inputs.nixpkgs.follows = "nixpkgs";
      flake = false;
    };

    # same tags as subprojects/*.wrap
    ts-core = {
      url = "github:tree-sitter/tree-sitter/v0.26.13";
      flake = false;
    };
    ts-c = {
      url = "github:tree-sitter/tree-sitter-c/v0.24.2";
      flake = false;
    };
    ts-cpp = {
      url = "github:tree-sitter/tree-sitter-cpp/v0.23.4";
      flake = false;
    };
    ts-java = {
      url = "github:tree-sitter/tree-sitter-java/v0.23.5";
      flake = false;
    };
    ts-lua = {
      url = "github:tree-sitter-grammars/tree-sitter-lua/v0.5.0";
      flake = false;
    };
    ts-rust = {
      url = "github:tree-sitter/tree-sitter-rust/v0.24.2";
      flake = false;
    };
  };

  outputs =
    inputs@{
      self,
      nixpkgs,
      trux,
      ...
    }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      lib = pkgs.lib;

      # keep in sync with the `ts_grammars` default in meson.options
      grammars = {
        c = inputs.ts-c;
        cpp = inputs.ts-cpp;
        java = inputs.ts-java;
        lua = inputs.ts-lua;
        rust = inputs.ts-rust;
      };
    in
    {
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname = "winter";
        version = "0.1.3";
        src = self;

        nativeBuildInputs = with pkgs; [
          meson
          ninja
          pkg-config
          makeWrapper
        ];
        buildInputs = [
          trux.packages.${system}.default
          pkgs.lua5_4
        ];

        mesonBuildType = "release";
        doCheck = false; # pty_test needs forkpty; try enabling later

        # replicate what the wraps would have fetched and patched
        postPatch = ''
          cp -r --no-preserve=mode ${inputs.ts-core} subprojects/tree-sitter
          cp subprojects/packagefiles/tree-sitter/meson.build subprojects/tree-sitter/
        ''
        + lib.concatStrings (
          lib.mapAttrsToList (n: src: ''
            cp -r --no-preserve=mode ${src} subprojects/tree-sitter-${n}
            cp subprojects/packagefiles/ts-grammar/meson.build subprojects/tree-sitter-${n}/
          '') grammars
        );

        postInstall = ''
          wrapProgram $out/bin/winter --suffix PATH : ${
            lib.makeBinPath (
              with pkgs;
              [
                git
                ripgrep
                wl-clipboard
                xclip
              ]
            )
          }
        '';

        meta = {
          description = "Modal terminal editor";
          mainProgram = "winter";
          platforms = lib.platforms.linux;
        };
      };

      devShells.${system}.default = pkgs.mkShell {
        buildInputs = [
          trux.packages.${system}.default
          pkgs.lua5_4
        ];
        packages = with pkgs; [
          meson
          ninja
          pkg-config
          cmake
          ccache
          mold
          tree-sitter
          git
        ];
        LDFLAGS = "-fuse-ld=mold";
      };
    };
}
