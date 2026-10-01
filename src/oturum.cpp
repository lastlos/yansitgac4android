#include "oturum.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "adb.hpp"
#include "cozucu.hpp"
#include "protokol.hpp"

namespace {

const char* const PAKET = "com.yansitgac.android";
const char* const ETKINLIK = "com.yansitgac.android/.AnaEtkinlik";

#ifndef YANSITGAC_APK_SURUM
#define YANSITGAC_APK_SURUM 1
#endif

// Okuma zaman aşımı: durdurma bayrağını kontrol edebilmek için kısa tutulur.
constexpr int OKUMA_ZAMAN_ASIMI_MS = 300;

int soket_ac(int port) {
    int s = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0) return -1;
    sockaddr_in adres{};
    adres.sin_family = AF_INET;
    adres.sin_port = htons(uint16_t(port));
    adres.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, reinterpret_cast<sockaddr*>(&adres), sizeof(adres)) < 0) {
        close(s);
        return -1;
    }
    int bir = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &bir, sizeof(bir));
    timeval tv{0, OKUMA_ZAMAN_ASIMI_MS * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return s;
}

// APK'yı kurar. Önce -g ile dener (ses izni kurulumda verilsin, öğretmene sorulmasın).
// Xiaomi/MIUI gibi bazı telefonlar -g'yi "USB hata ayıklama (Güvenlik ayarları)" kapalıyken
// SecurityException ile reddeder; o zaman -g'siz kurar ve izni pm grant ile vermeyi dener.
// O da reddedilirse telefondaki uygulama izni ilk açılışta kendisi sorar.
AdbSonuc apk_kur(const std::string& seri, const std::string& apk, std::vector<std::string> ek) {
    std::vector<std::string> arg = {"install"};
    arg.insert(arg.end(), ek.begin(), ek.end());
    std::vector<std::string> g_ile = arg;
    g_ile.push_back("-g");
    g_ile.push_back(apk);
    AdbSonuc k = adb::calistir(seri, g_ile);
    if (k.cikti.find("INSTALL_GRANT_RUNTIME_PERMISSIONS") == std::string::npos) return k;

    arg.push_back(apk);
    k = adb::calistir(seri, arg);
    if (k.cikti.find("Success") != std::string::npos)
        adb::calistir(seri, {"shell", "pm", "grant", PAKET, "android.permission.RECORD_AUDIO"});
    return k;
}

// adb çıktısından öğretmene gösterilecek tek satır (Java yığın dökümünü gösterme).
std::string kisa_hata(const std::string& cikti) {
    for (const char* anahtar : {"Failure [", "INSTALL_FAILED", "Exception", "failed", "error"}) {
        auto i = cikti.find(anahtar);
        if (i == std::string::npos) continue;
        auto bas = cikti.rfind('\n', i);
        bas = (bas == std::string::npos) ? 0 : bas + 1;
        std::string satir = cikti.substr(bas, cikti.find('\n', i) - bas);
        if (satir.size() > 160) satir = satir.substr(0, 157) + "...";
        return satir;
    }
    return cikti.substr(0, 160);
}

}  // namespace

Oturum::Oturum(Olaylar olaylar, int dogrudan_port)
    : olaylar_(std::move(olaylar)), dogrudan_port_(dogrudan_port) {}

Oturum::~Oturum() {
    durdur();
    if (is_.joinable()) is_.join();
}

void Oturum::baslat() { is_ = std::thread(&Oturum::calis, this); }

void Oturum::durdur() {
    dur_ = true;
    int s = soket_.load();
    if (s >= 0) shutdown(s, SHUT_RDWR);
}

bool Oturum::bekle(int milisaniye) {
    for (int t = 0; t < milisaniye && !dur_; t += 50) usleep(50 * 1000);
    return !dur_;
}

void Oturum::calis() {
    while (!dur_) {
        if (dogrudan_port_ > 0) {
            olaylar_.durum("127.0.0.1:" + std::to_string(dogrudan_port_) + " adresine bağlanılıyor…");
            bool yok = true;
            int s = baglan_ve_bekle(dogrudan_port_, "", yok);
            if (s >= 0) akisi_oku(s);
            if (!bekle(1000)) break;
            continue;
        }

        std::string seri;
        if (!cihazi_hazirla(seri)) {
            if (!bekle(1000)) break;
            continue;
        }

        // Telefondaki 27183 portunu PC'de boş bir porta yönlendir.
        AdbSonuc f = adb::calistir(seri, {"forward", "tcp:0",
                                          "tcp:" + std::to_string(protokol::TELEFON_PORTU)});
        int port = f.basarili() ? std::atoi(f.cikti.c_str()) : 0;
        if (port <= 0) {
            olaylar_.durum("Port yönlendirme başarısız: " + f.cikti);
            if (!bekle(2000)) break;
            continue;
        }

        bool uygulama_baslatildi = false;
        int s = baglan_ve_bekle(port, seri, uygulama_baslatildi);
        if (s >= 0) akisi_oku(s);
        adb::calistir(seri, {"forward", "--remove", "tcp:" + std::to_string(port)});
        if (!bekle(1000)) break;
    }
}

bool Oturum::cihazi_hazirla(std::string& seri) {
    if (adb::adb_yolu().empty()) {
        olaylar_.durum("adb bulunamadı. Program paketi eksik olabilir.");
        return false;
    }
    Cihaz c = adb::cihaz_bul();
    switch (c.durum) {
        case CihazDurumu::YOK:
            olaylar_.durum("Telefon bekleniyor…\n\nTelefonu USB kabloyla bağlayın ve\n"
                           "Geliştirici seçeneklerinden \"USB hata ayıklama\"yı açın.");
            return false;
        case CihazDurumu::YETKISIZ:
            olaylar_.durum("Telefonda \"USB hata ayıklamaya izin verilsin mi?\" sorusunu onaylayın.\n"
                           "(\"Bu bilgisayara her zaman izin ver\" kutusunu işaretleyebilirsiniz.)");
            return false;
        case CihazDurumu::IZIN_YOK:
            olaylar_.durum("Bu bilgisayar telefona USB üzerinden erişemiyor.\n\n"
                           "Telefonun bildiriminden USB modunu \"Dosya aktarımı\" yapın,\n"
                           "kabloyu çıkarıp yeniden takın.");
            return false;
        case CihazDurumu::CEVRIMDISI:
            olaylar_.durum("Telefon çevrimdışı görünüyor. Kabloyu çıkarıp yeniden takın.");
            return false;
        case CihazDurumu::HAZIR:
            break;
    }
    seri = c.seri;

    long surum = adb::kurulu_surum(seri, PAKET);
    if (surum < YANSITGAC_APK_SURUM) {
        const std::string apk = adb::apk_yolu();
        if (apk.empty()) {
            olaylar_.durum("Telefon uygulaması (APK) paket içinde bulunamadı.");
            return false;
        }
        olaylar_.durum(surum < 0 ? "Telefona Yansıtgaç uygulaması kuruluyor…"
                                 : "Telefondaki Yansıtgaç uygulaması güncelleniyor…");
        AdbSonuc k = apk_kur(seri, apk, {"-r"});
        if (k.cikti.find("INSTALL_FAILED_UPDATE_INCOMPATIBLE") != std::string::npos) {
            // Farklı imzalı eski sürüm: kaldırıp yeniden kur.
            adb::calistir(seri, {"uninstall", PAKET});
            k = apk_kur(seri, apk, {});
        }
        if (!k.basarili() || k.cikti.find("Success") == std::string::npos) {
            olaylar_.durum("Uygulama telefona kurulamadı.\n\n"
                           "Xiaomi, Redmi ve POCO telefonlarda Geliştirici seçeneklerinden\n"
                           "\"USB üzerinden yükle\" açılmalıdır.\n\n(" + kisa_hata(k.cikti) + ")");
            bekle(3000);
            return false;
        }
    }
    return true;
}

int Oturum::baglan_ve_bekle(int port, const std::string& seri, bool& uygulama_baslatildi) {
    using saat = std::chrono::steady_clock;
    auto baslangic = saat::now();

    while (!dur_) {
        int s = soket_ac(port);
        if (s >= 0) {
            // adb forward, telefonda dinleyen yoksa bağlantıyı kabul edip hemen kapatır;
            // bu yüzden başarı ölçütü başlığın gelmesidir.
            uint8_t baslik[protokol::BASLIK_BOYUTU];
            size_t alinan = 0;
            auto son = saat::now() + std::chrono::milliseconds(1500);
            while (alinan < sizeof(baslik) && saat::now() < son && !dur_) {
                ssize_t n = recv(s, baslik + alinan, sizeof(baslik) - alinan, 0);
                if (n > 0) alinan += size_t(n);
                else if (n == 0) break;
                else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) break;
            }
            if (alinan == sizeof(baslik) && std::memcmp(baslik, protokol::SIHIR, 4) == 0) {
                if (baslik[4] < protokol::EN_ESKI_SURUM || baslik[4] > protokol::SURUM) {
                    olaylar_.durum("Telefondaki uygulama sürümü uyumsuz.");
                    close(s);
                    return -1;
                }
                if (olaylar_.ses_durumu)
                    olaylar_.ses_durumu(baslik[4] >= 2 && (baslik[5] & protokol::BAYRAK_SES));
                return s;
            }
            close(s);
        }

        if (dogrudan_port_ > 0) {
            if (!bekle(500)) break;
            continue;
        }

        if (!uygulama_baslatildi) {
            adb::calistir(seri, {"shell", "am", "start", "-n", ETKINLIK});
            uygulama_baslatildi = true;
            olaylar_.durum("Telefonda ekran paylaşımı onayı bekleniyor…\n\n"
                           "Telefonda çıkan pencerede \"Şimdi başlat\"a dokunun.");
        }
        if (saat::now() - baslangic > std::chrono::seconds(90)) {
            olaylar_.durum("Telefondan yanıt gelmedi, yeniden deneniyor…");
            return -1;
        }
        if (adb::cihaz_bul().durum != CihazDurumu::HAZIR) return -1;
        if (!bekle(700)) break;
    }
    return -1;
}

void Oturum::akisi_oku(int s) {
    soket_ = s;
    olaylar_.durum("Bağlandı.");
    olaylar_.akis(true);

    Cozucu cozucu(olaylar_.kare);
    std::vector<uint8_t> veri;

    // Tam olarak n bayt okur; false: bağlantı koptu veya durduruldu.
    auto oku = [&](uint8_t* hedef, size_t n) {
        size_t alinan = 0;
        while (alinan < n) {
            if (dur_) return false;
            ssize_t r = recv(s, hedef + alinan, n - alinan, 0);
            if (r > 0) alinan += size_t(r);
            else if (r == 0) return false;
            else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return false;
        }
        return true;
    };

    uint8_t pb[protokol::PAKET_BASLIGI];
    while (oku(pb, sizeof(pb))) {
        const uint8_t tur = pb[0];
        const uint32_t boyut = protokol::oku32(pb + 4);
        const int64_t pts = int64_t(protokol::oku64(pb + 8));
        if (boyut > protokol::AZAMI_PAKET) {
            olaylar_.durum("Bozuk veri alındı, yeniden bağlanılıyor…");
            break;
        }
        veri.resize(boyut);
        if (!oku(veri.data(), boyut)) break;

        if (tur == protokol::AYAR) cozucu.ayar(veri.data(), boyut);
        else if (tur == protokol::SES) { if (olaylar_.ses) olaylar_.ses(veri.data(), boyut); }
        else cozucu.kare(veri.data(), boyut, pts);
    }

    soket_ = -1;
    close(s);
    olaylar_.akis(false);
    if (!dur_) olaylar_.durum("Bağlantı koptu. Yeniden bağlanılıyor…");
}
