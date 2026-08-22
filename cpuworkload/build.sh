#!/usr/bin/env sh
set -eu

target="${1:-desktop}"
configuration="${2:-Release}"
root_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir="$root_dir/build/$target-$(printf '%s' "$configuration" | tr '[:upper:]' '[:lower:]')"

set -- -S "$root_dir" -B "$build_dir" \
  "-DCMAKE_BUILD_TYPE=$configuration" \
  -DCPU_AVS_STATIC_CXX_RUNTIME=ON

case "$target" in
  desktop)
    ;;
  android-arm64)
    : "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to the Android NDK root}"
    set -- "$@" \
      "-DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_static
    ;;
  harmony-arm64)
    : "${OHOS_NDK_HOME:?Set OHOS_NDK_HOME to the HarmonyOS NDK root}"
    set -- "$@" \
      "-DCMAKE_TOOLCHAIN_FILE=$OHOS_NDK_HOME/build/cmake/ohos.toolchain.cmake" \
      -DOHOS_ARCH=arm64-v8a -DOHOS_STL=c++_static
    ;;
  *)
    echo "unknown target: $target" >&2
    exit 2
    ;;
esac

cmake "$@"
cmake --build "$build_dir" --config "$configuration" --parallel
