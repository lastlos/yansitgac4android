// Arka plan iş parçacığı: cihazı bekle → uygulamayı kur/başlat → bağlan → çöz.
// Bağlantı koparsa baştan başlar. GTK'ya dokunmaz; yalnızca geri çağrı yapar.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

class Oturum {
public:
    struct Olaylar {
        std::function<void(const std::string&)> durum;                  // kullanıcıya mesaj
        std::function<void(const uint8_t* bgra, int gen, int yuk)> kare;  // çözülmüş kare
        std::function<void(bool)> akis;                                 // akış başladı/bitti
        std::function<void(const uint8_t* pcm, size_t boyut)> ses;      // 48 kHz s16le stereo
        std::function<void(bool ses_var)> ses_durumu;                   // bağlantı başında
    };

    // dogrudan_port > 0 ise adb kullanılmaz, 127.0.0.1:port'a bağlanılır (test için).
    Oturum(Olaylar olaylar, int dogrudan_port = 0);
    ~Oturum();

    void baslat();
    void durdur();  // engellemez; iş parçacığı kısa sürede çıkar

private:
    void calis();
    bool cihazi_hazirla(std::string& seri);
    int  baglan_ve_bekle(int port, const std::string& seri, bool& uygulama_baslatildi);
    void akisi_oku(int soket);
    bool bekle(int milisaniye);  // false: durdurma istendi

    Olaylar olaylar_;
    int dogrudan_port_;
    std::atomic<bool> dur_{false};
    std::atomic<int> soket_{-1};
    std::thread is_;
};
