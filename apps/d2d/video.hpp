// D2Decomp — Bink cinematics through ffmpeg (binkvideo + binkaudio_rdft).
//
// D2's videos live in d2video/d2xvideo.mpq: the Blizzard / Blizzard North
// logos (640x480), the intros and act transitions (640x292, letterboxed).
// The file streams from the MPQ through a custom AVIOContext; frames come
// out as RGBA scaled to the requested size, audio as interleaved S16
// stereo at the source rate for the caller to queue (OpenAL in d2d).
// ponytail: wall-clock pacing, no A/V drift correction; fine for Bink's
// fixed 24 fps and in-file interleaving.
#pragma once

#include <mpq.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace d2d::video {

class Player {
public:
    Player() = default;
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    ~Player() { close(); }

    // Opens a video for frames of out_w x out_h. False if ffmpeg can't.
    bool open(mpq::File file, int out_w, int out_h) {
        close();
        file_ = std::make_unique<mpq::File>(std::move(file));
        out_w_ = out_w; out_h_ = out_h;
        constexpr int kBuf = 1 << 16;
        auto* buf = static_cast<unsigned char*>(av_malloc(kBuf));
        io_ = avio_alloc_context(buf, kBuf, 0, file_.get(), &Player::read_cb, nullptr, &Player::seek_cb);
        fmt_ = avformat_alloc_context();
        fmt_->pb = io_;
        if (avformat_open_input(&fmt_, nullptr, nullptr, nullptr) < 0) { fmt_ = nullptr; close(); return false; }
        if (avformat_find_stream_info(fmt_, nullptr) < 0) { close(); return false; }
        vstream_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        astream_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (vstream_ < 0 || !(vdec_ = open_codec(vstream_))) { close(); return false; }
        if (astream_ >= 0) adec_ = open_codec(astream_);
        if (adec_) {
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            if (swr_alloc_set_opts2(&swr_, &stereo, AV_SAMPLE_FMT_S16, adec_->sample_rate,
                                    &adec_->ch_layout, adec_->sample_fmt, adec_->sample_rate, 0, nullptr) < 0
                || swr_init(swr_) < 0) {
                swr_free(&swr_);
                avcodec_free_context(&adec_);
            }
        }
        frame_ = av_frame_alloc();
        pkt_ = av_packet_alloc();
        rgba_.assign(std::size_t(out_w_) * out_h_ * 4, 0);
        return true;
    }

    [[nodiscard]] int width() const noexcept { return out_w_; }
    [[nodiscard]] int height() const noexcept { return out_h_; }
    [[nodiscard]] int sample_rate() const noexcept { return adec_ ? adec_->sample_rate : 0; }
    [[nodiscard]] const std::vector<std::uint8_t>& rgba() const noexcept { return rgba_; }
    // Decoded audio waiting to be played (interleaved S16 stereo); the
    // caller takes it.
    std::vector<std::int16_t>& audio() noexcept { return audio_; }

    // Decodes up to time t (seconds from the start): the latest frame with
    // pts <= t lands in rgba(), audio found on the way in audio(). False
    // once the file is exhausted and its last frame has been shown.
    bool advance(double seconds) {
        if (!fmt_) return false;
        while (!eof_) {
            if (have_pending_) {
                if (pending_t_ > seconds) return true;
                show_pending();
            }
            if (av_read_frame(fmt_, pkt_) < 0) { eof_ = true; break; }
            if (pkt_->stream_index == vstream_) decode(vdec_, true);
            else if (adec_ && pkt_->stream_index == astream_) decode(adec_, false);
            av_packet_unref(pkt_);
        }
        if (have_pending_ && pending_t_ <= seconds) show_pending();
        return have_pending_;
    }

private:
    AVCodecContext* open_codec(int stream) {
        const auto* par = fmt_->streams[stream]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(par->codec_id);
        if (!codec) return nullptr;
        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        if (avcodec_parameters_to_context(ctx, par) < 0 || avcodec_open2(ctx, codec, nullptr) < 0) {
            avcodec_free_context(&ctx);
            return nullptr;
        }
        return ctx;
    }

    void decode(AVCodecContext* dec, bool video) {
        if (avcodec_send_packet(dec, pkt_) < 0) return;
        while (avcodec_receive_frame(dec, frame_) == 0) {
            if (video) {
                // Keep the newest frame pending until its time comes.
                pending_t_ = frame_->best_effort_timestamp == AV_NOPTS_VALUE ? 0.0
                           : double(frame_->best_effort_timestamp) * av_q2d(fmt_->streams[vstream_]->time_base);
                sws_ = sws_getCachedContext(sws_, frame_->width, frame_->height, AVPixelFormat(frame_->format),
                                            out_w_, out_h_, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                std::uint8_t* dst[1] = { pending_.data() };
                if (pending_.size() != rgba_.size()) { pending_.resize(rgba_.size()); dst[0] = pending_.data(); }
                const int stride[1] = { out_w_ * 4 };
                sws_scale(sws_, frame_->data, frame_->linesize, 0, frame_->height, dst, stride);
                have_pending_ = true;
            } else {
                const int max = swr_get_out_samples(swr_, frame_->nb_samples);
                const auto offset = audio_.size();
                audio_.resize(offset + std::size_t(max) * 2);
                auto* out = reinterpret_cast<std::uint8_t*>(audio_.data() + offset);
                const int samples = swr_convert(swr_, &out, max, const_cast<const std::uint8_t**>(frame_->extended_data),
                                          frame_->nb_samples);
                audio_.resize(offset + std::size_t(std::max(samples, 0)) * 2);
            }
            av_frame_unref(frame_);
            if (video) return;                    // one frame at a time
        }
    }

    void show_pending() { rgba_.swap(pending_); have_pending_ = false; }

    static int read_cb(void* opaque, std::uint8_t* buf, int size) {
        const auto got = static_cast<mpq::File*>(opaque)->read(buf, std::size_t(size));
        return got ? int(got) : AVERROR_EOF;
    }
    static std::int64_t seek_cb(void* opaque, std::int64_t off, int whence) {
        auto* file = static_cast<mpq::File*>(opaque);
        if (whence & AVSEEK_SIZE) return std::int64_t(file->size());
        if ((whence & ~AVSEEK_FORCE) != SEEK_SET) return -1;
        return std::int64_t(file->seek(std::uint64_t(off)));
    }

    void close() {
        if (pkt_) av_packet_free(&pkt_);
        if (frame_) av_frame_free(&frame_);
        if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
        if (swr_) swr_free(&swr_);
        if (vdec_) avcodec_free_context(&vdec_);
        if (adec_) avcodec_free_context(&adec_);
        if (fmt_) avformat_close_input(&fmt_);
        if (io_) { av_freep(&io_->buffer); avio_context_free(&io_); }
        file_.reset();
        eof_ = have_pending_ = false;
        audio_.clear();
    }

    std::unique_ptr<mpq::File> file_;
    AVIOContext* io_ = nullptr;
    AVFormatContext* fmt_ = nullptr;
    AVCodecContext *vdec_ = nullptr, *adec_ = nullptr;
    SwsContext* sws_ = nullptr;
    SwrContext* swr_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* pkt_ = nullptr;
    int vstream_ = -1, astream_ = -1, out_w_ = 0, out_h_ = 0;
    bool eof_ = false, have_pending_ = false;
    double pending_t_ = 0;
    std::vector<std::uint8_t> rgba_, pending_;
    std::vector<std::int16_t> audio_;
};

}  // namespace d2d::video
