// Sound output (OpenAL): voice, music, ambience and UI channels.
#pragma once

#include "frontend.hpp"

namespace {

// Sound output: OpenAL (openal-soft, as ../thirdeye), one source + buffer
// per channel: the NPC voice, the level's song, its ambience and UI
// clicks. Sounds.txt `Loop` entries play with AL_LOOPING. WAVs are decoded
// with SDL (PCM/ADPCM) to 8/16-bit PCM. Files: music (`Music Vol` 1)
// under data\global\music, the rest under data\global\sfx, speech under
// data\local\sfx. Headless runs use openal-soft's null backend.
// ponytail: channels are head-relative (no 3D positions yet), no fades.
struct Audio {
    struct Decoded { ALenum format = 0; ALsizei freq = 0; std::vector<Uint8> pcm; };
    struct Channel {
        ALuint src = 0, buf = 0;
        int sound = 0;                           // Sounds.txt index, 0 = silent, -1 = a fixed file
    };
    bool ok = false;
    ALCdevice* dev = nullptr;
    ALCcontext* ctx = nullptr;
    Channel voice, music, ambience, ui;
    int voice_sound() const { return voice.sound; }
    // Songs are ~20 MB WAVs (240 ms to read): decoded on a worker with its
    // own MPQ handles (StormLib handles aren't shared across threads).
    std::future<std::optional<Decoded>> music_job;
    int music_job_sound = 0;
    float music_job_gain = 1.f;
    std::unordered_map<std::string, std::vector<std::byte>> file_cache;

    void init() {
        dev = alcOpenDevice(nullptr);
        if (dev) ctx = alcCreateContext(dev, nullptr);
        ok = ctx && alcMakeContextCurrent(ctx);
        d2d::log::info("Initializing Sound:");
        if (!ok) { d2d::log::warn("  no OpenAL device — running silent"); return; }
        d2d::log::info("  Device: {}", alcGetString(dev, ALC_DEVICE_SPECIFIER));
        d2d::log::info("  OpenAL {} ({}, {})", alGetString(AL_VERSION), alGetString(AL_RENDERER), alGetString(AL_VENDOR));
    }
    ~Audio() {
        video_stop();
        for (auto* c : { &voice, &music, &ambience, &ui }) stop(*c);
        if (music_job.valid()) music_job.wait();
        alcMakeContextCurrent(nullptr);
        if (ctx) alcDestroyContext(ctx);
        if (dev) alcCloseDevice(dev);
    }
    static std::optional<Decoded> decode(std::span<const std::byte> wav) {
        SDL_AudioSpec spec{};
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(wav.data(), wav.size()), true, &spec, &buf, &len)) return std::nullopt;
        Decoded d;
        d.freq = spec.freq;
        if (spec.format == SDL_AUDIO_U8 || spec.format == SDL_AUDIO_S16LE) {
            d.pcm.assign(buf, buf + len);
        } else {                                 // anything else -> S16
            SDL_AudioSpec to{ SDL_AUDIO_S16LE, spec.channels, spec.freq };
            Uint8* out = nullptr;
            int out_len = 0;
            if (!SDL_ConvertAudioSamples(&spec, buf, int(len), &to, &out, &out_len)) { SDL_free(buf); return std::nullopt; }
            d.pcm.assign(out, out + out_len);
            SDL_free(out);
            spec.format = SDL_AUDIO_S16LE;
        }
        SDL_free(buf);
        const bool eight = spec.format == SDL_AUDIO_U8;
        if (spec.channels == 1) d.format = eight ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
        else if (spec.channels == 2) d.format = eight ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
        else return std::nullopt;
        return d;
    }
    void stop(Channel& c) {
        if (c.src) { alSourceStop(c.src); alDeleteSources(1, &c.src); }
        if (c.buf) alDeleteBuffers(1, &c.buf);
        c = {};
    }
    void stop_voice() { stop(voice); }
    bool start(Channel& c, const Decoded& d, float gain, bool loop, int index) {
        stop(c);
        if (!ok) return false;
        alGenBuffers(1, &c.buf);
        alBufferData(c.buf, d.format, d.pcm.data(), ALsizei(d.pcm.size()), d.freq);
        alGenSources(1, &c.src);
        alSourcei(c.src, AL_BUFFER, ALint(c.buf));
        alSourcef(c.src, AL_GAIN, gain);
        alSourcei(c.src, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
        alSourcei(c.src, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcePlay(c.src);
        if (alGetError() != AL_NO_ERROR) { stop(c); return false; }
        c.sound = index;
        return true;
    }
    bool start(Channel& c, std::span<const std::byte> wav, float gain, bool loop, int index) {
        const auto d = decode(wav);
        if (!d) { d2d::log::warn("sound {}: {}", index, SDL_GetError()); return false; }
        return start(c, *d, gain, loop, index);
    }
    void play(Channel& c, const Scene& s, int index) {
        stop(c);
        if (!ok || index <= 0 || std::size_t(index) >= s.sounds.size()) return;
        const auto& snd = s.sounds[std::size_t(index)];
        std::optional<std::vector<std::byte>> wav;
        if (snd.music) wav = s.mpqs.try_read(std::string(R"(data\global\music\)") + snd.file);
        for (const char* root : { R"(data\global\sfx\)", R"(data\local\sfx\)" })
            if (!wav) wav = s.mpqs.try_read(std::string(root) + snd.file);
        if (!wav) { d2d::log::warn("sound {}: {} not found", index, snd.file); return; }
        start(c, *wav, float(std::clamp(snd.volume, 0, 255)) / 255.f, snd.loop, index);
    }
    void play_voice(const Scene& s, int index) { play(voice, s, index); }
    // Music from its full path, on the worker. `id` identifies it (a
    // Sounds.txt index for level songs, negative for the frontend list).
    bool music_job_loop = true;
    void play_music_path(const Scene& s, std::string path, int id, float gain, bool loop) {
        stop(music);
        if (!ok) return;
        music.sound = id;                        // pending until the job lands
        music_job_sound = id;
        music_job_gain = gain;
        music_job_loop = loop;
        music_job = std::async(std::launch::async, [dir = s.data_dir, path = std::move(path)]() -> std::optional<Decoded> {
            d2d::mpq::Stack st;
            for (const char* n : { "d2xmusic.mpq", "d2music.mpq" })
                if (fs::exists(dir / n)) st.push(dir / n);
            const auto wav = st.try_read(path);
            return wav ? decode(*wav) : std::nullopt;
        });
    }
    void play_music(const Scene& s, int index) {
        if (index <= 0 || std::size_t(index) >= s.sounds.size()) { stop(music); return; }
        const auto& snd = s.sounds[std::size_t(index)];
        play_music_path(s, std::string(R"(data\global\music\)") + snd.file, index,
                        float(std::clamp(snd.volume, 0, 255)) / 255.f, true);
    }
    // A fixed-path UI sound (game.exe names these directly, not via
    // Sounds.txt); the file is cached, each play restarts the channel.
    void play_file(Channel& c, const Scene& s, const std::string& path) {
        if (!ok) return;
        auto it = file_cache.find(path);
        if (it == file_cache.end()) {
            auto b = s.mpqs.try_read(path);
            if (!b) return;
            it = file_cache.emplace(path, std::move(*b)).first;
        }
        start(c, it->second, 1.f, false, -1);
    }
    // Cinematic audio: a streaming source fed 4096-frame S16 stereo chunks.
    ALuint vsrc = 0;
    std::vector<ALuint> vfree, vall;
    int vrate = 0;
    void video_start(int rate) {
        video_stop();
        if (!ok || rate <= 0) return;
        alGenSources(1, &vsrc);
        alSourcei(vsrc, AL_SOURCE_RELATIVE, AL_TRUE);
        vall.resize(8);
        alGenBuffers(ALsizei(vall.size()), vall.data());
        vfree = vall;
        vrate = rate;
    }
    void video_feed(std::vector<std::int16_t>& pcm) {
        if (!vsrc) { pcm.clear(); return; }
        ALint done = 0;
        alGetSourcei(vsrc, AL_BUFFERS_PROCESSED, &done);
        while (done-- > 0) { ALuint b = 0; alSourceUnqueueBuffers(vsrc, 1, &b); vfree.push_back(b); }
        constexpr std::size_t kChunk = 4096 * 2;
        std::size_t at = 0;
        while (!vfree.empty() && at < pcm.size()) {
            const std::size_t n = std::min(kChunk, pcm.size() - at);
            const ALuint b = vfree.back(); vfree.pop_back();
            alBufferData(b, AL_FORMAT_STEREO16, pcm.data() + at, ALsizei(n * 2), vrate);
            alSourceQueueBuffers(vsrc, 1, &b);
            at += n;
        }
        pcm.erase(pcm.begin(), pcm.begin() + std::ptrdiff_t(at));
        ALint state = 0;
        alGetSourcei(vsrc, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING && vfree.size() < vall.size()) alSourcePlay(vsrc);
    }
    void video_stop() {
        if (vsrc) { alSourceStop(vsrc); alDeleteSources(1, &vsrc); vsrc = 0; }
        if (!vall.empty()) alDeleteBuffers(ALsizei(vall.size()), vall.data());
        vall.clear(); vfree.clear();
    }

    // Land a finished music job; free one-shots that have played out.
    void update() {
        if (music_job.valid() && music_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto d = music_job.get();
            if (!d) d2d::log::warn("music {}: not loaded", music_job_sound);
            else if (music.sound == music_job_sound) start(music, *d, music_job_gain, music_job_loop, music_job_sound);
        }
        for (auto* c : { &voice, &ui, &ambience, &music }) {
            if (!c->src) continue;
            ALint state = 0;
            alGetSourcei(c->src, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED) stop(*c);
        }
    }
};

}  // namespace
