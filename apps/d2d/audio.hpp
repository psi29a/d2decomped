// Sound output (OpenAL): voice, music, ambience and UI channels.
#pragma once

#include "common.hpp"

#include "platform.hpp"

namespace d2d::client {

// Sound output: OpenAL (openal-soft, as ../thirdeye), one source + buffer
// per channel: the NPC voice, the level's song, its ambience and UI
// clicks. Sounds.txt `Loop` entries play with AL_LOOPING. WAVs are decoded
// with SDL (PCM/ADPCM) to 8/16-bit PCM. Files: music (`Music Vol` 1)
// under data\global\music, the rest under data\global\sfx, speech under
// data\local\sfx. Headless runs use openal-soft's null backend.
// Changing songs cross-fades like game.exe: the old one fades out over its
// Sounds.txt Fade Out, the new one in over its Fade In (FUN_004b9ef0 /
// FUN_004ba020 ramp the volume linearly), in sound ticks — 25 a second.
// A level song picks up where it left off, at its next Block cue point
// (FUN_004dcaa0, below).
// The ambience cross-fades at dusk and dawn over 250 sound ticks
// (FUN_004e42e0: FUN_004b9ef0(0, 0xfa)), and switches at once otherwise.
// ponytail: channels are head-relative (no 3D positions yet).
// A song's resume point from its position (FUN_004dcaa0).
[[nodiscard]] constexpr int next_block(const std::array<int, 3>& blocks, int pos) {
    if (pos >= 0 && pos < blocks[0]) return blocks[0];
    if (pos >= blocks[0] && pos < blocks[1]) return blocks[1];
    if (pos >= blocks[1] && pos < blocks[2]) return blocks[2];
    return 0;
}
static_assert(next_block({ 100, 200, -1 }, 50) == 100 && next_block({ 100, 200, -1 }, 150) == 200
              && next_block({ 100, 200, -1 }, 250) == 0 && next_block({ -1, -1, -1 }, 10) == 0);

struct Audio {
    struct Decoded { ALenum format = 0; ALsizei freq = 0; std::vector<Uint8> pcm; };
    struct Channel {
        ALuint src = 0, buf = 0;
        int sound = 0;                           // Sounds.txt index, 0 = silent, -1 = a fixed file
        float gain = 1.f, fade_from = 0.f, fade_to = 0.f;
        std::uint64_t fade_t0 = 0, fade_t1 = 0;  // SDL ms; t1 = 0: not fading
    };
    static constexpr std::uint64_t kTickMs = 40;   // a sound tick (FUN_00482c20, 25 Hz)
    bool ok = false;
    ALCdevice* dev = nullptr;
    ALCcontext* ctx = nullptr;
    Channel voice, music, ambience, ui_sounds, rain;
    std::array<Channel, 12> sfx;                 // world sounds, oldest reused first
    std::size_t sfx_next = 0;
    Channel music_old;                           // the previous song, fading out under `music`
    Channel ambience_old;                        // the day's (night's) ambience, fading out under `ambience`
    std::uint64_t music_fade_in_ms = 0;          // for the song being decoded
    int voice_sound() const { return voice.sound; }
    // Songs are ~20 MB WAVs (240 ms to read): decoded on a worker with its
    // own MPQ handles (StormLib handles aren't shared across threads).
    std::future<std::optional<Decoded>> music_job;
    // Replaced jobs still running: an async future's destructor waits for
    // its task, so they're let finish here instead of on the frame.
    std::vector<std::future<std::optional<Decoded>>> music_dropped;
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
        for (auto* channel : { &voice, &music, &music_old, &ambience, &ambience_old, &ui_sounds, &rain }) stop(*channel);
        for (auto& channel : sfx) stop(channel);
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
        Decoded decoded;
        decoded.freq = spec.freq;
        if (spec.format == SDL_AUDIO_U8 || spec.format == SDL_AUDIO_S16LE) {
            decoded.pcm.assign(buf, buf + len);
        } else {                                 // anything else -> S16
            SDL_AudioSpec target_spec{ SDL_AUDIO_S16LE, spec.channels, spec.freq };
            Uint8* out = nullptr;
            int out_len = 0;
            if (!SDL_ConvertAudioSamples(&spec, buf, int(len), &target_spec, &out, &out_len)) { SDL_free(buf); return std::nullopt; }
            decoded.pcm.assign(out, out + out_len);
            SDL_free(out);
            spec.format = SDL_AUDIO_S16LE;
        }
        SDL_free(buf);
        const bool eight = spec.format == SDL_AUDIO_U8;
        if (spec.channels == 1) decoded.format = eight ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
        else if (spec.channels == 2) decoded.format = eight ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
        else return std::nullopt;
        return decoded;
    }
    void stop(Channel& channel) {
        if (channel.src) { alSourceStop(channel.src); alDeleteSources(1, &channel.src); }
        if (channel.buf) alDeleteBuffers(1, &channel.buf);
        channel = {};
    }
    void stop_voice() { stop(voice); }
    bool start(Channel& channel, const Decoded& decoded, float gain, bool loop, int index) {
        stop(channel);
        if (!ok) return false;
        alGenBuffers(1, &channel.buf);
        alBufferData(channel.buf, decoded.format, decoded.pcm.data(), ALsizei(decoded.pcm.size()), decoded.freq);
        alGenSources(1, &channel.src);
        alSourcei(channel.src, AL_BUFFER, ALint(channel.buf));
        alSourcef(channel.src, AL_GAIN, gain);
        channel.gain = gain;
        channel.fade_t1 = 0;
        alSourcei(channel.src, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
        alSourcei(channel.src, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcePlay(channel.src);
        if (alGetError() != AL_NO_ERROR) { stop(channel); return false; }
        channel.sound = index;
        return true;
    }
    bool start(Channel& channel, std::span<const std::byte> wav, float gain, bool loop, int index) {
        const auto decoded = decode(wav);
        if (!decoded) { d2d::log::warn("sound {}: {}", index, SDL_GetError()); return false; }
        return start(channel, *decoded, gain, loop, index);
    }
    // A world sound at `gain` (its distance), a random one of its group,
    // `pan` -1 (left) .. 1 (right).
    // ponytail: world sounds don't pan by where they are yet; only the
    // level's ambient events do.
    void play_sfx(const GameData& game_data, int index, float gain, int variant, float pan = 0.f) {
        if (!ok || index <= 0 || std::size_t(index) >= game_data.sounds.size() || gain <= 0.01f) return;
        const int group = game_data.sounds[std::size_t(index)].group;
        auto& channel = sfx[sfx_next++ % sfx.size()];
        play(channel, game_data, group > 1 ? index + variant % group : index);
        if (channel.src) alSourcef(channel.src, AL_GAIN, channel.gain * gain);
        if (channel.src && pan != 0.f) alSource3f(channel.src, AL_POSITION, pan, 0.f, -std::sqrt(std::max(0.f, 1.f - pan * pan)));
    }
    void play(Channel& channel, const GameData& game_data, int index) {
        stop(channel);
        if (!ok || index <= 0 || std::size_t(index) >= game_data.sounds.size()) return;
        const auto& snd = game_data.sounds[std::size_t(index)];
        std::optional<std::vector<std::byte>> wav;
        if (snd.music) wav = game_data.mpqs.try_read(std::string(R"(data\global\music\)") + snd.file);
        for (const char* root : { R"(data\global\sfx\)", R"(data\local\sfx\)" })
            if (!wav) wav = game_data.mpqs.try_read(std::string(root) + snd.file);
        if (!wav) { d2d::log::warn("sound {}: {} not found", index, snd.file); return; }
        start(channel, *wav, float(std::clamp(snd.volume, 0, 255)) / 255.f, snd.loop, index);
    }
    void play_voice(const GameData& game_data, int index) { play(voice, game_data, index); }
    // Music from its full path, on the worker. `id` identifies it (a
    // Sounds.txt index for level songs, negative for the frontend list).
    bool music_job_loop = true;
    void play_music_path(const GameData& game_data, std::string path, int id, float gain, bool loop) {
        stop(music);
        if (!ok) return;
        music.sound = id;                        // pending until the job lands
        music_job_sound = id;
        music_job_gain = gain;
        music_job_loop = loop;
        if (music_job.valid()) music_dropped.push_back(std::move(music_job));
        music_job = std::async(std::launch::async, [dir = game_data.data_dir, path = std::move(path)]() -> std::optional<Decoded> {
            d2d::mpq::Stack music_mpqs;
            for (const char* name : { "d2xmusic.mpq", "d2music.mpq" })
                if (fs::exists(dir / name)) music_mpqs.push(dir / name);
            const auto wav = music_mpqs.try_read(path);
            return wav ? decode(*wav) : std::nullopt;
        });
    }
    void set_gain(Channel& channel, float gain) {
        channel.gain = gain;
        if (channel.src) alSourcef(channel.src, AL_GAIN, gain);
    }
    void fade(Channel& channel, float target_gain, std::uint64_t duration_ms) {
        channel.fade_from = channel.gain;
        channel.fade_to = target_gain;
        channel.fade_t0 = SDL_GetTicks();
        channel.fade_t1 = channel.fade_t0 + std::max<std::uint64_t>(duration_ms, 1);
    }
    // A new level's song (FUN_004dcaa0): the playing one fades out while
    // this one, once decoded, fades in.
    // Day to night (or back): the old ambience out and the new one in over
    // 250 sound ticks (10 s).
    void crossfade_ambience(const GameData& game_data, int index) {
        stop(ambience_old);
        if (ambience.src) {
            ambience_old = ambience;
            ambience = {};
            fade(ambience_old, 0.f, 250 * kTickMs);
        }
        play(ambience, game_data, index);
        if (ambience.src) {
            const float target_gain = ambience.gain;
            set_gain(ambience, 0.f);
            fade(ambience, target_gain, 250 * kTickMs);
        }
    }
    void crossfade_music(const GameData& game_data, int index) {
        auto fade_of = [&](int index, bool fading_in) {
            if (index <= 0 || std::size_t(index) >= game_data.sounds.size()) return std::uint64_t(0);
            return std::uint64_t(fading_in ? game_data.sounds[std::size_t(index)].fade_in : game_data.sounds[std::size_t(index)].fade_out) * kTickMs;
        };
        stop(music_old);
        if (music.src) {
            const auto out = fade_of(music.sound, false);
            music_old = music;
            music = {};
            fade(music_old, 0.f, out);
        }
        music_fade_in_ms = fade_of(index, true);
        play_music(game_data, index);
    }
    // Where each song picks up (FUN_004dcaa0): every 125 sound ticks (5 s)
    // of play, the next Block cue point past the position — [0, block 1)
    // -> block 1, [1, 2) -> 2, [2, 3) -> 3, else the start. By Sounds.txt
    // index, in sample frames.
    std::unordered_map<int, int> song_resume;
    std::array<int, 3> music_blocks{ -1, -1, -1 };
    std::uint64_t music_mark_ms = 0;
    void play_music(const GameData& game_data, int index) {
        if (index <= 0 || std::size_t(index) >= game_data.sounds.size()) { stop(music); return; }
        const auto& snd = game_data.sounds[std::size_t(index)];
        music_blocks = snd.block;
        play_music_path(game_data, std::string(R"(data\global\music\)") + snd.file, index,
                        float(std::clamp(snd.volume, 0, 255)) / 255.f, true);
    }
    // A fixed-path UI sound (game.exe names these directly, not via
    // Sounds.txt); the file is cached, each play restarts the channel.
    void play_file(Channel& channel, const GameData& game_data, const std::string& path) {
        if (!ok) return;
        auto found = file_cache.find(path);
        if (found == file_cache.end()) {
            auto bytes = game_data.mpqs.try_read(path);
            if (!bytes) return;
            found = file_cache.emplace(path, std::move(*bytes)).first;
        }
        start(channel, found->second, 1.f, false, -1);
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
        while (done-- > 0) { ALuint buffer = 0; alSourceUnqueueBuffers(vsrc, 1, &buffer); vfree.push_back(buffer); }
        constexpr std::size_t kChunk = 4096 * 2;
        std::size_t offset = 0;
        while (!vfree.empty() && offset < pcm.size()) {
            const std::size_t count = std::min(kChunk, pcm.size() - offset);
            const ALuint buffer = vfree.back(); vfree.pop_back();
            alBufferData(buffer, AL_FORMAT_STEREO16, pcm.data() + offset, ALsizei(count * 2), vrate);
            alSourceQueueBuffers(vsrc, 1, &buffer);
            offset += count;
        }
        pcm.erase(pcm.begin(), pcm.begin() + std::ptrdiff_t(offset));
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
        std::erase_if(music_dropped, [](const auto& dropped) { return dropped.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
        if (music_job.valid() && music_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto decoded = music_job.get();
            if (!decoded) d2d::log::warn("music {}: not loaded", music_job_sound);
            else if (music.sound == music_job_sound && start(music, *decoded, music_job_gain, music_job_loop, music_job_sound)) {
                if (const auto found = song_resume.find(music.sound); found != song_resume.end() && found->second > 0)
                    alSourcei(music.src, AL_SAMPLE_OFFSET, found->second);
                music_mark_ms = SDL_GetTicks();
                if (music_fade_in_ms) {
                    set_gain(music, 0.f);
                    fade(music, music_job_gain, music_fade_in_ms);
                }
            }
            music_fade_in_ms = 0;
        }
        const auto now = SDL_GetTicks();
        if (music.src && music.sound > 0 && now - music_mark_ms > 125 * kTickMs) {
            ALint pos = 0;
            alGetSourcei(music.src, AL_SAMPLE_OFFSET, &pos);
            song_resume[music.sound] = next_block(music_blocks, pos);
            music_mark_ms = now;
        }
        for (auto* channel : { &music, &music_old, &ambience, &ambience_old }) {
            if (!channel->fade_t1) continue;
            const float fraction = std::min(1.f, float(now - channel->fade_t0) / float(channel->fade_t1 - channel->fade_t0));
            set_gain(*channel, channel->fade_from + (channel->fade_to - channel->fade_from) * fraction);
            if (fraction < 1.f) continue;
            channel->fade_t1 = 0;
            if (channel == &music_old) stop(music_old);
            if (channel == &ambience_old) stop(ambience_old);
        }
        for (auto* channel : { &voice, &ui_sounds, &ambience, &rain, &music, &music_old }) {
            if (!channel->src) continue;
            ALint state = 0;
            alGetSourcei(channel->src, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED) stop(*channel);
        }
    }
};

// Plays the World's cues (cues.hpp) that are due, quieter with distance
// (silent past 20 cells from the listener).
inline void play_cues(Cues& cues, Audio& audio, float listener_x, float listener_y, d2d::rules::Rng& rng, std::uint32_t now_ms) {
    std::erase_if(cues.due, [&](const Cues::Cue& cue) {
        if (now_ms < cue.when_ms) return false;
        const float distance = std::hypot(cue.x - listener_x, cue.y - listener_y);
        audio.play_sfx(*cues.game_data, cue.sound, std::clamp(1.f - distance / 20.f, 0.f, 1.f), rng(16));
        return true;
    });
}

}  // namespace d2d::client
