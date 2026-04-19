#!/usr/bin/env bash
#  _____   ____  __ ___ ___ _____ ___  ___  _  _
# / __\ \ / /  \/  | _ )_ _|_   _| _ \/ _ \| \| |
# \__ \\ V /| |\/| | _ \| |  | | |   / (_) | .` |
# |___/ |_| |_|  |_|___/___| |_| |_|_\\___/|_|\_|
# Incremental Build Script (Linux)

set -e

# ── Architecture selection ($1 = 32bit -> x86, default = x64) ───────────────
ARCH="64"
if [[ "${1,,}" == "32bit" ]]; then
    ARCH="32"
fi

CONFIG_FILE="installer_config.txt"
HASH_FILE="object_hashes.txt"
BIN_DIR="bin${ARCH}"
EXE_DIR="exe${ARCH}"


# ═════════════════════════════════════════════════════════════════════════════
#  Config Parser
# ═════════════════════════════════════════════════════════════════════════════

parse_config() {
    if [[ ! -f "$CONFIG_FILE" ]]; then
        generate_default_config
    fi
    while IFS='=' read -r key value; do
        # Skip comments and blank lines
        [[ "$key" =~ ^[[:space:]]*# ]] && continue
        [[ -z "$key" ]] && continue
        # Trim whitespace
        key="${key// /}"
        eval "CFG_${key}=\"${value}\""
    done < "$CONFIG_FILE"
}


# ═════════════════════════════════════════════════════════════════════════════
#  Source Discovery
# ═════════════════════════════════════════════════════════════════════════════

discover_sources() {
    ALL_SOURCES=""

    # Explicit source files
    if [[ -n "$CFG_SOURCE_FILES" ]]; then
        ALL_SOURCES="$CFG_SOURCE_FILES"
    fi

    # Source folders — convert backslashes to forward slashes
    if [[ -n "$CFG_SOURCE_FOLDERS" ]]; then
        local folders="${CFG_SOURCE_FOLDERS//\\//}"
        shopt -s nullglob
        for dir in $folders; do
            for ext in cpp c; do
                for f in "$dir"/*."$ext"; do
                    [[ -f "$f" ]] && ALL_SOURCES="$ALL_SOURCES $f"
                done
            done
        done
        shopt -u nullglob
    fi

    ALL_SOURCES="${ALL_SOURCES# }"  # trim leading space

    if [[ -z "$ALL_SOURCES" ]]; then
        echo "[ERROR] No source files. Set SOURCE_FILES or SOURCE_FOLDERS in $CONFIG_FILE"
        exit 1
    fi
}


# ═════════════════════════════════════════════════════════════════════════════
#  Compiler Setup
# ═════════════════════════════════════════════════════════════════════════════

init_compiler() {
    local toolchain="${CFG_COMPILER:-gcc}"
    local lang="${CFG_LANGUAGE:-c++}"

    # Determine compiler binary
    if [[ -n "$CFG_COMPILER_PATH" ]]; then
        CC="$CFG_COMPILER_PATH"
    else
        case "${toolchain,,}" in
            gcc)
                [[ "$lang" == "c" ]] && CC="gcc" || CC="g++"
                ;;
            clang)
                [[ "$lang" == "c" ]] && CC="clang" || CC="clang++"
                ;;
            *)
                CC="$toolchain"
                ;;
        esac
    fi

    # Verify compiler exists
    if ! command -v "$CC" &>/dev/null; then
        echo "[ERROR] Compiler not found: $CC"
        echo "        Install it or set COMPILER_PATH in $CONFIG_FILE"
        exit 1
    fi

    # Build compile flags
    CC_FLAGS=""
    [[ -n "$CFG_GCC_OPTIMIZATION" ]]  && CC_FLAGS="$CC_FLAGS $CFG_GCC_OPTIMIZATION"
    [[ -n "$CFG_GCC_STANDARD" ]]      && CC_FLAGS="$CC_FLAGS $CFG_GCC_STANDARD"
    [[ -n "$CFG_GCC_WARNINGS" ]]      && CC_FLAGS="$CC_FLAGS $CFG_GCC_WARNINGS"
    [[ -n "$CFG_GCC_DEBUG" ]]         && CC_FLAGS="$CC_FLAGS $CFG_GCC_DEBUG"
    [[ -n "$CFG_GCC_DEFINES" ]]       && CC_FLAGS="$CC_FLAGS $CFG_GCC_DEFINES"
    [[ -n "$CFG_GCC_EXTRA" ]]         && CC_FLAGS="$CC_FLAGS $CFG_GCC_EXTRA"
    CC_FLAGS="${CC_FLAGS# }"

    # Build link flags
    LD_FLAGS=""
    [[ -n "$CFG_GCC_EXTRA_LINK" ]]    && LD_FLAGS="$CFG_GCC_EXTRA_LINK"
    LINK_LIBS=""
    [[ -n "$CFG_GCC_LIBS" ]]          && LINK_LIBS="$CFG_GCC_LIBS"

    # 32-bit cross-compilation
    if [[ "$ARCH" == "32" ]]; then
        CC_FLAGS="$CC_FLAGS -m32"
        LD_FLAGS="$LD_FLAGS -m32"
    fi

    COMPILE_DISPLAY="$CC $CC_FLAGS"
    LINK_DISPLAY="$CC $LD_FLAGS $LINK_LIBS"
}


# ═════════════════════════════════════════════════════════════════════════════
#  Hashing (md5sum)
# ═════════════════════════════════════════════════════════════════════════════

file_hash() {
    if command -v md5sum &>/dev/null; then
        md5sum "$1" 2>/dev/null | awk '{print $1}'
    elif command -v md5 &>/dev/null; then
        md5 -q "$1" 2>/dev/null
    else
        # Fallback: use file modification time
        stat -c %Y "$1" 2>/dev/null || stat -f %m "$1" 2>/dev/null
    fi
}

combined_hash() {
    local src="$1"
    local src_hash
    src_hash=$(file_hash "$src")

    # Derive header: .cpp -> .hpp, .c -> .h
    local hdr="${src%.cpp}.hpp"
    if [[ "$hdr" == "$src" ]]; then
        hdr="${src%.c}.h"
    fi

    local hdr_hash="NONE"
    if [[ -f "$hdr" ]]; then
        hdr_hash=$(file_hash "$hdr")
    fi

    echo "${src_hash}_${hdr_hash}"
}

get_stored_hash() {
    local key="$1"
    grep -F "$key" "$HASH_FILE" 2>/dev/null | awk '{print $2}'
}

update_stored_hash() {
    local key="$1"
    local hash="$2"
    grep -vF "$key" "$HASH_FILE" > "$HASH_FILE.tmp" 2>/dev/null || true
    echo "$key $hash" >> "$HASH_FILE.tmp"
    mv -f "$HASH_FILE.tmp" "$HASH_FILE"
}


# ═════════════════════════════════════════════════════════════════════════════
#  Build Operations
# ═════════════════════════════════════════════════════════════════════════════

do_compile() {
    local src="$1"
    local obj="$2"
    $CC -c $CC_FLAGS "$src" -o "$obj"
}

do_link() {
    local target="$1"
    shift
    $CC -o "$target" "$@" $LD_FLAGS $LINK_LIBS
}

check_and_compile() {
    local src="$1"
    local obj="$2"
    local toolchain="${CFG_COMPILER:-gcc}"
    local hash_key="${toolchain}:${ARCH}:${src}"
    local current_hash
    current_hash=$(combined_hash "$src")

    if [[ ! -f "$obj" ]]; then
        echo "[COMPILE] $src (object missing)"
        do_compile "$src" "$obj"
        update_stored_hash "$hash_key" "$current_hash"
        return
    fi

    local stored_hash
    stored_hash=$(get_stored_hash "$hash_key")

    if [[ "$current_hash" == "$stored_hash" ]]; then
        echo "[  OK  ] $src is up to date"
        return
    fi

    echo "[COMPILE] $src (changed)"
    do_compile "$src" "$obj"
    update_stored_hash "$hash_key" "$current_hash"
}


# ═════════════════════════════════════════════════════════════════════════════
#  Quick Up-to-date Check
# ═════════════════════════════════════════════════════════════════════════════

quick_check_all() {
    local toolchain="${CFG_COMPILER:-gcc}"

    [[ ! -f "$HASH_FILE" ]] && return 1

    local link_target="${CFG_RUN_TARGET:-output}"
    # Strip .exe extension for Linux
    link_target="${link_target%.exe}"
    [[ ! -f "$EXE_DIR/$link_target" ]] && return 1

    for src in $ALL_SOURCES; do
        local hash_key="${toolchain}:${ARCH}:${src}"
        local current_hash
        current_hash=$(combined_hash "$src")
        local stored_hash
        stored_hash=$(get_stored_hash "$hash_key")
        [[ "$current_hash" != "$stored_hash" ]] && return 1
    done
    return 0
}


# ═════════════════════════════════════════════════════════════════════════════
#  Default Config Generator
# ═════════════════════════════════════════════════════════════════════════════

generate_default_config() {
    echo "Generating default build config..."
    cat > "$CONFIG_FILE" << 'CONFIGEOF'
# ============================================================
# Build Configuration
# ============================================================
# Lines starting with # are comments. Do not add spaces around =
# Usage: ./install.sh          (64-bit)
#        ./install.sh 32bit    (32-bit)



# -- Compiler Toolchain ----------------------------------------
#  gcc    = GCC
#  clang  = Clang / LLVM
COMPILER=gcc



# -- Language --------------------------------------------------
#  c    = compile as C    (gcc)
#  c++  = compile as C++  (g++)
LANGUAGE=c++



# -- Compiler Path (optional) ----------------------------------
#  Full path to compiler executable.
#  Leave empty to auto-detect from PATH.
COMPILER_PATH=



# -- Source Files -----------------------------------------------
#  Specific .cpp/.c files to compile (space-separated)
SOURCE_FILES=



# -- Source Folders ---------------------------------------------
#  Auto-compile ALL .cpp/.c files found in these folders
#  (space-separated folder names, non-recursive)
#  Use forward slashes on Linux.
SOURCE_FOLDERS=core core/console core/input core/clock core/sound core/render core/render/tinyrenderer-master



# -- Run Target -------------------------------------------------
#  Binary to auto-run when everything is up to date (from exe folder)
#  Leave empty to just print "up to date"
RUN_TARGET=main



# -- Optimization -----------------------------------------------
#  -O0=none  -O1=some  -O2=default  -O3=aggressive
#  -Os=size  -Ofast=fast  -Og=debug-friendly
GCC_OPTIMIZATION=-O2



# -- Language Standard ------------------------------------------
#  C:   -std=c11  -std=c17  -std=c23  -std=gnu17
#  C++: -std=c++14  -std=c++17  -std=c++20  -std=c++23  -std=gnu++17
GCC_STANDARD=-std=c++17



# -- Warnings ---------------------------------------------------
#  -Wall         = common warnings
#  -Wextra       = extra warnings
#  -Wpedantic    = strict ISO compliance
#  -Werror       = treat warnings as errors
GCC_WARNINGS=-Wall -Wextra



# -- Debug Info -------------------------------------------------
#  -g   = debug symbols   -g0 = none   -g3 = max debug
GCC_DEBUG=



# -- Preprocessor Defines ---------------------------------------
#  Space-separated: -DTESTING -DBEST -DNDEBUG
GCC_DEFINES=-DTESTING -DBEST



# -- Extra Compiler Flags ---------------------------------------
#  -fno-exceptions      = disable C++ exceptions
#  -fno-rtti            = disable RTTI
#  -march=native        = optimize for current CPU
GCC_EXTRA=



# -- Linker Libraries -------------------------------------------
#  Space-separated: -lm  -lpthread  -ldl
GCC_LIBS=-lm



# -- Extra Linker Flags -----------------------------------------
#  -s                   = strip symbols (smaller binary)
#  -static              = static linking
GCC_EXTRA_LINK=
CONFIGEOF
    echo "Default config generated: $CONFIG_FILE"
}


# ═════════════════════════════════════════════════════════════════════════════
#  Main
# ═════════════════════════════════════════════════════════════════════════════

parse_config
discover_sources
init_compiler

# Determine link target (strip .exe for Linux)
LINK_TARGET="${CFG_RUN_TARGET:-output}"
LINK_TARGET="${LINK_TARGET%.exe}"

# Quick up-to-date check — skip build and run if nothing changed
if quick_check_all; then
    clear
    if [[ -n "$CFG_RUN_TARGET" ]]; then
        "./$EXE_DIR/$LINK_TARGET"
    else
        echo "All files up to date."
    fi
    exit 0
fi

echo "Starting incremental build..."

mkdir -p "$BIN_DIR"
mkdir -p "$EXE_DIR"
[[ ! -f "$HASH_FILE" ]] && touch "$HASH_FILE"

echo "[CONFIG] compile: $COMPILE_DISPLAY"
echo "[CONFIG] link:    $LINK_DISPLAY"


# ── Compilation ─────────────────────────────────────────────────────────────
ALL_OBJS=""
for src in $ALL_SOURCES; do
    # Object name: strip path, change extension to .o
    obj_name=$(basename "${src%.*}").o
    obj_path="$BIN_DIR/$obj_name"
    check_and_compile "$src" "$obj_path"
    ALL_OBJS="$ALL_OBJS $obj_path"
done


# ── Linking ─────────────────────────────────────────────────────────────────
echo "Linking..."
do_link "$EXE_DIR/$LINK_TARGET" $ALL_OBJS

echo "Build complete!"
