{
  description = "Logos Chat Module";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    nix-bundle-lgx.url = "github:logos-co/nix-bundle-lgx";
    # Local path → use the outer working tree directly. Lets the build see
    # in-progress changes without round-tripping through GitHub, and avoids
    # transitive fetchGit ref/rev resolution issues in the submodule chain.
    logos-chat.url = "git+file:///Users/arseniy/Waku/Logos/logos-chat?submodules=1";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
      externalLibInputs = {
        chat = inputs.logos-chat;
      };
      # TODO: The module builder copies the wrong header from the flake output.
      # liblogoschat.h lives in the source tree, not the build output.
      # Should be fixed in logos-module-builder (e.g. header_path in metadata.json).
      preConfigure = ''
        mkdir -p lib
        for f in $(find /nix/store -maxdepth 5 -name "liblogoschat.h" 2>/dev/null); do
          cp "$f" lib/ 2>/dev/null || true
        done
      '';
      tests = {
        dir = ./tests;
        mockCLibs = [ "logoschat" ];
      };
    };
}
