#!/bin/sh -e

# CMake-based alternative to autogen.sh
# This script prepares the build directory for CMake without requiring autotools

case "$0" in
    */*)
	cd $(dirname $0)
	;;
esac

case "$1" in
    --skip-submodules|-s)
	skip_submodules=yes
	;;
    "")
	;;
    *)
	echo "usage: $0 [--skip-submodules]" >&2
	exit 1
	;;
esac

# Initialize submodules (same as autogen.sh but without running autotools)
case "${skip_submodules}" in
    0|no|false|"")
        echo "Initializing git submodules..."
        git submodule update --init
        
        # For libsodium: if there's a dist-build directory with scripts we might need,
        # we don't run autogen.sh since CMake will handle the build differently
        echo "Submodules initialized (skipping autotools in submodules for CMake build)"
    ;;
esac

# Generate source file lists for CMake
# This is similar to make-mks but outputs CMake-compatible format
echo "Generating source file lists..."
./make-mks

echo ""
echo "Preparation complete. To build with CMake:"
echo ""
echo "  mkdir build && cd build"
echo "  cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20"
echo "  make -j\$(nproc)"
echo ""
echo "For more options, see cmake -LH .."
