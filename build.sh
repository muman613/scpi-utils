#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

build_dir="${script_dir}/build"
build_type="Debug"
generator=""
install_prefix=""
parallel=""
target=""
build_testing="ON"
verbose=0
configure=1
build=1
run_tests=0
install=0
fresh=0
list_targets=0

cmake_defs=()
cmake_config_args=()
cmake_build_args=()
ctest_args=()

usage() {
    cat <<'EOF'
Usage: ./build.sh [options]

Configure and build scpi-utils with CMake.

Common options:
  -h, --help                 Show this help text
  -B, --build-dir <dir>      Build directory (default: ./build)
  -G, --generator <name>     CMake generator, e.g. "Ninja"
  -t, --target <name>        Build a target (default: all)
  -j, --parallel [jobs]      Parallel build; omit jobs to let CMake choose
      --verbose              Enable verbose build output
      --fresh                Remove the build directory before configuring
      --configure-only       Configure but do not build
      --build-only           Build without running the configure step
      --list-targets         Configure, then print available build targets

Build type options:
      --build-type <type>    CMAKE_BUILD_TYPE (default: Debug)
      --debug                Shortcut for --build-type Debug
      --release              Shortcut for --build-type Release
      --relwithdebinfo       Shortcut for --build-type RelWithDebInfo
      --minsizerel           Shortcut for --build-type MinSizeRel

Project/CMake cache options:
      --tests                Enable BUILD_TESTING (default)
      --no-tests             Disable BUILD_TESTING
      --prefix <path>        CMAKE_INSTALL_PREFIX
  -D, --define <key=value>   Pass an extra -D cache definition to CMake

Post-build options:
      --test                 Run ctest after building
      --install              Run cmake --install after building

Pass-through options:
      --cmake-arg <arg>      Extra argument for cmake configure
      --build-arg <arg>      Extra argument for cmake --build
      --ctest-arg <arg>      Extra argument for ctest

Useful targets from this project include:
  all, clean, install, test, scpi-device, scpi-device-test,
  scpi-service, scpi-smoke, scpi-util

Examples:
  ./build.sh
  ./build.sh --release -j
  ./build.sh --target scpi-util --no-tests
  ./build.sh --prefix /usr/local --install
  ./build.sh --list-targets
EOF
}

require_value() {
    local option="$1"
    local value="${2:-}"
    if [[ -z "${value}" ]]; then
        echo "error: ${option} requires a value" >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -B|--build-dir)
            require_value "$1" "${2:-}"
            build_dir="$2"
            shift 2
            ;;
        -G|--generator)
            require_value "$1" "${2:-}"
            generator="$2"
            shift 2
            ;;
        -t|--target)
            require_value "$1" "${2:-}"
            target="$2"
            shift 2
            ;;
        -j|--parallel)
            if [[ "${2:-}" =~ ^[0-9]+$ ]]; then
                parallel="$2"
                shift 2
            else
                parallel="auto"
                shift
            fi
            ;;
        --verbose)
            verbose=1
            shift
            ;;
        --fresh)
            fresh=1
            shift
            ;;
        --configure-only)
            build=0
            shift
            ;;
        --build-only)
            configure=0
            shift
            ;;
        --list-targets)
            list_targets=1
            build=0
            shift
            ;;
        --build-type)
            require_value "$1" "${2:-}"
            build_type="$2"
            shift 2
            ;;
        --debug)
            build_type="Debug"
            shift
            ;;
        --release)
            build_type="Release"
            shift
            ;;
        --relwithdebinfo)
            build_type="RelWithDebInfo"
            shift
            ;;
        --minsizerel)
            build_type="MinSizeRel"
            shift
            ;;
        --tests)
            build_testing="ON"
            shift
            ;;
        --no-tests)
            build_testing="OFF"
            shift
            ;;
        --prefix)
            require_value "$1" "${2:-}"
            install_prefix="$2"
            shift 2
            ;;
        -D|--define)
            require_value "$1" "${2:-}"
            cmake_defs+=("-D$2")
            shift 2
            ;;
        --test)
            run_tests=1
            shift
            ;;
        --install)
            install=1
            shift
            ;;
        --cmake-arg)
            require_value "$1" "${2:-}"
            cmake_config_args+=("$2")
            shift 2
            ;;
        --build-arg)
            require_value "$1" "${2:-}"
            cmake_build_args+=("$2")
            shift 2
            ;;
        --ctest-arg)
            require_value "$1" "${2:-}"
            ctest_args+=("$2")
            shift 2
            ;;
        --)
            shift
            cmake_build_args+=("$@")
            break
            ;;
        *)
            echo "error: unknown option: $1" >&2
            echo "Run './build.sh --help' for usage." >&2
            exit 2
            ;;
    esac
done

if [[ "${fresh}" -eq 1 && "${configure}" -eq 0 ]]; then
    echo "error: --fresh cannot be used with --build-only" >&2
    exit 2
fi

if [[ "${run_tests}" -eq 1 && "${build_testing}" == "OFF" && "${configure}" -eq 1 ]]; then
    echo "error: --test requires tests; remove --no-tests" >&2
    exit 2
fi

if [[ "${fresh}" -eq 1 ]]; then
    rm -rf "${build_dir}"
fi

if [[ "${configure}" -eq 1 ]]; then
    configure_cmd=(
        cmake
        -S "${script_dir}"
        -B "${build_dir}"
        -DCMAKE_BUILD_TYPE="${build_type}"
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        -DBUILD_TESTING="${build_testing}"
    )

    if [[ -n "${generator}" ]]; then
        configure_cmd+=(-G "${generator}")
    fi

    if [[ -n "${install_prefix}" ]]; then
        configure_cmd+=(-DCMAKE_INSTALL_PREFIX="${install_prefix}")
    fi

    configure_cmd+=("${cmake_defs[@]}" "${cmake_config_args[@]}")

    echo "+ ${configure_cmd[*]}"
    "${configure_cmd[@]}"
fi

if [[ "${list_targets}" -eq 1 ]]; then
    cmake --build "${build_dir}" --target help
    exit 0
fi

if [[ "${build}" -eq 1 ]]; then
    build_cmd=(cmake --build "${build_dir}")

    if [[ -n "${target}" ]]; then
        build_cmd+=(--target "${target}")
    fi

    if [[ -n "${parallel}" ]]; then
        if [[ "${parallel}" == "auto" ]]; then
            build_cmd+=(--parallel)
        else
            build_cmd+=(--parallel "${parallel}")
        fi
    fi

    if [[ "${verbose}" -eq 1 ]]; then
        build_cmd+=(--verbose)
    fi

    build_cmd+=("${cmake_build_args[@]}")

    echo "+ ${build_cmd[*]}"
    "${build_cmd[@]}"
fi

if [[ "${run_tests}" -eq 1 ]]; then
    test_cmd=(ctest --test-dir "${build_dir}" --output-on-failure)
    test_cmd+=("${ctest_args[@]}")

    echo "+ ${test_cmd[*]}"
    "${test_cmd[@]}"
fi

if [[ "${install}" -eq 1 ]]; then
    install_cmd=(cmake --install "${build_dir}")

    echo "+ ${install_cmd[*]}"
    "${install_cmd[@]}"
fi
