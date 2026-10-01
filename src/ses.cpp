#include "ses.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

constexpr int HIZ = 48000;
constexpr int KANAL = 2;
constexpr size_t KARE_BAYT = KANAL * 2;                       // 16 bit stereo
constexpr size_t MS_BAYT = HIZ / 1000 * KARE_BAYT;            // 1 ms = 192 bayt
constexpr size_t PARCA = 20 * MS_BAYT;                        // tek seferde çalınan
constexpr size_t AZAMI_KUYRUK = 150 * MS_BAYT;                // bunu aşarsa gecikme birikiyor
constexpr size_t HEDEF_KUYRUK = 60 * MS_BAYT;                 // ...ve buna kırpılır

// --- libpulse-simple (yalnızca kullandığımız kısım; ABI kararlı) ---
struct pa_sample_spec {
    int format;
    uint32_t rate;
    uint8_t channels;
};
struct pa_buffer_attr {
    uint32_t maxlength, tlength, prebuf, minreq, fragsize;
};
constexpr int PA_SAMPLE_S16LE = 3;
constexpr int PA_STREAM_PLAYBACK = 1;
using pa_simple_new_t = void* (*)(const char*, const char*, int, const char*, const char*,
                                  const pa_sample_spec*, const void*, const pa_buffer_attr*, int*);
using pa_simple_write_t = int (*)(void*, const void*, size_t, int*);
using pa_simple_free_t = void (*)(void*);

// --- libasound ---
constexpr int SND_PCM_STREAM_PLAYBACK = 0;
constexpr int SND_PCM_FORMAT_S16_LE = 2;
constexpr int SND_PCM_ACCESS_RW_INTERLEAVED = 3;
using snd_pcm_open_t = int (*)(void**, const char*, int, int);
using snd_pcm_set_params_t = int (*)(void*, int, int, unsigned, unsigned, int, unsigned);
using snd_pcm_writei_t = long (*)(void*, const void*, unsigned long);
using snd_pcm_recover_t = int (*)(void*, int, int);
using snd_pcm_close_t = int (*)(void*);

template <typename T>
T sembol(void* kutuphane, const char* ad) {
    return reinterpret_cast<T>(dlsym(kutuphane, ad));
}

}  // namespace

// İş parçacığı gövdede başlar: tüm üyeler ilklendikten sonra.
SesCalar::SesCalar() { is_ = std::thread(&SesCalar::calis, this); }

SesCalar::~SesCalar() {
    bitir_ = true;
    kosul_.notify_all();
    if (is_.joinable()) is_.join();
    kapat();
}

void SesCalar::yaz(const uint8_t* veri, size_t boyut) {
    {
        std::lock_guard<std::mutex> k(kilit_);
        kuyruk_.insert(kuyruk_.end(), veri, veri + boyut);
        // Telefon ve bilgisayar saatleri tam aynı hızda değil; fark birikip gecikme büyümesin.
        if (kuyruk_.size() > AZAMI_KUYRUK) {
            size_t at = kuyruk_.size() - HEDEF_KUYRUK;
            at -= at % KARE_BAYT;  // örnek sınırını koru
            kuyruk_.erase(kuyruk_.begin(), kuyruk_.begin() + long(at));
        }
    }
    kosul_.notify_one();
}

void SesCalar::temizle() {
    std::lock_guard<std::mutex> k(kilit_);
    kuyruk_.clear();
}

std::string SesCalar::arka_uc() const {
    std::lock_guard<std::mutex> k(ad_kilidi_);
    return arka_uc_adi_;
}

void SesCalar::calis() {
    std::vector<uint8_t> parca;
    while (!bitir_) {
        {
            std::unique_lock<std::mutex> k(kilit_);
            kosul_.wait(k, [&] { return bitir_ || !kuyruk_.empty(); });
            if (bitir_) break;
            size_t n = std::min(kuyruk_.size(), PARCA);
            n -= n % KARE_BAYT;
            if (n == 0) continue;
            parca.assign(kuyruk_.begin(), kuyruk_.begin() + long(n));
            kuyruk_.erase(kuyruk_.begin(), kuyruk_.begin() + long(n));
        }
        if (!denendi_) {
            denendi_ = true;
            ac();
        }
        if (tur_ == 0) continue;  // ses aygıtı yok: veriyi tüket ve at
        if (sessiz_) std::fill(parca.begin(), parca.end(), 0);  // zamanlama bozulmasın
        if (!cal(parca.data(), parca.size())) {
            // Ses sunucusu yeniden başlatıldı vb.: bir sonraki veride yeniden aç.
            kapat();
            denendi_ = false;
        }
    }
}

bool SesCalar::ac() {
    // 1) PulseAudio / PipeWire (pipewire-pulse aynı API'yi sunar)
    if (void* l = dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL)) {
        auto yeni = sembol<pa_simple_new_t>(l, "pa_simple_new");
        if (yeni && sembol<pa_simple_write_t>(l, "pa_simple_write") &&
            sembol<pa_simple_free_t>(l, "pa_simple_free")) {
            pa_sample_spec ozellik{PA_SAMPLE_S16LE, HIZ, KANAL};
            const uint32_t yok = uint32_t(-1);
            pa_buffer_attr tampon{yok, uint32_t(60 * MS_BAYT), yok, yok, yok};  // ~60 ms gecikme
            int hata = 0;
            void* a = yeni(nullptr, "Yansıtgaç", PA_STREAM_PLAYBACK, nullptr, "Telefon sesi",
                           &ozellik, nullptr, &tampon, &hata);
            if (a) {
                kutuphane_ = l;
                akis_ = a;
                tur_ = 1;
                std::lock_guard<std::mutex> k(ad_kilidi_);
                arka_uc_adi_ = "PulseAudio";
                return true;
            }
            std::fprintf(stderr, "PulseAudio açılamadı (hata %d), ALSA deneniyor\n", hata);
        }
        dlclose(l);
    }
    // 2) ALSA
    if (void* l = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL)) {
        auto ac_ = sembol<snd_pcm_open_t>(l, "snd_pcm_open");
        auto ayarla = sembol<snd_pcm_set_params_t>(l, "snd_pcm_set_params");
        auto kapa = sembol<snd_pcm_close_t>(l, "snd_pcm_close");
        void* pcm = nullptr;
        if (ac_ && ayarla && kapa && ac_(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) == 0) {
            if (ayarla(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, KANAL, HIZ, 1,
                       100000) == 0) {
                kutuphane_ = l;
                akis_ = pcm;
                tur_ = 2;
                std::lock_guard<std::mutex> k(ad_kilidi_);
                arka_uc_adi_ = "ALSA";
                return true;
            }
            kapa(pcm);
        }
        dlclose(l);
    }
    std::fprintf(stderr, "Ses aygıtı bulunamadı; ses çalınmayacak\n");
    return false;
}

bool SesCalar::cal(const uint8_t* veri, size_t boyut) {
    if (tur_ == 1) {
        int hata = 0;
        return sembol<pa_simple_write_t>(kutuphane_, "pa_simple_write")(akis_, veri, boyut, &hata) == 0;
    }
    if (tur_ == 2) {
        auto yaz = sembol<snd_pcm_writei_t>(kutuphane_, "snd_pcm_writei");
        auto kurtar = sembol<snd_pcm_recover_t>(kutuphane_, "snd_pcm_recover");
        unsigned long kalan = boyut / KARE_BAYT;
        while (kalan > 0) {
            long n = yaz(akis_, veri, kalan);
            if (n < 0) {
                if (kurtar(akis_, int(n), 1) < 0) return false;  // ör. alt taşma → kurtarılır
                continue;
            }
            veri += size_t(n) * KARE_BAYT;
            kalan -= (unsigned long)n;
        }
        return true;
    }
    return false;
}

void SesCalar::kapat() {
    if (tur_ == 1) sembol<pa_simple_free_t>(kutuphane_, "pa_simple_free")(akis_);
    if (tur_ == 2) sembol<snd_pcm_close_t>(kutuphane_, "snd_pcm_close")(akis_);
    if (kutuphane_) dlclose(kutuphane_);
    kutuphane_ = akis_ = nullptr;
    tur_ = 0;
    std::lock_guard<std::mutex> k(ad_kilidi_);
    arka_uc_adi_.clear();
}
