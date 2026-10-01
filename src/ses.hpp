// Telefondan gelen ham PCM sesi (48 kHz, 16 bit, stereo) bilgisayarda çalar.
//
// Ses kütüphaneleri çalışma anında dlopen ile SİSTEMDEN yüklenir (pakete gömülmez): önce
// PulseAudio/PipeWire (libpulse-simple), olmazsa ALSA (libasound). İkisi de yoksa ses sessizce
// atlanır; görüntü etkilenmez.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

class SesCalar {
public:
    SesCalar();
    ~SesCalar();
    SesCalar(const SesCalar&) = delete;
    SesCalar& operator=(const SesCalar&) = delete;

    // Ağ iş parçacığından çağrılır; engellemez. Kuyruk fazla dolarsa (gecikme birikmesin diye)
    // en eski veri atılır.
    void yaz(const uint8_t* veri, size_t boyut);
    // Bağlantı kopunca/yeniden kurulunca kuyruktaki eski sesi at.
    void temizle();

    void sessiz(bool kapali) { sessiz_ = kapali; }
    bool sessiz() const { return sessiz_; }

    // Kullanılan arka uç: "PulseAudio", "ALSA" veya "" (henüz açılmadı / bulunamadı).
    std::string arka_uc() const;

private:
    void calis();
    bool ac();
    bool cal(const uint8_t* veri, size_t boyut);  // engelleyerek çalar
    void kapat();

    std::mutex kilit_;
    std::condition_variable kosul_;
    std::deque<uint8_t> kuyruk_;
    std::atomic<bool> sessiz_{false};
    std::atomic<bool> bitir_{false};
    std::thread is_;

    // Arka uç durumu (yalnızca ses iş parçacığı dokunur, arka_uc_adi_ hariç)
    void* kutuphane_ = nullptr;
    void* akis_ = nullptr;
    int tur_ = 0;  // 0 yok, 1 pulse, 2 alsa
    bool denendi_ = false;
    mutable std::mutex ad_kilidi_;
    std::string arka_uc_adi_;
};
