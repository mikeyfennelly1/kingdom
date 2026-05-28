{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = [
    pkgs.nodejs_20_x
    pkgs.nodePackages.typescript@5.4.5
    pkgs.nodePackages.ts-node
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
