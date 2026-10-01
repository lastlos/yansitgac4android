// H.264 → BGRA çözücü (statik, minimal FFmpeg üzerinde).
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwsContext;

class Cozucu {
public:
    // Çözülen her kare için çağrılır: BGRA (cairo RGB24 ile uyumlu), satır adımı = gen*4
    using KareGeriCagrisi = std::function<void(const uint8_t* bgra, int gen, int yuk)>;

    explicit Cozucu(KareGeriCagrisi geri);
    ~Cozucu();
    Cozucu(const Cozucu&) = delete;
    Cozucu& operator=(const Cozucu&) = delete;

    bool hazir() const { return ctx_ != nullptr; }

    // SPS/PPS saklanır, bir sonraki kareye eklenir.
    void ayar(const uint8_t* veri, size_t boyut);
    // false: çözme hatası (akış yine de devam edebilir)
    bool kare(const uint8_t* veri, size_t boyut, int64_t pts);

private:
    void cikis_al();

    KareGeriCagrisi geri_;
    AVCodecContext* ctx_ = nullptr;
    AVPacket* paket_ = nullptr;
    AVFrame* cerceve_ = nullptr;
    SwsContext* sws_ = nullptr;
    int sws_gen_ = 0, sws_yuk_ = 0, sws_bicim_ = -1;
    std::vector<uint8_t> bekleyen_ayar_;
    std::vector<uint8_t> birlesik_;
    std::vector<uint8_t> bgra_;
};
