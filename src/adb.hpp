// Gömülü (veya sistemdeki) adb'yi çalıştıran ince sarmalayıcı.
#pragma once

#include <string>
#include <vector>

struct AdbSonuc {
    int kod = -1;        // çıkış kodu; -1: çalıştırılamadı
    std::string cikti;   // stdout + stderr
    bool basarili() const { return kod == 0; }
};

enum class CihazDurumu {
    YOK,            // hiç cihaz yok
    HAZIR,          // "device"
    YETKISIZ,       // "unauthorized" — telefonda onay bekleniyor
    IZIN_YOK,       // "no permissions" — USB erişim izni (udev) yok
    CEVRIMDISI,     // "offline"
};

struct Cihaz {
    std::string seri;
    CihazDurumu durum = CihazDurumu::YOK;
};

namespace adb {

// adb ve APK yollarını bulur (AppImage içi → çalıştırılabilir dosyanın yanı → sistem).
std::string adb_yolu();
std::string apk_yolu();

// adb AppImage içinden mi geliyor? (Öyleyse çıkışta sunucusu kapatılmalı; aksi halde
// sunucu arka planda kalır ve AppImage bağlama noktasını açık tutar.)
bool gomulu_mu();

AdbSonuc calistir(const std::vector<std::string>& argumanlar);
AdbSonuc calistir(const std::string& seri, const std::vector<std::string>& argumanlar);

// İlk uygun cihazı döndürür; HAZIR olan varsa onu tercih eder.
Cihaz cihaz_bul();

// Telefondaki uygulamanın versionCode'u; kurulu değilse -1.
long kurulu_surum(const std::string& seri, const std::string& paket);

}  // namespace adb
