#!/usr/bin/env bash
# Yansıtgaç AppImage'ını üretir: PC programı + GTK2 kütüphaneleri + adb + telefon APK'sı.
# Root gerektirmez (rootless podman kullanır).
#
# Kullanım: scripts/appimage-yap.sh [pardus23|etap21|hepsi]   (varsayılan: pardus23)
#   pardus23 → dist/Yansitgac-x86_64.AppImage         Debian 12, glibc 2.36: Pardus 23 / ETAP 23 ve sonrası
#   etap21   → dist/Yansitgac-ETAP21-x86_64.AppImage  Debian 11, glibc 2.31: Pardus 21 / ETAP 21 ve sonrası
#
# C++ tarafı ve gömülen kütüphaneler hedefin Debian sürümünde derlenir/toplanır (paketleme/Containerfile).
# Konteynersiz (yalnızca bu makinede çalışacak) paket için: YERELDE=1 ./scripts/appimage-yap.sh
set -euo pipefail

KOK="$(cd "$(dirname "$0")/.." && pwd)"
ARACLAR="$KOK/build/araclar"
ANDROID_SDK="${ANDROID_HOME:-$HOME/Android/Sdk}"
export APPIMAGE_EXTRACT_AND_RUN=1   # paketleme araçları FUSE olmadan da çalışsın

HEDEF="${1:-pardus23}"
if [[ "$HEDEF" == hepsi ]]; then
    "$0" pardus23
    "$0" etap21
    exit 0
fi
case "$HEDEF" in
    pardus23) TABAN=docker.io/library/debian:12; GLIBC=2.36; AD=Yansitgac-x86_64.AppImage ;;
    etap21)   TABAN=docker.io/library/debian:11; GLIBC=2.31; AD=Yansitgac-ETAP21-x86_64.AppImage ;;
    *) echo "Bilinmeyen hedef: $HEDEF (pardus23 | etap21 | hepsi)" >&2; exit 2 ;;
esac
CIKTI="$KOK/build-$HEDEF"            # her hedefin ayrı derleme dizini (yerel build/ ile de karışmasın)
APPDIR="$CIKTI/AppDir"
IMAJ="yansitgac-derleyici-$HEDEF"
echo "== Hedef: $HEDEF ($TABAN, glibc ≤ $GLIBC) → dist/$AD"

mkdir -p "$ARACLAR" "$KOK/dist" "$CIKTI"

indir() {  # indir <url> <hedef>
    [[ -f "$2" ]] || { echo "İndiriliyor: $1"; curl -fL -o "$2" "$1"; chmod +x "$2"; }
}
indir https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
      "$ARACLAR/linuxdeploy"
indir https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage \
      "$ARACLAR/appimagetool"

# 1) Telefon uygulaması (Java; glibc'den bağımsız, ana makinede derlenir)
( cd "$KOK/Yansıtgaç-Android" && ./gradlew assembleRelease --console=plain -q )
APK="$KOK/Yansıtgaç-Android/app/build/outputs/apk/release/app-release.apk"

# 2) PC programı + GTK2 kütüphaneleri → AppDir
rm -rf "$APPDIR"
if [[ "${YERELDE:-0}" == 1 ]]; then
    "$KOK/scripts/appdir-hazirla.sh" "$CIKTI"
else
    podman build -q --build-arg TABAN="$TABAN" -t "$IMAJ" \
        -f "$KOK/paketleme/Containerfile" "$KOK/paketleme" >/dev/null
    podman run --rm -v "$KOK:/kaynak:Z" -w /kaynak -e APPIMAGE_EXTRACT_AND_RUN=1 "$IMAJ" \
        ./scripts/appdir-hazirla.sh "/kaynak/build-$HEDEF"
fi

# 3) adb (glibc 2.16 yeterli): yerel SDK'dan, yoksa Google'dan platform-tools
ADB="$ANDROID_SDK/platform-tools/adb"
if [[ ! -x "$ADB" ]]; then
    indir https://dl.google.com/android/repository/platform-tools-latest-linux.zip \
          "$ARACLAR/platform-tools.zip"
    (cd "$ARACLAR" && unzip -oq platform-tools.zip)
    ADB="$ARACLAR/platform-tools/adb"
fi
install -Dm755 "$ADB" "$APPDIR/usr/bin/adb"
install -Dm644 "$APK" "$APPDIR/usr/share/yansitgac/yansitgac.apk"
install -Dm644 "$(dirname "$ADB")/NOTICE.txt" "$APPDIR/usr/share/yansitgac/adb-NOTICE.txt" 2>/dev/null || true

# 4) Uyumluluk denetimi: hiçbir dosya hedefin glibc'sinden yenisini istememeli.
EN_YENI=$(find "$APPDIR/usr" -type f \( -name '*.so*' -o -perm -u+x \) -exec objdump -T {} + 2>/dev/null \
          | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
echo "Paketin istediği en yeni glibc: $EN_YENI (sınır GLIBC_$GLIBC)"
if [[ "${YERELDE:-0}" != 1 ]] && \
   [[ "$(printf '%s\nGLIBC_%s\n' "$EN_YENI" "$GLIBC" | sort -V | tail -1)" != "GLIBC_$GLIBC" ]]; then
    echo "HATA: $EN_YENI, $HEDEF (glibc $GLIBC) ile uyumsuz" >&2
    exit 1
fi

# 5) Paketle (yeni statik runtime: hedef makinede libfuse2 gerekmez)
# Önce geçici dosyaya yaz, sonra yerine taşı: eski paket o an çalışıyorsa üzerine yazılamaz
# ("Text file busy"); yeniden adlandırma ise çalışan kopyayı bozmadan yenisini koyar.
ARCH=x86_64 "$ARACLAR/appimagetool" --no-appstream "$APPDIR" "$KOK/dist/.$AD.yeni"
mv -f "$KOK/dist/.$AD.yeni" "$KOK/dist/$AD"
echo "Hazır: $KOK/dist/$AD"
