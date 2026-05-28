{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = [
    pkgs.nodejs_20_x
    pkgs.nodePackages.typescript
    pkgs.nodintroduction tacticts-node
  ];

  shellHook = ''
    echo "---------------------------------------------------------"
    echo " Kingdom Rigorous Test Shell "
    echo " Node: $(node --version)"
    echo " npm: $(npm --version)"
    echo " tsc: $(tsc --version)"
    echo "---------------------------------------------------------"
  '';
}
