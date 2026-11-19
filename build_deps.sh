#!/usr/bin/env bash
# =============================================================================
# build_deps.sh -- build every third-party dependency into external/install
#
# After this finishes, external/install holds boost, libsodium, libOTe and
# secure-join, so `cmake ..` no longer depends on what the host distribution
# happens to ship.
#
# Usage:
#   ./build_deps.sh              # build what is missing
#   ./build_deps.sh --clean      # wipe external/install and all build trees
#   ./build_deps.sh --jobs 8     # limit parallelism (default: nproc)
#
# The script is idempotent: components that are already installed are skipped.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

INSTALL_DIR="$SCRIPT_DIR/external/install"
LOG_FILE="$SCRIPT_DIR/build_deps.log"

# ---- Pinned versions -------------------------------------------------------
# libOTe must match what secure-join was tested against.
LIBOTE_COMMIT="657f6da90bff5774a2d01c824e997572d5e8ba00"
SECURE_JOIN_COMMIT="377ca63b9d8f4f6aede0d3a2e3d9078973a3ee10"
# libsodium >= 1.0.19 is required: cryptoTools uses crypto_scalarmult_noclamp,
# which was added in that release (Ubuntu 24.04 ships 1.0.18).
LIBSODIUM_VERSION="1.0.19"

JOBS="$(nproc)"
CLEAN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean) CLEAN=1; shift ;;
        --jobs)  JOBS="$2"; shift 2 ;;
        -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

# Everything below is appended to the log as well as the terminal.
exec > >(tee -a "$LOG_FILE") 2>&1

step()  { echo; echo "============================================================"; \
          echo ">>> $*"; echo "============================================================"; }
info()  { echo "    $*"; }
die()   { echo "ERROR: $*" >&2; exit 1; }

echo "Build started: $(date)"
echo "Project:       $SCRIPT_DIR"
echo "Install dir:   $INSTALL_DIR"
echo "Jobs:          $JOBS"
echo "Log:           $LOG_FILE"

# ---- 0. Toolchain ----------------------------------------------------------
step "[0/5] Checking toolchain"
for tool in git cmake make g++ python3; do
    command -v "$tool" >/dev/null 2>&1 || die \
        "'$tool' not found. Install it first, e.g.: sudo apt install -y build-essential cmake git"
    info "$tool: $($tool --version 2>&1 | head -1)"
done

# libsodium is the only system dependency we still need headers for at build
# time; it is installed into external/install below, but the build tools for
# it (autotools) are required.
for tool in make automake autoconf libtoolize; do
    command -v "$tool" >/dev/null 2>&1 || info "note: '$tool' missing (needed only to rebuild libsodium)"
done

if [[ $CLEAN -eq 1 ]]; then
    step "Cleaning previous builds"
    info "removing external/install and dependency build trees"
    rm -rf "$INSTALL_DIR" \
           "$SCRIPT_DIR/external/libOTe/out" \
           "$SCRIPT_DIR/external/secure-join/out"
fi

mkdir -p "$INSTALL_DIR"

# ---- 1. Submodules ---------------------------------------------------------
step "[1/5] Preparing submodules"
for sub in libOTe secure-join; do
    if [[ ! -e "$SCRIPT_DIR/external/$sub/.git" ]]; then
        info "initialising $sub"
        git submodule update --init --recursive "external/$sub" \
            || die "failed to initialise submodule $sub"
    fi
done

info "libOTe -> $LIBOTE_COMMIT"
git -C "$SCRIPT_DIR/external/libOTe" fetch --quiet origin || info "warning: fetch failed, using local refs"
git -C "$SCRIPT_DIR/external/libOTe" checkout --quiet "$LIBOTE_COMMIT" \
    || die "cannot check out libOTe $LIBOTE_COMMIT"
git -C "$SCRIPT_DIR/external/libOTe" submodule update --init --recursive --quiet

info "secure-join -> $SECURE_JOIN_COMMIT"
git -C "$SCRIPT_DIR/external/secure-join" fetch --quiet origin || info "warning: fetch failed, using local refs"
git -C "$SCRIPT_DIR/external/secure-join" checkout --quiet "$SECURE_JOIN_COMMIT" \
    || die "cannot check out secure-join $SECURE_JOIN_COMMIT"
git -C "$SCRIPT_DIR/external/secure-join" submodule update --init --recursive --quiet

# cryptoTools' vendored FIND_SODIUM() hard-codes the filename "libsodium.a",
# which (a) misses the shared library in external/install and (b) makes the
# linker pull in libsodium's ed25519 symbols, clashing with SimplestOT's
# ge25519_* definitions. Switching to the standard name "sodium" lets CMake
# resolve the shared library and avoid both problems.
# The edit lives inside a submodule, so it is re-applied here every run.
step "Patching cryptoTools FIND_SODIUM"
CT_HELPER="$SCRIPT_DIR/external/libOTe/cryptoTools/cmake/cryptoToolsDepHelper.cmake"
if [[ -f "$CT_HELPER" ]]; then
    if grep -q 'set(SODIUM_LIB_NAME libsodium\.a)' "$CT_HELPER"; then
        sed -i 's|set(SODIUM_LIB_NAME libsodium\.a)|set(SODIUM_LIB_NAME sodium)|' "$CT_HELPER"
        info "patched: libsodium.a -> sodium"
    else
        info "already patched (or upstream changed), skipping"
    fi
else
    info "note: $CT_HELPER not found, skipping"
fi

# secure-join links against "libOTe", but an installed libOTe exports the
# namespaced target oc::libOTe only (the plain name does not exist outside
# libOTe's own build tree). Linking the wrong name silently drops the
# transitive include directories, so every secure-join source then fails with
# "cryptoTools/Common/Defines.h: No such file or directory".
step "Patching secure-join link target"
SJ_CMAKE="$SCRIPT_DIR/external/secure-join/secure-join/CMakeLists.txt"
if [[ -f "$SJ_CMAKE" ]]; then
    if grep -q '^target_link_libraries(secureJoin libOTe)$' "$SJ_CMAKE"; then
        python3 - "$SJ_CMAKE" <<'PY'
import io, sys
p = sys.argv[1]
s = io.open(p, encoding="utf-8").read()
s = s.replace(
    "target_link_libraries(secureJoin libOTe)",
    "if(TARGET oc::libOTe)\n"
    "    target_link_libraries(secureJoin oc::libOTe)\n"
    "else()\n"
    "    target_link_libraries(secureJoin libOTe)\n"
    "endif()",
)
io.open(p, "w", encoding="utf-8").write(s)
PY
        info "patched: libOTe -> oc::libOTe"
    else
        info "already patched (or upstream changed), skipping"
    fi
else
    info "note: $SJ_CMAKE not found, skipping"
fi

# Some networks cannot reach github.com reliably; the vendored fetch scripts of
# libOTe and secure-join clone extra repos mid-configure. Point them at a
# mirror if the user asked for one.
if [[ -n "${GITHUB_MIRROR:-}" ]]; then
    step "Rewriting github.com -> $GITHUB_MIRROR in vendored fetch scripts"
    for dir in "$SCRIPT_DIR/external/libOTe" "$SCRIPT_DIR/external/secure-join"; do
        mapfile -t files < <(grep -rl "https://github\.com/" \
            --include="*.cmake" --include="CMakeLists.txt" "$dir" 2>/dev/null \
            | grep -v "/out/" || true)
        for f in "${files[@]}"; do
            sed -i "s|https://github\.com/|https://${GITHUB_MIRROR}/|g" "$f"
        done
        info "$dir: ${#files[@]} file(s) rewritten"
    done
fi

# ---- 2. libsodium ----------------------------------------------------------
step "[2/5] libsodium $LIBSODIUM_VERSION"
if [[ -f "$INSTALL_DIR/lib/libsodium.so" || -f "$INSTALL_DIR/lib/libsodium.a" ]] \
   && grep -q "\"$LIBSODIUM_VERSION\"" "$INSTALL_DIR/include/sodium/version.h" 2>/dev/null; then
    info "already installed in external/install, skipping"
else
    SODIUM_SRC="$SCRIPT_DIR/external/.deps/libsodium"
    mkdir -p "$SCRIPT_DIR/external/.deps"
    if [[ ! -f "$SODIUM_SRC/configure" ]]; then
        tarball="$SCRIPT_DIR/external/.deps/libsodium-$LIBSODIUM_VERSION.tar.gz"
        if [[ ! -s "$tarball" ]]; then
            info "downloading libsodium $LIBSODIUM_VERSION"
            url="https://github.com/jedisct1/libsodium/releases/download/${LIBSODIUM_VERSION}-RELEASE/libsodium-${LIBSODIUM_VERSION}.tar.gz"
            curl -fsSL -o "$tarball" "$url" \
                || curl -fsSL -o "$tarball" "https://download.libsodium.org/libsodium/releases/libsodium-${LIBSODIUM_VERSION}.tar.gz" \
                || die "failed to download libsodium $LIBSODIUM_VERSION"
        fi
        mkdir -p "$SODIUM_SRC"
        tar xzf "$tarball" -C "$SODIUM_SRC" --strip-components=1 \
            || die "failed to extract libsodium tarball"
    fi
    (
        cd "$SODIUM_SRC"
        info "configuring"
        # Both static and shared: cryptoTools' FIND_SODIUM() looks for
        # libsodium.a explicitly, while the shared library avoids the symbol
        # clash between libsodium's ed25519 code and libOTe's SimplestOT.
        ./configure --prefix="$INSTALL_DIR" --enable-shared --enable-static >/dev/null
        info "building with $JOBS jobs"
        make -j"$JOBS" >/dev/null
        make install >/dev/null
    )
    info "installed: $(grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+' "$INSTALL_DIR/include/sodium/version.h" | head -1)"
fi

# ---- 3. libOTe -------------------------------------------------------------
step "[3/5] libOTe"

# Force the libsodium from external/install. cryptoTools' FIND_SODIUM() would
# otherwise search the default prefixes and can end up linking the system
# static library, whose ed25519 symbols collide with SimplestOT's.
SODIUM_LIB_ARG=()
if [[ -f "$INSTALL_DIR/lib/libsodium.so" ]]; then
    SODIUM_LIB_ARG=(-DSODIUM_LIBRARIES="$INSTALL_DIR/lib/libsodium.so"
                    -DSODIUM_INCLUDE_DIRS="$INSTALL_DIR/include")
    info "libsodium: $INSTALL_DIR/lib/libsodium.so"
elif [[ -f "$INSTALL_DIR/lib/libsodium.a" ]]; then
    SODIUM_LIB_ARG=(-DSODIUM_LIBRARIES="$INSTALL_DIR/lib/libsodium.a"
                    -DSODIUM_INCLUDE_DIRS="$INSTALL_DIR/include")
    info "libsodium: $INSTALL_DIR/lib/libsodium.a (shared library not found)"
else
    die "libsodium not found in $INSTALL_DIR/lib"
fi

if [[ -f "$INSTALL_DIR/lib/liblibOTe.a" ]]; then
    info "already installed in external/install, skipping"
    info "(use --clean to force a rebuild)"
else
    (
        cd "$SCRIPT_DIR/external/libOTe"
        info "building (this takes a while: it also builds boost into external/install)"
        python3 build.py \
            -DENABLE_CIRCUITS=ON -DENABLE_BOOST=ON -DENABLE_SODIUM=ON \
            -DSODIUM_MONTGOMERY=OFF \
            -DENABLE_SSE=ON -DENABLE_AES=ON -DCOPROTO_ENABLE_BOOST=ON \
            -DENABLE_ALL_OT=ON -DENABLE_SILENT_VOLE=ON \
            -DCMAKE_PREFIX_PATH="$INSTALL_DIR" \
            "${SODIUM_LIB_ARG[@]}" \
            -DCMAKE_CXX_FLAGS="-march=native" \
            || die "libOTe build failed"
        cd out/build/linux
        cmake --install . --prefix "$INSTALL_DIR" >/dev/null
    )
    info "installed to external/install"
fi

# ---- 4. secure-join --------------------------------------------------------
step "[4/5] secure-join"
if [[ -f "$INSTALL_DIR/lib/libsecureJoin.a" ]]; then
    info "already installed in external/install, skipping"
    info "(use --clean to force a rebuild)"
else
    (
        cd "$SCRIPT_DIR/external/secure-join"
        # FETCH_LIBOTE=OFF makes it reuse the libOTe installed above instead of
        # cloning and building a private copy.
        info "configuring"
        cmake -S . -B out/build/linux \
            -D CMAKE_BUILD_TYPE=Release \
            -D SODIUM_MONTGOMERY=false \
            -D COPROTO_ENABLE_BOOST=ON \
            -DFETCH_LIBOTE=OFF \
            -DSECUREJOIN_THIRDPARTY_DIR="$INSTALL_DIR" \
            -DCMAKE_PREFIX_PATH="$INSTALL_DIR" \
            "${SODIUM_LIB_ARG[@]}" \
            >/dev/null || die "secure-join configure failed"
        info "building with $JOBS jobs"
        # Build the library target only. The bundled secJoinfrontend demo needs
        # libtests_cryptoTools, which is not part of the installed set, and
        # nothing in this project uses that demo.
        cmake --build out/build/linux --parallel "$JOBS" --target secureJoin \
            || die "secure-join build failed"
        cd out/build/linux
        cmake --install . --prefix "$INSTALL_DIR" >/dev/null
    )
    info "installed to external/install"
fi

# ---- 5. Verify -------------------------------------------------------------
step "[5/5] Verifying external/install"

fail=0
check_file() {
    if [[ -e "$INSTALL_DIR/$1" ]]; then
        info "OK      $1"
    else
        info "MISSING $1"
        fail=1
    fi
}

check_file include/boost/version.hpp
check_file include/sodium.h
check_file include/libOTe/Tools/Tools.h
check_file include/secureJoin/secure-join/Defines.h
check_file lib/libsodium.so
check_file lib/liblibOTe.a
check_file lib/libcryptoTools.a
check_file lib/libsecureJoin.a
check_file lib/cmake/libOTe/libOTeConfig.cmake
check_file lib/cmake/secureJoin/secureJoinConfig.cmake

boost_ver="$(grep -o '"1_[0-9]*"' "$INSTALL_DIR/include/boost/version.hpp" 2>/dev/null | head -1 | tr -d '"')"
sodium_ver="$(grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+' "$INSTALL_DIR/include/sodium/version.h" 2>/dev/null | head -1)"
info "boost:      ${boost_ver:-unknown}"
info "libsodium:  ${sodium_ver:-unknown}"
info "libOTe:     $(git -C "$SCRIPT_DIR/external/libOTe" rev-parse --short HEAD 2>/dev/null || echo unknown)"
info "secure-join:$(git -C "$SCRIPT_DIR/external/secure-join" rev-parse --short HEAD 2>/dev/null || echo unknown)"

echo
if [[ $fail -ne 0 ]]; then
    echo "============================================================"
    echo " DEPENDENCY BUILD FAILED -- see $LOG_FILE"
    echo "============================================================"
    exit 1
fi

echo "============================================================"
echo " All dependencies installed into external/install"
echo " Finished: $(date)"
echo
echo " Next step:"
echo "   mkdir -p build && cd build && cmake .. && make -j"
echo "============================================================"
