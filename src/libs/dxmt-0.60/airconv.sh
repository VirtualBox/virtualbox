export ROOT=$(pwd)

MTL_HUD_ENABLED=1 OBJC_DEBUG_MISSING_POOLS=NO MTL_CAPTURE_ENABLED=0 WINEPREFIX=$ROOT/wineprefix XDG_DATA_HOME=$ROOT/.local/share XDG_CONFIG_HOME=$ROOT/.config \
XDG_CACHE_HOME=$ROOT/.cache WINEDEBUG="fixme-all" WINEDLLOVERRIDES="dxgi,d3d11,d3dcompiler_47=n,b" $ROOT/toolchains/wine/bin/wine build/src/airconv/airconv.exe "$@"
