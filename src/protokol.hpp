// Telefon (Yansıtgaç-Android) ile PC arasındaki akış protokolü.
// Tüm çok baytlı alanlar big-endian'dır. Ayrıntı: docs/protokol.md
#pragma once

#include <cstdint>

namespace protokol {

constexpr uint16_t TELEFON_PORTU = 27183;   // Telefonda dinlenen TCP portu (127.0.0.1)
constexpr char     SIHIR[4]      = {'Y', 'N', 'S', 'G'};
constexpr uint8_t  SURUM         = 2;   // 2: ses paketleri eklendi
constexpr uint8_t  EN_ESKI_SURUM = 1;   // sürüm 1 telefonlar sessiz çalışır

constexpr int BASLIK_BOYUTU = 12;  // "YNSG" | u8 sürüm | u8 bayraklar | u16 gen | u16 yük | u16 0
constexpr uint8_t BAYRAK_SES = 1;  // telefon bu oturumda ses gönderecek (Android 10+ ve izin var)
constexpr int PAKET_BASLIGI = 16;  // u8 tür | u8[3] 0 | u32 boyut | u64 pts_mikrosaniye

enum PaketTuru : uint8_t {
    KARE        = 0,  // H.264 Annex-B kare
    AYAR        = 1,  // SPS/PPS (codec config) — bir sonraki kareye eklenir
    ANAHTAR     = 2,  // H.264 anahtar kare (IDR)
    SES         = 3,  // ham PCM: 48 kHz, 16 bit işaretli little-endian, stereo (iç içe)
};

constexpr uint32_t AZAMI_PAKET = 16u * 1024 * 1024;  // bozuk akışa karşı üst sınır

inline uint16_t oku16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t oku32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
inline uint64_t oku64(const uint8_t* p) { return uint64_t(oku32(p)) << 32 | oku32(p + 4); }

}  // namespace protokol
