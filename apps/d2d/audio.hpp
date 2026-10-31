// SPDX-License-Identifier: GPL-3.0-or-later
// Sound output (OpenAL): voice, music, ambience and UI channels.
#pragma once

#include "common.hpp"
#include "platform.hpp"

#include <install.hpp>
#include <log.hpp>
#include <rules.hpp>
#include <sound_mix.hpp>
#include <sound_start.hpp>
#include <shake.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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
        ALuint right_src = 0, right_buf = 0;     // a split world sound: src the left speaker, this the right
        int sound = 0;                           // Sounds.txt index, 0 = silent, -1 = a fixed file
        int owner = 0;                           // a missile's sound id (Cues::Cue::owner): stops with it
        float gain = 1.f, fade_from = 0.f, fade_to = 0.f;
        std::uint64_t fade_t0 = 0, fade_t1 = 0;  // SDL ms; t1 = 0: not fading
        // A world sound (play_world): where it is (cells), its Sounds.txt
        // Volume and Falloff, and Tracking: its mix redone every frame.
        bool placed = false, tracking = false;
        float x = 0.f, y = 0.f;
        int volume = 255, falloff = 0;
        float screen_x = 0.f, screen_y2 = 0.f;   // a placed one from the listener at its last mix, client pixels (rules::screen_offset)
        // How it started (rules/sound_start.hpp): its group's first row, the
        // unit it plays on, the sound tick it started, Solo.
        int group_base = 0;
        std::uint64_t unit = 0;
        std::int64_t start_tick = 0;
        bool solo = false;
    };
    static constexpr std::uint64_t kTickMs = 40;   // a sound tick (FUN_00482c20, 25 Hz)
    bool ok = false;
    ALCdevice* dev = nullptr;
    ALCcontext* ctx = nullptr;
    Channel voice, music, ambience, ui_sounds, rain, quake_loop;
    int quake_level = 0;
    std::array<Channel, 32> sfx;                 // world sounds: a free one, else the oldest reused
    std::size_t sfx_next = 0;
    Channel music_old;                           // the previous song, fading out under `music`
    Channel ambience_old;                        // the day's (night's) ambience, fading out under `ambience`
    std::uint64_t music_fade_in_ms = 0;          // for the song being decoded
    d2d::mpq::Stack music_mpqs;                  // music archives aren't part of GameData::mpqs
    std::filesystem::path music_mpqs_dir;
    std::unordered_set<int> unplayable;          // Sounds.txt rows that didn't read or decode: logged once, not tried again
    int voice_sound() const { return voice.sound; }
    // The game menu's sliders, 0..100 ("Master Volume" 0x8817b0, "Music
    // Volume" 0x8817b4): 0x4dfc84 scales a sound's volume by music / 100
    // if it's a Music Vol one, then everything by master / 100.
    // ponytail: master is the listener's gain, so it scales cinematics too.
    int master_volume = 100, music_volume = 50;
    [[nodiscard]] float out_gain(const Channel& channel, float gain) const {
        return (&channel == &music || &channel == &music_old ? gain * float(music_volume) / 100.f : gain) * duck(channel);
    }
    // The ducks (FUN_004ba020, 0x4dfcb2 / 0x4dfd0b): under a Solo row the
    // rest at down to 70%, under the paused game menu all but the cursor,
    // ambience and music rows down to 0, stepped each sound tick.
    // ponytail: game.exe's paused frame runs the sound tick unpaced, so its
    // fade is quicker than these 40 ms ticks.
    int solo_percent = 100, menu_percent = 100;
    bool menu_paused = false;                    // a local game paused under the game menu (Town)
    std::int64_t sound_tick = 0;                 // DAT_007bc9bc: +1 each 40 ms
    std::uint64_t tick_mark_ms = 0;
    // A placed sound takes them in its integer volume (apply_mix, rules::ducked_mix).
    // ponytail: one not placed (UI, voice, ambience, music) has a float gain
    // with its fades and takes them as a multiplier, not truncated percent steps.
    [[nodiscard]] float duck(const Channel& channel) const {
        // played by path, no Sounds.txt row (the frontend's music, button.wav): not a game sound, never ducked
        if ((solo_percent == 100 && menu_percent == 100) || channel.sound <= 0) return 1.f;
        return float(d2d::rules::ducked_volume(10000, channel.sound, channel.solo, menu_percent, solo_percent)) / 10000.f;
    }
    void apply_volume() {
        if (!ok) return;
        alListenerf(AL_GAIN, float(master_volume) / 100.f);
        for (auto* channel : { &music, &music_old }) set_gain(*channel, channel->gain);
    }
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
        direct_channels = alIsExtensionPresent("AL_SOFT_direct_channels") == AL_TRUE;
    }
    ~Audio() {
        video_stop();
        for (auto* channel : { &voice, &music, &music_old, &ambience, &ambience_old, &ui_sounds, &rain, &quake_loop }) stop(*channel);
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
        if (channel.right_src) { alSourceStop(channel.right_src); alDeleteSources(1, &channel.right_src); }
        if (channel.right_buf) alDeleteBuffers(1, &channel.right_buf);
        channel = {};
    }
    void stop_voice() { stop(voice); }
    [[nodiscard]] bool owner_playing(int owner) const {
        return std::ranges::any_of(sfx, [&](const Channel& channel) {
            ALint state = AL_STOPPED;
            if (channel.owner == owner && channel.src) alGetSourcei(channel.src, AL_SOURCE_STATE, &state);
            return state == AL_PLAYING || state == AL_PAUSED;
        });
    }
    // The sounds of missiles and objects no longer in `live` (their sound ids) stop.
    template <class Live> void stop_owners(Live&& live) {
        for (auto& channel : sfx) if (channel.owner && !live(channel.owner)) stop(channel);
    }
    bool start(Channel& channel, const Decoded& decoded, float gain, bool loop, int index) {
        stop(channel);
        if (!ok) return false;
        alGenBuffers(1, &channel.buf);
        alBufferData(channel.buf, decoded.format, decoded.pcm.data(), ALsizei(decoded.pcm.size()), decoded.freq);
        alGenSources(1, &channel.src);
        alSourcei(channel.src, AL_BUFFER, ALint(channel.buf));
        alSourcef(channel.src, AL_GAIN, out_gain(channel, gain));
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
        if (!decoded) {
            d2d::log::warn("sound {}: {}", index, SDL_GetError());
            if (index > 0) unplayable.insert(index);
            return false;
        }
        return start(channel, *decoded, gain, loop, index);
    }
    // A sound not placed in the world (the UI's, the level's ambient events)
    // at `gain`, a random one of its group, `pan` -1 (left) .. 1 (right).
    // World sounds go through play_world.
    void play_sfx(const GameData& game_data, int index, float gain, int variant, float pan = 0.f, int owner = 0, std::uint64_t unit = 0) {
        if (!ok || index <= 0 || std::size_t(index) >= game_data.sounds.size() || gain <= 0.01f) return;
        if (!may_start(game_data, index, unit, false)) return;
        const int group = game_data.sounds[std::size_t(index)].group;
        auto& channel = free_channel();
        play(channel, game_data, group > 1 ? index + variant % group : index);
        channel.owner = owner;
        started(channel, game_data, index, unit);
        if (channel.src) set_gain(channel, channel.gain * gain);
        if (channel.src && pan != 0.f) alSource3f(channel.src, AL_POSITION, pan, 0.f, -std::sqrt(std::max(0.f, 1.f - pan * pan)));
    }
    // A world sound `x`, `y` cells away from the listener at `listener_x`,
    // `listener_y`: game.exe's distance volume and pan (rules/sound_mix.hpp);
    // not started past its Falloff's far range.
    void play_world(const GameData& game_data, int index, int variant, float x, float y, float listener_x, float listener_y, int owner = 0,
                    std::uint64_t unit = 0) {
        const auto* snd = variant_row(game_data, index, variant);
        if (!snd) return;
        const auto [screen_x, screen_y2] = d2d::rules::screen_offset(x - listener_x, y - listener_y);
        const auto mix = d2d::rules::sound_mix_pixels(std::clamp(snd->volume, 0, 255), snd->falloff, screen_x, screen_y2);
        if (!ok || !mix.audible || mix.volume <= 0 || !may_start(game_data, index, unit, true)) return;
        if (Channel* channel = play_mixed(game_data, *snd, int(snd - game_data.sounds.data()), screen_x, screen_y2)) {
            started(*channel, game_data, index, unit);
            channel->owner = owner;
            channel->tracking = snd->tracking;
            channel->x = x;
            channel->y = y;
        }
    }
    // One placed by client pixels from the listener instead (dx, and 2 dy as
    // FUN_004b97d0 doubles it), kept there: the level's ambient events.
    void play_placed(const GameData& game_data, int index, int variant, float screen_x, float screen_y2) {
        const auto* snd = variant_row(game_data, index, variant);
        if (!snd) return;
        if (!may_start(game_data, index, 0, true)) return;
        if (Channel* channel = play_mixed(game_data, *snd, int(snd - game_data.sounds.data()), screen_x, screen_y2))
            started(*channel, game_data, index, 0);
    }
    // A free world channel, else the oldest reused.
    Channel& free_channel() {
        const auto done = [](const Channel& channel) {
            ALint state = AL_STOPPED;
            if (channel.src) alGetSourcei(channel.src, AL_SOURCE_STATE, &state);
            return state != AL_PLAYING && state != AL_PAUSED;
        };
        const auto found = std::ranges::find_if(sfx, done);
        return found != sfx.end() ? *found : sfx[sfx_next++ % sfx.size()];
    }
    // Whether `index` starts against the world sounds playing (rules/sound_start.hpp):
    // Compound merges it into one of its group (nothing new starts; ponytail: the
    // new unit isn't added to that one's list), Defer / Stop Inst and a unit's
    // speech drop it or stop the one it clashes with. game.exe checks Compound
    // at the play and the rest at the start; a cue plays when due, so both here.
    bool may_start(const GameData& game_data, int index, std::uint64_t unit, bool positioned) {
        const auto& snd = game_data.sounds[std::size_t(index)];
        std::vector<d2d::rules::SoundInstance> playing;
        std::vector<Channel*> on_channel;          // playing[k]'s channel
        for (auto& channel : sfx) {
            if (!channel.src || channel.sound <= 0) continue;
            ALint state = AL_STOPPED;
            alGetSourcei(channel.src, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING && state != AL_PAUSED) continue;
            playing.push_back({ channel.sound, channel.group_base, channel.unit, channel.start_tick, false });
            on_channel.push_back(&channel);
        }
        const d2d::rules::SoundRule rule{ index, snd.group_base, snd.compound, snd.defer, snd.stop_inst };
        if (d2d::rules::compound_into(playing, rule, sound_tick)) return false;
        const auto clash = d2d::rules::start_clash(playing, rule, unit, positioned);
        if (clash.other && clash.stop_other) stop(*on_channel[*clash.other]);
        return clash.play;
    }
    // One of `unit`'s speech lines playing (FUN_004b9700 on it: rules::speech_row).
    bool speaking(std::uint64_t unit) {
        return unit != 0 && std::ranges::any_of(sfx, [&](const Channel& channel) {
            if (!channel.src || channel.unit != unit || !d2d::rules::speech_row(channel.sound)) return false;
            ALint state = AL_STOPPED;
            alGetSourcei(channel.src, AL_SOURCE_STATE, &state);
            return state == AL_PLAYING || state == AL_PAUSED;
        });
    }
    void started(Channel& channel, const GameData& game_data, int index, std::uint64_t unit) {
        channel.group_base = game_data.sounds[std::size_t(index)].group_base;
        channel.unit = unit;
        channel.start_tick = sound_tick;
    }
    // AL_SOFT_direct_channels: a split world sound's two buffers go each
    // straight to its speaker, so game.exe's pan law holds (Speakers).
    bool direct_channels = false;
    static constexpr ALenum kDirectChannelsSoft = 0x1033;   // AL_DIRECT_CHANNELS_SOFT (alext.h)
    // A placed sound's mix `screen_x`, `screen_y2` from the listener, under the
    // ducks as FUN_004dfc20 has them (rules::ducked_mix); false past its far range.
    bool apply_mix(Channel& channel, float screen_x, float screen_y2) {
        channel.screen_x = screen_x;
        channel.screen_y2 = screen_y2;
        // played by path, no Sounds.txt row: never ducked (as `duck`)
        const auto mix = channel.sound > 0
            ? d2d::rules::ducked_mix(channel.volume, channel.falloff, screen_x, screen_y2, channel.sound, channel.solo, menu_percent, solo_percent)
            : d2d::rules::sound_mix_pixels(channel.volume, channel.falloff, screen_x, screen_y2);
        if (!mix.audible) return false;
        if (channel.right_src) {
            const auto speakers = d2d::rules::speakers(mix);
            alSourcef(channel.src, AL_GAIN, speakers.left);
            alSourcef(channel.right_src, AL_GAIN, speakers.right);
            return true;
        }
        // ponytail: without direct channels OpenAL pans the mono source by
        // its angle, not game.exe's far-side-down law.
        const float pan = std::clamp(float(mix.pan - 128) / 128.f, -1.f, 1.f);
        alSourcef(channel.src, AL_GAIN, float(mix.volume) / 255.f);
        alSource3f(channel.src, AL_POSITION, pan, 0.f, -std::sqrt(std::max(0.f, 1.f - pan * pan)));
        return true;
    }
    // Every frame: Tracking sounds follow their owner (`where(owner)`, a
    // position or nothing) and the listener, and stop past their far range
    // (FUN_004ba020's per-tick pass: FUN_004b97d0, FUN_004dfc20).
    template <class Where> void track(float listener_x, float listener_y, Where&& where) {
        for (auto& channel : sfx) {
            if (!channel.src || !channel.placed || !channel.tracking) continue;
            if (const auto position = where(channel.owner)) { channel.x = position->first; channel.y = position->second; }
            const auto [screen_x, screen_y2] = d2d::rules::screen_offset(channel.x - listener_x, channel.y - listener_y);
            if (!apply_mix(channel, screen_x, screen_y2)) stop(channel);
        }
    }
    // Leaving the game: every world sound, the level's ambience and the rain.
    void stop_quake() { stop(quake_loop); quake_level = 0; }
    // FUN_004769d0 owns a separate, unplaced loop whose instance volume
    // moves six units per rendered frame toward the shake envelope.
    void quake(const GameData& game_data, int strength) {
        if (!ok) return;
        const int volume = d2d::rules::quake_volume(quake_level, strength);
        if (volume == 0) { stop_quake(); return; }
        if (!quake_loop.src) {
            const auto found = game_data.sound_index.find("andariel_quake_loop");
            if (found == game_data.sound_index.end()) return;
            play(quake_loop, game_data, found->second);
            if (!quake_loop.src) return;
        }
        quake_level = volume;
        set_gain(quake_loop, float(volume) / 255.f);
    }
    void stop_world() {
        stop_quake();
        for (auto& channel : sfx) stop(channel);
        for (auto* channel : { &ambience, &ambience_old, &rain }) stop(*channel);
        world_paused = false;
        menu_paused = false;                     // left from the game menu: step_ducks fades back up
    }
    // A cinematic over the game: the world's sounds hold until it ends.
    // (Under the paused game menu they fade out instead: menu_paused.)
    bool world_paused = false;
    void pause_world(bool paused) {
        if (paused == world_paused || !ok) return;
        world_paused = paused;
        for (auto* channel : { &ambience, &ambience_old, &rain, &quake_loop }) if (channel->src) (paused ? alSourcePause : alSourcePlay)(channel->src);
        for (auto& channel : sfx) {
            if (!channel.src) continue;
            ALint state = AL_STOPPED;
            alGetSourcei(channel.src, AL_SOURCE_STATE, &state);
            const std::array<ALuint, 2> both{ channel.src, channel.right_src };
            if (paused && state == AL_PLAYING) alSourcePausev(channel.right_src ? 2 : 1, both.data());
            if (!paused && state == AL_PAUSED) alSourcePlayv(channel.right_src ? 2 : 1, both.data());
        }
    }
    std::optional<std::vector<std::byte>> read_wav(const GameData& game_data, const GameData::Sound& snd, int index) {
        std::optional<std::vector<std::byte>> wav;
        if (unplayable.contains(index)) return wav;
        // One there that won't read (a cut-short MPQ) goes unheard.
        try {
            if (snd.music) wav = game_data.mpqs.try_read(std::string(R"(data\global\music\)") + snd.file);
            if (snd.music && !wav) {
                if (music_mpqs_dir != game_data.data_dir) {
                    music_mpqs = {};
                    music_mpqs_dir = game_data.data_dir;
                    for (const char* name : { "d2xmusic.mpq", "d2music.mpq" })
                        if (const auto mpq = d2d::install::find_file(music_mpqs_dir, name)) {
                            try { music_mpqs.push(*mpq); }
                            catch (const std::exception& error) { d2d::log::warn("music {}: {}", name, error.what()); }
                        }
                }
                wav = music_mpqs.try_read(std::string(R"(data\global\music\)") + snd.file);
            }
            for (const char* root : { R"(data\global\sfx\)", R"(data\local\sfx\)" })
                if (!wav) wav = game_data.mpqs.try_read(std::string(root) + snd.file);
        } catch (const std::exception& error) {
            d2d::log::warn("sound {}: {}", index, error.what());
            unplayable.insert(index);
            return std::nullopt;
        }
        if (!wav) {
            if (!game_data.mpqs.failed(std::string(R"(data\global\sfx\)") + snd.file))   // else logged as unreadable
                d2d::log::warn("sound {}: {} not found", index, snd.file);
            unplayable.insert(index);
        }
        return wav;
    }
    void play(Channel& channel, const GameData& game_data, int index) {
        stop(channel);
        if (!ok || index <= 0 || std::size_t(index) >= game_data.sounds.size()) return;
        const auto& snd = game_data.sounds[std::size_t(index)];
        if (const auto wav = read_wav(game_data, snd, index); wav && start(channel, *wav, float(std::clamp(snd.volume, 0, 255)) / 255.f, snd.loop, index)) {
            channel.solo = snd.solo;
            set_gain(channel, channel.gain);
        }
    }
    // `index`'s row, or a random one of its group.
    static const GameData::Sound* variant_row(const GameData& game_data, int index, int variant) {
        if (index <= 0 || std::size_t(index) >= game_data.sounds.size()) return nullptr;
        const int group = game_data.sounds[std::size_t(index)].group;
        const int row = group > 1 ? index + variant % group : index;
        return std::size_t(row) < game_data.sounds.size() ? &game_data.sounds[std::size_t(row)] : nullptr;
    }
    // A free world channel (else the oldest), the sound started in it `screen_x`, `screen_y2` from the listener.
    Channel* play_mixed(const GameData& game_data, const GameData::Sound& snd, int index, float screen_x, float screen_y2) {
        const auto mix = d2d::rules::sound_mix_pixels(std::clamp(snd.volume, 0, 255), snd.falloff, screen_x, screen_y2);
        if (!ok || !mix.audible || mix.volume <= 0 || unplayable.contains(index)) return nullptr;
        auto& channel = free_channel();
        stop(channel);
        const auto wav = read_wav(game_data, snd, index);
        if (!wav) return nullptr;
        if (direct_channels) {
            const auto decoded = decode(*wav);
            if (!decoded) {
                d2d::log::warn("sound {}: {}", index, SDL_GetError());
                unplayable.insert(index);
                return nullptr;
            }
            start_split(channel, *decoded, snd.loop, index);
        } else {
            start(channel, *wav, 1.f, snd.loop, index);
        }
        if (!channel.src) return nullptr;
        channel.placed = true;
        channel.solo = snd.solo;
        channel.volume = std::clamp(snd.volume, 0, 255);
        channel.falloff = snd.falloff;
        apply_mix(channel, screen_x, screen_y2);
        if (world_paused) {
            alSourcePause(channel.src);
            if (channel.right_src) alSourcePause(channel.right_src);
        }
        return &channel;
    }
    // A sound as two stereo buffers, one silent side each (S16), playing
    // in step: the left speaker's and the right's.
    bool start_split(Channel& channel, const Decoded& decoded, bool loop, int index) {
        const bool stereo = decoded.format == AL_FORMAT_STEREO8 || decoded.format == AL_FORMAT_STEREO16;
        const bool eight = decoded.format == AL_FORMAT_MONO8 || decoded.format == AL_FORMAT_STEREO8;
        const std::size_t sample_size = eight ? 1 : 2, frames = decoded.pcm.size() / (sample_size * (stereo ? 2 : 1));
        const auto sample = [&](std::size_t frame, int side) -> std::int16_t {
            const std::size_t offset = (frame * (stereo ? 2 : 1) + (stereo ? std::size_t(side) : 0)) * sample_size;
            if (eight) return std::int16_t((int(decoded.pcm[offset]) - 128) * 256);
            return std::int16_t(decoded.pcm[offset] | (decoded.pcm[offset + 1] << 8));
        };
        std::vector<std::int16_t> left(frames * 2, 0), right(frames * 2, 0);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            left[frame * 2] = sample(frame, 0);
            right[frame * 2 + 1] = sample(frame, 1);
        }
        const auto make = [&](ALuint& src, ALuint& buf, const std::vector<std::int16_t>& pcm) {
            alGenBuffers(1, &buf);
            alBufferData(buf, AL_FORMAT_STEREO16, pcm.data(), ALsizei(pcm.size() * sizeof(std::int16_t)), decoded.freq);
            alGenSources(1, &src);
            alSourcei(src, AL_BUFFER, ALint(buf));
            alSourcei(src, kDirectChannelsSoft, AL_TRUE);
            alSourcei(src, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
            alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
        };
        make(channel.src, channel.buf, left);
        make(channel.right_src, channel.right_buf, right);
        const std::array<ALuint, 2> both{ channel.src, channel.right_src };
        alSourcePlayv(2, both.data());
        if (alGetError() != AL_NO_ERROR) { stop(channel); return false; }
        channel.gain = 1.f;
        channel.fade_t1 = 0;
        channel.sound = index;
        return true;
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
        music_job = std::async(std::launch::async, [dir = game_data.data_dir, song_path = std::move(path)]() -> std::optional<Decoded> {
            // An MPQ that won't open is left out, one that won't read is no
            // song (update's get() would rethrow it on the main thread).
            try {
                d2d::mpq::Stack song_mpqs;
                for (const char* name : { "d2xmusic.mpq", "d2music.mpq" })
                    if (const auto mpq = d2d::install::find_file(dir, name)) {
                        try { song_mpqs.push(*mpq); }
                        catch (const std::exception& error) { d2d::log::warn("music: {}", error.what()); }
                    }
                const auto wav = song_mpqs.try_read(song_path);
                return wav ? decode(*wav) : std::nullopt;
            } catch (const std::exception& error) {
                d2d::log::warn("music {}: {}", song_path, error.what());
                return std::nullopt;
            }
        });
    }
    void set_gain(Channel& channel, float gain) {
        channel.gain = gain;
        if (channel.src) alSourcef(channel.src, AL_GAIN, out_gain(channel, gain));
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
        auto fade_of = [&](int sound, bool fading_in) {
            if (sound <= 0 || std::size_t(sound) >= game_data.sounds.size()) return std::uint64_t(0);
            return std::uint64_t(fading_in ? game_data.sounds[std::size_t(sound)].fade_in : game_data.sounds[std::size_t(sound)].fade_out) * kTickMs;
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
            std::optional<std::vector<std::byte>> bytes;
            try { bytes = game_data.mpqs.try_read(path); }
            catch (const std::exception& error) { d2d::log::warn("sound {}: {}", path, error.what()); }
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

    // Each sound tick passed: the ducks step (FUN_004ba020, 0x4ba68f); on a
    // change every channel's gain is redone.
    void step_ducks() {
        const auto now_ms = SDL_GetTicks();
        if (tick_mark_ms == 0) tick_mark_ms = now_ms;
        const auto ticks = std::min<std::uint64_t>((now_ms - tick_mark_ms) / kTickMs, 100);
        if (ticks == 0) return;
        tick_mark_ms += ticks * kTickMs;
        sound_tick += std::int64_t(ticks);
        const auto playing = [](const Channel& channel) {
            ALint state = AL_STOPPED;
            if (channel.src) alGetSourcei(channel.src, AL_SOURCE_STATE, &state);
            return state == AL_PLAYING;
        };
        // a Solo row playing and not fading out (game.exe's instance +0x45 / +0x4d)
        bool solo_playing = std::ranges::any_of(sfx, [&](const Channel& channel) { return channel.solo && playing(channel); });
        for (const auto* channel : { &voice, &ui_sounds }) solo_playing = solo_playing || (channel->solo && playing(*channel));
        const int was_solo = solo_percent, was_menu = menu_percent;
        for (std::uint64_t k = 0; k < ticks; ++k) {
            solo_percent = d2d::rules::solo_duck_step(solo_percent, solo_playing);
            menu_percent = d2d::rules::menu_duck_step(menu_percent, menu_paused);
        }
        if (solo_percent == was_solo && menu_percent == was_menu) return;
        for (auto& channel : sfx) {
            if (!channel.src) continue;
            if (channel.placed) (void)apply_mix(channel, channel.screen_x, channel.screen_y2);
            else set_gain(channel, channel.gain);
        }
        for (auto* channel : { &voice, &ui_sounds, &ambience, &ambience_old, &rain, &quake_loop, &music, &music_old }) set_gain(*channel, channel->gain);
    }
    // Land a finished music job; free one-shots that have played out.
    void update() {
        step_ducks();
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
        for (auto* channel : { &voice, &ui_sounds, &ambience, &rain, &quake_loop, &music, &music_old }) {
            if (!channel->src) continue;
            ALint state = 0;
            alGetSourcei(channel->src, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED) stop(*channel);
        }
    }
};

// Plays the World's cues (cues.hpp) that are due, by their distance from
// the listener (Audio::play_world).
inline void play_cues(Cues& cues, Audio& audio, float listener_x, float listener_y, d2d::rules::Rng& rng, std::uint32_t now_ms) {
    std::erase_if(cues.due, [&](const Cues::Cue& cue) {
        if (now_ms < cue.when_ms) return false;
        if (cue.hush && audio.speaking(cue.unit)) return true;
        audio.play_world(*cues.game_data, cue.sound, rng(16), cue.x, cue.y, listener_x, listener_y, cue.owner, cue.unit);
        return true;
    });
}

}  // namespace d2d::client
