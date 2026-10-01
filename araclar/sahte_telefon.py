#!/usr/bin/env python3
"""Telefonsuz test: Yansıtgaç protokolüyle H.264 test görüntüsü yayınlar.

Kullanım:
    python3 araclar/sahte_telefon.py [port] [--dondur SANIYE] [--ses]   (varsayılan 27183)
    ./build/yansitgac --dogrudan 27183

ffmpeg CLI ile testsrc2 üretir, Annex-B NAL'larını erişim birimlerine bölüp
telefonun gönderdiği biçimde (SPS/PPS → AYAR, IDR → ANAHTAR, diğerleri → KARE) yollar.
"""
import socket
import struct
import math
import subprocess
import sys
import threading
import time

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
PORT = int(ARGS[0]) if ARGS else 27183
# --dondur N: her N saniyede dikey ↔ yatay geçiş (tablet döndürme benzetimi)
DONDUR = float(sys.argv[sys.argv.index("--dondur") + 1]) if "--dondur" in sys.argv else 0
# --ses: protokol 2 + ses bayrağı, 440 Hz ton (48 kHz s16le stereo, 20 ms'lik paketler)
SES = "--ses" in sys.argv
GEN, YUK, FPS = 720, 1280, 30
KARE, AYAR, ANAHTAR, SES_PAKETI = 0, 1, 2, 3
yazma_kilidi = threading.Lock()  # görüntü ve ses aynı soketi paylaşır (telefondaki gibi)


def gonder(baglanti, tur, veri, pts):
    with yazma_kilidi:
        baglanti.sendall(struct.pack(">B3xIQ", tur, len(veri), pts) + veri)


def ses_yayinla(baglanti, dur):
    hiz, parca = 48000, 960  # 20 ms
    ornek = 0
    t0 = time.monotonic()
    while not dur.is_set():
        veri = bytearray()
        for _ in range(parca):
            d = int(math.sin(2 * math.pi * 440 * ornek / hiz) * 6000)
            veri += struct.pack("<hh", d, d)
            ornek += 1
        try:
            gonder(baglanti, SES_PAKETI, bytes(veri), int((time.monotonic() - t0) * 1e6))
        except OSError:
            return
        # gerçek zamanlı hızda gönder
        bekle = t0 + ornek / hiz - time.monotonic()
        if bekle > 0:
            time.sleep(bekle)


def nal_bol(akis):
    """Annex-B baytlarından (nal_turu, nal_baytlari_baslangic_koduyla) üretir."""
    tampon = b""
    while True:
        parca = akis.read1(65536)
        if not parca:
            break
        tampon += parca
        while True:
            bas = tampon.find(b"\x00\x00\x01", 4)  # mevcut başlangıç kodunu atla
            if bas < 0:
                break
            son = bas - 1 if tampon[bas - 1] == 0 else bas  # 4 baytlık başlangıç kodu
            nal, tampon = tampon[:son], tampon[son:]
            if len(nal) > 4:
                yield nal


def nal_turu(nal):
    i = nal.find(b"\x00\x00\x01") + 3
    return nal[i] & 0x1F


def yayinla(baglanti):
    baglanti.sendall(b"YNSG" + struct.pack(">BBHHH", 2 if SES else 1, 1 if SES else 0, GEN, YUK, 0))
    if SES:
        dur = threading.Event()
        threading.Thread(target=ses_yayinla, args=(baglanti, dur), daemon=True).start()
    t0 = time.monotonic()
    gen, yuk = GEN, YUK
    while True:
        # Telefon gibi: dönüşte yeni kodlayıcı → yeni SPS/PPS + anahtar kare, bağlantı aynı.
        parca(baglanti, gen, yuk, t0, DONDUR or None)
        gen, yuk = yuk, gen
        print(f"Döndürüldü: {gen}x{yuk}")


def parca(baglanti, gen, yuk, t0, sure):
    ffmpeg = subprocess.Popen(
        ["ffmpeg", "-loglevel", "error", "-re", "-f", "lavfi",
         "-i", f"testsrc2=size={gen}x{yuk}:rate={FPS}",
         "-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
         "-g", "60", "-bf", "0", "-threads", "1", "-f", "h264", "-"],
        stdout=subprocess.PIPE)
    ayar = b""
    bitis = time.monotonic() + sure if sure else None
    try:
        for nal in nal_bol(ffmpeg.stdout):
            tur = nal_turu(nal)
            if tur in (7, 8):  # SPS/PPS
                ayar += nal
                continue
            if tur == 6:  # SEI: sonraki kareye eklenmesine gerek yok
                continue
            if ayar:
                gonder(baglanti, AYAR, ayar, 0)
                ayar = b""
            pts = int((time.monotonic() - t0) * 1e6)
            pt = ANAHTAR if tur == 5 else KARE
            gonder(baglanti, pt, nal, pts)
            if bitis and time.monotonic() > bitis:
                return
    finally:
        ffmpeg.kill()


def main():
    sunucu = socket.socket()
    sunucu.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sunucu.bind(("127.0.0.1", PORT))
    sunucu.listen(1)
    print(f"Sahte telefon 127.0.0.1:{PORT} üzerinde bekliyor")
    while True:
        baglanti, _ = sunucu.accept()
        print("İstemci bağlandı")
        try:
            yayinla(baglanti)
        except (BrokenPipeError, ConnectionResetError):
            print("İstemci ayrıldı")
        finally:
            baglanti.close()


if __name__ == "__main__":
    main()
