NATIVE_BUILD=""

while [[ $# -gt 0 ]]; do
  case $1 in
    -n|--native)
      NATIVE_BUILD="1"
      ;;
  esac
  shift
done

mkdir -p ./toolchains

if test -z "$NATIVE_BUILD"; then
  # install wine
  curl -SL https://github.com/3Shain/wine/releases/download/v8.16-3shain/wine.tar.gz > ./wine.tar.gz
  mkdir -p ./toolchains/wine
  tar -zvxf ./wine.tar.gz -C ./toolchains/wine
  rm ./wine.tar.gz
  # install mingw-llvm
  curl -SL https://github.com/mstorsjo/llvm-mingw/releases/download/20231017/llvm-mingw-20231017-ucrt-macos-universal.tar.xz > ./llvm.tar.xz
  tar -zvxf ./llvm.tar.xz -C ./toolchains
  rm ./llvm.tar.xz
fi

git clone --depth 1 --branch llvmorg-15.0.7 https://github.com/llvm/llvm-project.git toolchains/llvm-project 

if test -n "$NATIVE_BUILD"; then
  mkdir -p ./toolchains/llvm-darwin-build-arm64
  cmake -B ./toolchains/llvm-darwin-build-arm64 -S ./toolchains/llvm-project/llvm \
    -DCMAKE_INSTALL_PREFIX="$(pwd)/toolchains/llvm-darwin" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DLLVM_HOST_TRIPLE=arm64-apple-darwin \
    -DLLVM_ENABLE_ASSERTIONS=On \
    -DLLVM_ENABLE_ZSTD=Off \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_TARGETS_TO_BUILD="" \
    -DLLVM_BUILD_TOOLS=Off \
    -DBUG_REPORT_URL="https://github.com/3Shain/dxmt" \
    -DPACKAGE_VENDOR="DXMT" \
    -DLLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO=Off \
    -G Ninja
  pushd ./toolchains/llvm-darwin-build-arm64
  ninja
  ninja install
  popd
else
  mkdir -p ./toolchains/llvm-build
  cmake -B ./toolchains/llvm-build -S ./toolchains/llvm-project/llvm -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_INSTALL_PREFIX="$(pwd)/toolchains/llvm" \
    -DLLVM_HOST_TRIPLE=x86_64-w64-mingw32 \
    -DLLVM_ENABLE_ASSERTIONS=On \
    -DLLVM_ENABLE_ZSTD=Off \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_TARGETS_TO_BUILD="" \
    -DLLVM_BUILD_TOOLS=Off \
    -DCMAKE_SYSROOT="$(pwd)/toolchains/llvm-mingw-20231017-ucrt-macos-universal" \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DBUG_REPORT_URL="https://github.com/3Shain/dxmt" \
    -DPACKAGE_VENDOR="DXMT" \
    -DLLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO=Off \
    -G Ninja
  pushd ./toolchains/llvm-build
  ninja
  ninja install
  popd

  mkdir -p ./toolchains/llvm-darwin-build
  cmake -B ./toolchains/llvm-darwin-build -S ./toolchains/llvm-project/llvm \
    -DCMAKE_INSTALL_PREFIX="$(pwd)/toolchains/llvm-darwin" \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 \
    -DLLVM_HOST_TRIPLE=x86_64-apple-darwin \
    -DLLVM_ENABLE_ASSERTIONS=On \
    -DLLVM_ENABLE_ZSTD=Off \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_TARGETS_TO_BUILD="" \
    -DLLVM_BUILD_TOOLS=Off \
    -DBUG_REPORT_URL="https://github.com/3Shain/dxmt" \
    -DPACKAGE_VENDOR="DXMT" \
    -DLLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO=Off \
    -G Ninja
  pushd ./toolchains/llvm-darwin-build
  ninja
  ninja install
  popd
fi

## configure
# export PATH="$PATH:$(pwd)/toolchains/llvm-mingw-20231017-ucrt-macos-universal/bin"
# meson setup --cross-file build-win64.txt --native-file build-osx.txt build
## build
# meson compile -C build