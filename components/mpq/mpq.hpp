// D2Decomp MPQ reader — thin RAII wrapper over StormLib.
//
// Two types: Archive (one .mpq) and Stack (search across many in push order,
// first-match wins). Priority ordering matches the D2 model documented in
// research: patch archives pushed first, base archives after.
//
// Header-only. No exceptions on missing files — use try_read; read() throws
// only when the file exists but the read itself fails (I/O error, decompress
// failure). Constructor throws when the archive won't open at all.
//
// ponytail: no listfile enumeration API here yet. Add when the first caller
// needs it — StormLib's SFileFindFirstFile/Next is straightforward.
#pragma once

#include <StormLib.h>

#include "bnpatch.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::mpq {

// A file opened for streaming reads (videos: the D2 intro is 94 MB).
// Only for plain archive files, not installer entries.
class File {
public:
    File() = default;
    explicit File(HANDLE f) : f_(f) {}
    File(File&& o) noexcept : f_(std::exchange(o.f_, nullptr)) {}
    File& operator=(File&& o) noexcept { if (this != &o) { close(); f_ = std::exchange(o.f_, nullptr); } return *this; }
    ~File() { close(); }
    [[nodiscard]] std::uint64_t size() const {
        DWORD hi = 0;
        const DWORD lo = SFileGetFileSize(f_, &hi);
        return std::uint64_t(hi) << 32 | lo;
    }
    // Bytes read (0 at the end).
    std::size_t read(void* buf, std::size_t n) {
        DWORD got = 0;
        SFileReadFile(f_, buf, DWORD(n), &got, nullptr);
        return got;
    }
    // Absolute position; returns the new one.
    std::uint64_t seek(std::uint64_t pos) {
        LONG hi = LONG(pos >> 32);
        return SFileSetFilePointer(f_, LONG(pos & 0xffffffffu), &hi, FILE_BEGIN) | std::uint64_t(DWORD(hi)) << 32;
    }
private:
    void close() { if (f_) SFileCloseFile(f_); f_ = nullptr; }
    HANDLE f_ = nullptr;
};

class Archive {
public:
    explicit Archive(const std::filesystem::path& path) {
        // StormLib's TCHAR is wchar_t on Windows when UNICODE is defined, char otherwise.
        if (!SFileOpenArchive(path.string<TCHAR>().c_str(), 0,
                              MPQ_OPEN_READ_ONLY | STREAM_FLAG_READ_ONLY,
                              &h_)) {
            throw std::runtime_error("MPQ open failed: " + path.string());
        }
    }
    ~Archive() { if (h_) SFileCloseArchive(h_); }

    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    Archive(Archive&& other) noexcept
        : h_(std::exchange(other.h_, nullptr)), remap_(std::move(other.remap_)) {}

    Archive& operator=(Archive&& other) noexcept {
        if (this != &other) {
            if (h_) SFileCloseArchive(h_);
            h_ = std::exchange(other.h_, nullptr);
            remap_ = std::move(other.remap_);
        }
        return *this;
    }

    // A Blizzard patch installer (LODPatch_114d.exe: an MPQ appended to an
    // exe) read as the patch_d2.mpq it would install. Files sit flat in the
    // archive; patch.lst maps game paths to them ("data\global\excel\
    // armor.txt;armor.txt;0x0", ENG patchstring.tbl = patchstring~01.tbl)
    // and each carries a 24-byte header:
    //   u16 size (24), u8 4, u8 stored (1 = raw, 0 = delta),
    //   u32 CRC-32 of the base file, u32 base size, u32 output size,
    //   u64 filetime.
    // Raw entries (stored = 1) come back directly; the rest are BNUpdate
    // deltas against the same file in the base MPQs, which Stack applies
    // (bnpatch.hpp) — Archive alone can't see the base.
    [[nodiscard]] static Archive installer(const std::filesystem::path& path) {
        Archive a(path);
        const auto lst = a.try_read("patch.lst");
        if (!lst) throw std::runtime_error("not a patch installer: " + path.string());
        std::string_view all(reinterpret_cast<const char*>(lst->data()), lst->size());
        while (!all.empty()) {
            auto line = all.substr(0, all.find('\n'));
            all.remove_prefix(std::min(all.size(), line.size() + 1));
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            const auto a1 = line.find(';');
            if (a1 == line.npos) continue;
            const auto a2 = line.find(';', a1 + 1);
            a.remap_.emplace(key(line.substr(0, a1)),
                             std::string(line.substr(a1 + 1, a2 == line.npos ? a2 : a2 - a1 - 1)));
        }
        return a;
    }

    [[nodiscard]] bool is_installer() const noexcept { return !remap_.empty(); }

    // Installer only: the entry for a game path, 24-byte header included.
    [[nodiscard]] std::optional<std::vector<std::byte>>
    installer_entry(std::string_view name) const {
        const auto it = remap_.find(key(name));
        if (it == remap_.end()) return std::nullopt;
        return read_raw(it->second);
    }

    [[nodiscard]] bool contains(std::string_view name) const {
        if (!remap_.empty()) return try_read(name).has_value();
        return SFileHasFile(h_, std::string(name).c_str());
    }

    // Returns the decompressed file bytes. Throws if the file is missing OR
    // the read fails — callers who tolerate absence should use try_read.
    [[nodiscard]] std::vector<std::byte> read(std::string_view name) const {
        auto data = try_read(name);
        if (!data) throw std::runtime_error("MPQ file not found: " + std::string(name));
        return std::move(*data);
    }

    [[nodiscard]] std::optional<std::vector<std::byte>>
    try_read(std::string_view name) const {
        if (!remap_.empty()) {
            const auto it = remap_.find(key(name));
            if (it == remap_.end()) return std::nullopt;
            auto raw = read_raw(it->second);
            if (!raw || raw->size() < 24) return std::nullopt;
            const auto u8 = [&](std::size_t o) { return std::uint8_t((*raw)[o]); };
            const std::uint32_t len = u8(12) | u8(13) << 8 | u8(14) << 16 | std::uint32_t(u8(15)) << 24;
            if ((u8(0) | u8(1) << 8) != 24 || u8(3) != 1 || 24 + std::size_t(len) > raw->size())
                return std::nullopt;   // compressed entry: not decodable yet
            return std::vector<std::byte>(raw->begin() + 24, raw->begin() + 24 + len);
        }
        return read_raw(name);
    }

    [[nodiscard]] std::optional<File> open(std::string_view name) const {
        if (!remap_.empty()) return std::nullopt;
        HANDLE f{};
        if (!SFileOpenFileEx(h_, std::string(name).c_str(), 0, &f)) return std::nullopt;
        return File(f);
    }

private:
    // Case/slash-insensitive lookup key, like MPQ name hashing.
    static std::string key(std::string_view name) {
        std::string k(name);
        for (auto& c : k) c = c == '/' ? '\\' : char(std::tolower((unsigned char)c));
        return k;
    }

    [[nodiscard]] std::optional<std::vector<std::byte>>
    read_raw(std::string_view name) const {
        const std::string namez(name);
        HANDLE f{};
        if (!SFileOpenFileEx(h_, namez.c_str(), 0, &f)) return std::nullopt;
        const DWORD size = SFileGetFileSize(f, nullptr);
        std::vector<std::byte> buf(size);
        DWORD got = 0;
        const bool ok = SFileReadFile(f, buf.data(), size, &got, nullptr)
                     && got == size;
        SFileCloseFile(f);
        if (!ok) throw std::runtime_error("MPQ read failed: " + namez);
        return buf;
    }

    HANDLE h_ = nullptr;
    std::unordered_map<std::string, std::string> remap_;   // installer only
};

class Stack {
public:
    // Highest priority first. D2 pushes patch_d2.mpq before base archives.
    void push(const std::filesystem::path& p) { archives_.emplace_back(p); }
    void push_installer(const std::filesystem::path& p) {
        archives_.push_back(Archive::installer(p));
    }

    [[nodiscard]] bool empty() const noexcept { return archives_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return archives_.size(); }

    [[nodiscard]] bool contains(std::string_view name) const {
        for (const auto& a : archives_) if (a.contains(name)) return true;
        return false;
    }

    [[nodiscard]] std::vector<std::byte> read(std::string_view name) const {
        auto data = try_read(name);
        if (!data) throw std::runtime_error("MPQ stack: file not found: "
                                            + std::string(name));
        return std::move(*data);
    }

    // First archive that has the file wins. An installer's delta entry is
    // applied to the file from the archives below it; when that base
    // doesn't match (CRC/size), the lookup falls through to the base.
    [[nodiscard]] std::optional<std::vector<std::byte>>
    try_read(std::string_view name) const {
        for (std::size_t i = 0; i < archives_.size(); ++i) {
            const auto& a = archives_[i];
            if (auto data = a.try_read(name)) return data;
            if (!a.is_installer()) continue;
            const auto entry = a.installer_entry(name);
            if (!entry) continue;
            for (std::size_t j = i + 1; j < archives_.size(); ++j)
                if (auto base = archives_[j].try_read(name)) {
                    if (auto out = bnpatch::apply(*entry, *base)) return out;
                    break;
                }
        }
        return std::nullopt;
    }

    // First plain archive that has the file, for streaming.
    [[nodiscard]] std::optional<File> open(std::string_view name) const {
        for (const auto& a : archives_)
            if (auto f = a.open(name)) return f;
        return std::nullopt;
    }

private:
    std::vector<Archive> archives_;
};

}  // namespace d2d::mpq
