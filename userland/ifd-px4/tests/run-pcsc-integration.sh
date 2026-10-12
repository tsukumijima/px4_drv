#!/bin/sh
set -eu
ulimit -c 0

# ホストの既存 pcscd に接続しないよう、専用コンテナ以外での実行を拒否する。
test "${PX4_ISOLATED_TEST:-}" = 1
test -f /.dockerenv
test ! -e /run/pcscd/pcscd.comm
for process in /proc/[0-9]*; do
    test -r "$process/comm" || continue
    read -r name < "$process/comm" || continue
    if test "$name" = pcscd; then
        echo "An existing pcscd is running; use a fresh test container." >&2
        exit 1
    fi
done
test ! -e /dev/bus/usb
test ! -e /dev/px4card-test0
build_dir=$(cd "${BUILD:-build}" && pwd)
# pcscd は設定の読み込み時に DEVICENAME の存在を検査する。I/O は模擬 IFD が処理する。
: > /dev/px4card-test0
stage=$(mktemp -d /tmp/px4-pcsc-test.XXXXXX)
pcsc_pid=
finish() {
    if test -n "$pcsc_pid"; then
        kill "$pcsc_pid" 2>/dev/null || true
        wait "$pcsc_pid" 2>/dev/null || true
    fi
    rm -rf "$stage"
    rm -f /dev/px4card-test0
}
trap finish EXIT HUP INT TERM
mkdir -p "$stage/readers" /run/pcscd
cat > "$stage/readers/px4.conf" <<EOF
FRIENDLYNAME "PX4 isolated test"
DEVICENAME /dev/px4card-test0
LIBPATH $build_dir/libifd-px4-test.so
CHANNELID 0
EOF
set -- --foreground --config "$stage/readers"
# Polkit 有効の pcscd は、D-Bus のない隔離コンテナで試験クライアントの接続を拒否する。
if pcscd --help | grep -q -- --disable-polkit; then
    set -- "$@" --disable-polkit
fi
pcscd "$@" > "$stage/pcscd.log" 2>&1 &
pcsc_pid=$!
if ! "$build_dir/pcsc_integration_test"; then
    cat "$stage/pcscd.log" >&2
    exit 1
fi
