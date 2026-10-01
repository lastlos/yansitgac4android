#!/usr/bin/env bash
# PC programını derler ve GTK2 bağımlılıklarıyla birlikte <cikti>/AppDir'e koyar.
# Normalde appimage-yap.sh tarafından Debian 12 konteynerinde çağrılır.
# Kullanım: scripts/appdir-hazirla.sh <cikti_dizini>
set -euo pipefail

KOK="$(cd "$(dirname "$0")/.." && pwd)"
CIKTI="$1"
APPDIR="$CIKTI/AppDir"

# Bu ortamın glibc'siyle derlenmiş ayrı bir FFmpeg (ana makinedekiyle karışmasın).
export FFMPEG_HEDEF="$CIKTI/ffmpeg"
export FFMPEG_IS="$CIKTI/ffmpeg-kaynak"
"$KOK/scripts/ffmpeg-derle.sh"

cmake -S "$KOK" -B "$CIKTI/derleme" -DCMAKE_BUILD_TYPE=Release -DFFMPEG_DIZINI="$FFMPEG_HEDEF"
cmake --build "$CIKTI/derleme" -j"$(nproc)"

install -Dm755 "$CIKTI/derleme/yansitgac" "$APPDIR/usr/bin/yansitgac"

# GTK2 ve bağımlılıklarını kopyala (glibc, X11, fontconfig gibi sistemde olması
# beklenenler linuxdeploy'un dışlama listesiyle hariç tutulur).
# libfribidi dışlama listesinde ama asgari Debian 12'de yok (konteyner testinde görüldü) → elle ekle.
"$KOK/build/araclar/linuxdeploy" --appdir "$APPDIR" \
    -e "$APPDIR/usr/bin/yansitgac" \
    -l /usr/lib/x86_64-linux-gnu/libfribidi.so.0 \
    -d "$KOK/data/yansitgac.desktop" \
    -i "$KOK/data/yansitgac.svg"

# Gömülü kütüphaneleri RUNPATH dışındaki "yedek" dizinine taşı: AppRun, sistemde GTK2 yoksa
# LD_LIBRARY_PATH ile bunları devreye alır; varsa sistemin kütüphaneleri kullanılır (bkz. paketleme/AppRun).
mkdir -p "$APPDIR/usr/lib/yedek"
find "$APPDIR/usr/lib" -maxdepth 1 -name '*.so*' -exec mv -t "$APPDIR/usr/lib/yedek" {} +
rm -f "$APPDIR/AppRun"
install -Dm755 "$KOK/paketleme/AppRun" "$APPDIR/AppRun"
install -Dm644 "$KOK/paketleme/gtkrc"  "$APPDIR/usr/share/yansitgac/gtkrc"
: > "$APPDIR/usr/share/yansitgac/bos"
