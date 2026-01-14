# Nix development shell for Papyrix Reader
#
# Usage:
#   nix-shell              # Enter development environment
#   nix-shell --pure       # Enter pure (isolated) environment
#
{ pkgs ? import <nixpkgs> {} }:

let
  pythonEnv = pkgs.python312.withPackages (ps: with ps; [
    freetype-py  # Font conversion scripts
    pillow       # Image conversion scripts
  ]);
in
pkgs.mkShell {
  name = "papyrix-dev";

  buildInputs = with pkgs; [
    # PlatformIO for ESP32 firmware builds
    platformio-core

    # Python environment with packages
    pythonEnv

    # Code quality tools
    clang-tools  # clang-format
    cppcheck

    # Version control
    git

    # Build tools
    gnumake
  ];

  shellHook = ''
    echo "Papyrix Reader Development Environment"
    echo "  make build   - Build firmware"
    echo "  make check   - Run cppcheck"
    echo "  make format  - Format code"
  '';
}
