#!/usr/bin/env bash
# Yalnızca H.264 çözücü + swscale içeren küçük, statik bir FFmpeg derler.
# Çıktı: $FFMPEG_HEDEF (varsayılan third_party/ffmpeg)/{include,lib}. Root gerektirmez.
set -euo pipefail

SURUM="7.1.2"
KOK="$(cd "$(dirname "$0")/.." && pwd)"
HEDEF="${FFMPEG_HEDEF:-$KOK/third_party/ffmpeg}"
IS="${FFMPEG_IS:-$KOK/build/ffmpeg-kaynak}"

if [[ -f "$HEDEF/lib/libavcodec.a" ]]; then
    echo "FFmpeg zaten derlenmiş: $HEDEF"
    exit 0
fi

mkdir -p "$IS"
cd "$IS"
if [[ ! -d "ffmpeg-$SURUM" ]]; then
    curl -fL -o "ffmpeg-$SURUM.tar.xz" "https://ffmpeg.org/releases/ffmpeg-$SURUM.tar.xz"
    tar xf "ffmpeg-$SURUM.tar.xz"
fi
cd "ffmpeg-$SURUM"
make distclean >/dev/null 2>&1 || true

./configure \
    --prefix="$HEDEF" \
    --enable-static --disable-shared --enable-pic \
    --disable-programs --disable-doc --disable-network --disable-autodetect \
    --disable-everything \
    --disable-avdevice --disable-avformat --disable-avfilter \
    --disable-swresample --disable-postproc \
    --enable-avcodec --enable-swscale \
    --enable-decoder=h264 --enable-parser=h264

make -j"$(nproc)"
make install
echo "FFmpeg hazır: $HEDEF"
