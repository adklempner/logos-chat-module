# Install pre-generated API headers (avoids logos-cpp-generator which segfaults
# on some macOS versions in nix builds). The generated/ directory in the source
# tree contains chat_module_api.{cpp,h} produced by the generator offline.
{ pkgs, common, src, lib, logosSdk }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-headers";
  version = common.version;

  inherit src;
  inherit (common) meta;

  dontConfigure = true;
  dontBuild = true;

  installPhase = ''
    mkdir -p $out/include
    cp generated/* $out/include/
  '';
}
