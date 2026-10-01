#include "cozucu.hpp"

#include <cstdio>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

Cozucu::Cozucu(KareGeriCagrisi geri) : geri_(std::move(geri)) {
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::fprintf(stderr, "H.264 çözücü bulunamadı\n");
        return;
    }
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    ctx->thread_type = FF_THREAD_SLICE;  // kare iş parçacığı gecikme ekler
    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        std::fprintf(stderr, "H.264 çözücü açılamadı\n");
        avcodec_free_context(&ctx);
        return;
    }
    ctx_ = ctx;
    paket_ = av_packet_alloc();
    cerceve_ = av_frame_alloc();
}

Cozucu::~Cozucu() {
    sws_freeContext(sws_);
    av_frame_free(&cerceve_);
    av_packet_free(&paket_);
    avcodec_free_context(&ctx_);
}

void Cozucu::ayar(const uint8_t* veri, size_t boyut) {
    bekleyen_ayar_.assign(veri, veri + boyut);
}

bool Cozucu::kare(const uint8_t* veri, size_t boyut, int64_t pts) {
    if (!ctx_) return false;

    const uint8_t* gonderilecek = veri;
    size_t gonderilecek_boyut = boyut;
    if (!bekleyen_ayar_.empty()) {
        birlesik_.resize(bekleyen_ayar_.size() + boyut);
        std::memcpy(birlesik_.data(), bekleyen_ayar_.data(), bekleyen_ayar_.size());
        std::memcpy(birlesik_.data() + bekleyen_ayar_.size(), veri, boyut);
        bekleyen_ayar_.clear();
        gonderilecek = birlesik_.data();
        gonderilecek_boyut = birlesik_.size();
    }

    paket_->data = const_cast<uint8_t*>(gonderilecek);
    paket_->size = int(gonderilecek_boyut);
    paket_->pts = pts;
    int r = avcodec_send_packet(ctx_, paket_);
    av_packet_unref(paket_);
    if (r < 0 && r != AVERROR(EAGAIN)) return false;
    cikis_al();
    return true;
}

void Cozucu::cikis_al() {
    while (avcodec_receive_frame(ctx_, cerceve_) == 0) {
        const int gen = cerceve_->width, yuk = cerceve_->height;
        if (!sws_ || gen != sws_gen_ || yuk != sws_yuk_ || cerceve_->format != sws_bicim_) {
            sws_freeContext(sws_);
            sws_ = sws_getContext(gen, yuk, AVPixelFormat(cerceve_->format), gen, yuk,
                                  AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            sws_gen_ = gen;
            sws_yuk_ = yuk;
            sws_bicim_ = cerceve_->format;
            bgra_.resize(size_t(gen) * yuk * 4);
        }
        if (sws_) {
            uint8_t* hedef[4] = {bgra_.data(), nullptr, nullptr, nullptr};
            int adim[4] = {gen * 4, 0, 0, 0};
            sws_scale(sws_, cerceve_->data, cerceve_->linesize, 0, yuk, hedef, adim);
            geri_(bgra_.data(), gen, yuk);
        }
        av_frame_unref(cerceve_);
    }
}
