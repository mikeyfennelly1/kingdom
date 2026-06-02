#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."

main() {
    CLOSURE_NAME="kds-closure"
    out_dir="${PROJECT_ROOT}/out"
    mkdir -p "${out_dir}"
    cmake_build_dir_location="${PROJECT_ROOT}/build/kds/kds"
    output_tarball_location="${out_dir}/${CLOSURE_NAME}.tar.gz"
    dependency_graph_file="${out_dir}/${CLOSURE_NAME}.dot"

    echo "DEBUG: constructing ldd closure dependency graph"
    construct_runtime_closure_graph "${cmake_build_dir_location}" "${dependency_graph_file}"

    local svg_output_location="${out_dir}/${CLOSURE_NAME}.svg"
    echo "DEBUG: generating SVG from dependency graph"
    dot -Tsvg "${dependency_graph_file}" -o "${svg_output_location}"
    printf "DEBUG: SVG written to:\n\t ${svg_output_location}\n"

    printf "DEBUG: constructing tarball for minimum closure\n"
    construct_min_closure_tarball "${cmake_build_dir_location}" "${output_tarball_location}"
    if [[ $? -ne 0 ]]; then
        printf "ERROR: an error occurred constructing tarball\n"
        exit 1
    fi

    printf "INFO: successfully created ${output_tarball_location}\n"

    return 0
}

# ─── helpers ────────────────────────────────────────────────

# Constructs a tarball with thintroduction tactic
# artifacts
function construct_min_closure_tarball() {
    cmake_build_dir_location=$1
    output_tarball_location=$2
    if [[ -z "${output_tarball_location}" ]]; then
        printf "ERROR: argument output_tarball_location is an empty string \n"
        return 1
    elif [[ -z "${cmake_build_dir_location}" ]]; then
        printf "ERROR: argument cmake_build_dir_location is an empty string \n"
        return 1
    fi
    printf "DEBUG: constructing minimum runtime closure \n"
    # use ldd to list dynamic dependencies
    #   see -> https://man7.org/linux/man-pages/man1/ldd.1.html
    if ! ldd "${cmake_build_dir_location}" \
      | awk '$2=="=>" && $3~/^\/nix\/store/ { n=split($3,a,"/"); print "/nix/store/"a[4] }' \
      | sort -u \
      | xargs nix-store --query --requisites \
      | sort -u \
      | tar -czPf "${output_tarball_location}" --files-from=-; then
        printf "ERROR: an error occurred constructing tarball from nix store dependencies\n"
        return 1
    fi

    return 0
}

function construct_runtime_closure_graph() {
    local cmake_build_dir_location=$1
    local output_dot_location=${2:-"/tmp/kds-closure.dot"}

    if [[ -z "${cmake_build_dir_location}" ]]; then
        printf "ERROR: argument cmake_build_dir_location is an empty string\n"
        return 1
    fi

    printf "constructing runtime closure dependency graph for %s\n" "${cmake_build_dir_location}"

    local requisites
    requisites=$(ldd "${cmake_build_dir_location}" \
      | awk '$2=="=>" && $3~/^\/nix\/store/ { n=split($3,a,"/"); print "/nix/store/"a[4] }' \
      | sort -u \
      | xargs nix-store --query --requisites \
      | sort -u)

    if [[ -z "${requisites}" ]]; then
        printf "ERROR: no nix store dependencies found for %s\n" "${cmake_build_dir_location}"
        return 1
    fi

    echo "${requisites}" \
      | xargs nix-store --query --graph \
      > "${output_dot_location}"

    printf "dependency graph written to %s\n" "${output_dot_location}"
    return 0
}

# ─────────────────────────────────────────────────────────────
main "$@"
