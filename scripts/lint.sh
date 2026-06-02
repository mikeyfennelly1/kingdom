#!/usr/bin/env bash
# lint.sh - Run clang-format and clang-tidy checks on project source files
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}/.."
BUILD_DIR="${PROJECT_ROOT}/build"

# Source dirs to lint — excludes build/, blockchain/, and third-party
SOURCE_DIRS=("libkd" "kds" "kdctl" "tests")

FAILED=0

# ─── helpers ────────────────────────────────────────────────────

function collect_sources() {
    local files=()
    for dir in "${SOURCE_DIRS[@]}"; do
        local full="${PROJECT_ROOT}/${dir}"
        if [[ -d "${full}" ]]; then
            while IFS= read -r -d '' f; do
                files+=("$f")
            done < <(find "${full}" \( -name "*.cc" -o -name "*.hh" \) -print0)
        fi
    done
    echo "${files[@]+"${files[@]}"}"
}

function check_format() {
    echo "=== clang-format ==="
    local bad=()
    for f in "$@"; do
        if ! clang-format --dry-run --Werror "${f}" 2>/dev/null; then
            bad+=("${f}")
        fi
    done

    if [[ ${#bad[@]} -gt 0 ]]; then
        echo "Format violations in:"
        for f in "${bad[@]}"; do
            echo "  ${f#"${PROJECT_ROOT}/"}"
        done
        echo "Run 'task format' or clang-format -i on the above files."
        return 1
    fi

    echo "Format OK"
    return 0
}

function check_tidy() {
    echo ""
    echo "=== clang-tidy ==="

    if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
        echo "ERROR: ${BUILD_DIR}/compile_commands.json not found." >&2
        echo "Run 'cmake -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON' first." >&2
        return 1
    fi

    local tidy_failed=0
    for f in "$@"; do
        if ! clang-tidy -p "${BUILD_DIR}" --config-file="${PROJECT_ROOT}/.clang-tidy" "${f}" 2>&1; then
            tidy_failed=1
        fi
    done

    if [[ ${tidy_failed} -ne 0 ]]; then
        echo "clang-tidy reported issues."
        return 1
    fi

    echo "Tidy OK"
    return 0
}

# ─── main ────────────────────────────────────────────────────────

function main() {
    cd "${PROJECT_ROOT}"

    # Parse flags
    local run_format=1
    local run_tidy=1
    for arg in "$@"; do
        case "${arg}" in
            --format-only) run_tidy=0 ;;
            --tidy-only)   run_format=0 ;;
            --help|-h)
                echo "Usage: lint.sh [--format-only | --tidy-only]"
                exit 0
                ;;
        esac
    done

    # Collect files
    local sources
    read -ra sources <<< "$(collect_sources)"

    if [[ ${#sources[@]} -eq 0 ]]; then
        echo "No source files found."
        exit 0
    fi

    echo "Checking ${#sources[@]} file(s) in: ${SOURCE_DIRS[*]}"
    echo ""

    if [[ ${run_format} -eq 1 ]]; then
        check_format "${sources[@]}" || FAILED=1
    fi

    if [[ ${run_tidy} -eq 1 ]]; then
        check_tidy "${sources[@]}" || FAILED=1
    fi

    echo ""
    if [[ ${FAILED} -ne 0 ]]; then
        echo "LINT FAILED"
        exit 1
    fi

    echo "All checks passed."
}

main "$@"
