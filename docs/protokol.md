# Akış protokolü (sürüm 2)

Telefon `127.0.0.1:27183` üzerinde dinler; PC `adb forward tcp:0 tcp:27183` ile bağlanır.
Tek yönlü (telefon → PC). Tüm sayılar big-endian. Tek seferde tek istemci; yeni bağlanan eskisini düşürür.

## Başlık (12 bayt, bağlantı başında bir kez)

| Ofset | Boyut | Alan |
|---|---|---|
| 0 | 4 | `YNSG` |
| 4 | 1 | protokol sürümü (2; PC 1 ve 2'yi kabul eder, 1 = sessiz) |
| 5 | 1 | bayraklar: bit 0 = bu oturumda ses gönderilecek (sürüm 2+) |
| 6 | 2 | genişlik |
| 8 | 2 | yükseklik |
| 10 | 2 | 0 |

`adb forward`, telefonda dinleyen yoksa bağlantıyı kabul edip hemen kapatır; bu yüzden PC başarıyı
başlığın gelmesiyle ölçer.

## Paket (16 bayt başlık + veri)

| Ofset | Boyut | Alan |
|---|---|---|
| 0 | 1 | tür: 0 = kare, 1 = ayar (SPS/PPS), 2 = anahtar kare, 3 = ses |
| 1 | 3 | 0 |
| 4 | 4 | veri boyutu (≤ 16 MiB) |
| 8 | 8 | pts (mikrosaniye) |

Görüntü verisi: H.264 Annex-B. Ayar paketi PC'de saklanır ve bir sonraki kare paketinin önüne eklenir.
Ekran dönünce telefon kodlayıcıyı yeni boyutla yeniden başlatır; yeni ayar + anahtar kare gelir,
bağlantı ve başlık değişmez.

Ses verisi (tür 3): ham PCM, 48 kHz, 16 bit işaretli little-endian, stereo (iç içe), ~20 ms'lik
parçalar. Android 10+ `AudioPlaybackCapture` ile yakalanır (RECORD_AUDIO izni; PC `install -g` ile verir).
Görüntü ve ses iki iş parçacığından aynı sokete yazılır; telefonda `PaketYazici` kilitle sıralar.

Değişiklikte: `src/protokol.hpp`, `YansitmaServisi.java`, `araclar/sahte_telefon.py` birlikte güncellenir;
uyumsuz değişiklikte sürüm baytı ve Android `versionCode` artırılır.
