{
  description = "Nix development shell for the ESP32-C6-Touch-LCD-1.54 clock";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});

      hostTools =
        p: with p; [
          python312
          git
          wget
          cmake
          ninja
          ccache
          dfu-util
          libusb1
        ];

      setup = ''
        repo_root="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
        if [ ! -f "$repo_root/esp-idf/export.sh" ]; then
          echo "esp-idf submodule is missing. Run: git submodule update --init --recursive --depth 1" >&2
        else
          export IDF_TOOLS_PATH="$repo_root/.espressif"
          export PIP_CACHE_DIR="$IDF_TOOLS_PATH/.pip-cache"
          export IDF_COMPONENT_CACHE_PATH="$IDF_TOOLS_PATH/.component-cache"
          export CCACHE_DIR="$IDF_TOOLS_PATH/.ccache"
          py_ver="$(python3 -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
          if ! ls -d "$IDF_TOOLS_PATH"/python_env/idf*_py"$py_ver"_env >/dev/null 2>&1; then
            echo "Installing ESP-IDF tools for esp32c6 into $IDF_TOOLS_PATH (first run only)..." >&2
            "$repo_root/esp-idf/install.sh" esp32c6
          fi
          . "$repo_root/esp-idf/export.sh" >/dev/null
        fi
      '';
    in
    {
      devShells = forAllSystems (pkgs: {
        default =
          if pkgs.stdenv.isLinux then
            (pkgs.buildFHSEnv {
              name = "esp-idf";
              targetPkgs =
                p:
                hostTools p
                ++ (with p; [
                  flex
                  bison
                  gperf
                  gcc
                  pkg-config
                  libffi
                  openssl
                  zlib
                  ncurses
                  udev
                  stdenv.cc.cc.lib
                ]);
              profile = setup;
            }).env
          else
            pkgs.mkShellNoCC {
              packages = hostTools pkgs;
              shellHook = setup;
            };
      });
    };
}
