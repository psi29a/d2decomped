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
    explicit File(HANDLE file) : file_(file) {}
    File(File&& other) noexcept : file_(std::exchange(other.file_, nullptr)) {}
    File& operator=(File&& other) noexcept { if (this != &other) { close(); file_ = std::exchange(other.file_, nullptr); } return *this; }
    ~File() { close(); }
    [[nodiscard]] std::uint64_t size() const {
        DWORD high = 0;
        const DWORD low = SFileGetFileSize(file_, &high);
        return std::uint64_t(high) << 32 | low;
    }
    // Bytes read (0 at the end).
    std::size_t read(void* buf, std::size_t count) {
        DWORD got = 0;
        SFileReadFile(file_, buf, DWORD(count), &got, nullptr);
        return got;
    }
    // Absolute position; returns the new one.
    std::uint64_t seek(std::uint64_t pos) {
        LONG high = LONG(pos >> 32);
        return SFileSetFilePointer(file_, LONG(pos & 0xffffffffu), &high, FILE_BEGIN) | std::uint64_t(DWORD(high)) << 32;
    }
private:
    void close() { if (file_) SFileCloseFile(file_); file_ = nullptr; }
    HANDLE file_ = nullptr;
};

class Archive {
public:
    explicit Archive(const std::filesystem::path& path) {
        // StormLib's TCHAR is wchar_t on Windows when UNICODE is defined, char otherwise.
        if (!SFileOpenArchive(path.string<TCHAR>().c_str(), 0,
                              MPQ_OPEN_READ_ONLY | STREAM_FLAG_READ_ONLY,
                              &handle_)) {
            throw std::runtime_error("MPQ open failed: " + path.string());
        }
    }
    ~Archive() { if (handle_) SFileCloseArchive(handle_); }

    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    Archive(Archive&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)), remap_(std::move(other.remap_)) {}

    Archive& operator=(Archive&& other) noexcept {
        if (this != &other) {
            if (handle_) SFileCloseArchive(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
            remap_ = std::move(other.remap_);
        }
        return *this;
    }

    // A Blizzard patch installer (LODPatch_114d.exe: an MPQ appended to an
    // exe) read as the patch_d2.mpq it would install. Files sit flat in the
    // archive; patch.lst maps game paths to them
    // ("data\global\excel\armor.txt;armor.txt;0x0", ENG patchstring.tbl =
    // patchstring~01.tbl)
    // and each carries a 24-byte header:
    //   u16 size (24), u8 4, u8 stored (1 = raw, 0 = delta),
    //   u32 CRC-32 of the base file, u32 base size, u32 output size,
    //   u64 filetime.
    // Raw entries (stored = 1) come back directly; the rest are BNUpdate
    // deltas against the same file in the base MPQs, which Stack applies
    // (bnpatch.hpp) — Archive alone can't see the base.
    [[nodiscard]] static Archive installer(const std::filesystem::path& path) {
        Archive archive(path);
        const auto lst = archive.try_read("patch.lst");
        if (!lst) throw std::runtime_error("not a patch installer: " + path.string());
        std::string_view all(reinterpret_cast<const char*>(lst->data()), lst->size());
        while (!all.empty()) {
            auto line = all.substr(0, all.find('\n'));
            all.remove_prefix(std::min(all.size(), line.size() + 1));
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            const auto first_semicolon = line.find(';');
            if (first_semicolon == line.npos) continue;
            const auto second_semicolon = line.find(';', first_semicolon + 1);
            archive.remap_.emplace(key(line.substr(0, first_semicolon)),
                             std::string(line.substr(first_semicolon + 1, second_semicolon == line.npos ? second_semicolon : second_semicolon - first_semicolon - 1)));
        }
        return archive;
    }

    [[nodiscard]] bool is_installer() const noexcept { return !remap_.empty(); }

    // Installer only: the entry for a game path, 24-byte header included.
    [[nodiscard]] std::optional<std::vector<std::byte>>
    installer_entry(std::string_view name) const {
        const auto found = remap_.find(key(name));
        if (found == remap_.end()) return std::nullopt;
        return read_raw(found->second);
    }

    [[nodiscard]] bool contains(std::string_view name) const {
        if (!remap_.empty()) return try_read(name).has_value();
        return SFileHasFile(handle_, std::string(name).c_str());
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
            const auto found = remap_.find(key(name));
            if (found == remap_.end()) return std::nullopt;
            auto raw = read_raw(found->second);
            if (!raw || raw->size() < 24) return std::nullopt;
            const auto byte_at = [&](std::size_t offset) { return std::uint8_t((*raw)[offset]); };
            const std::uint32_t len = byte_at(12) | byte_at(13) << 8 | byte_at(14) << 16 | std::uint32_t(byte_at(15)) << 24;
            if ((byte_at(0) | byte_at(1) << 8) != 24 || byte_at(3) != 1 || 24 + std::size_t(len) > raw->size())
                return std::nullopt;   // compressed entry: not decodable yet
            return std::vector<std::byte>(raw->begin() + 24, raw->begin() + 24 + len);
        }
        return read_raw(name);
    }

    [[nodiscard]] std::optional<File> open(std::string_view name) const {
        if (!remap_.empty()) return std::nullopt;
        HANDLE file{};
        if (!SFileOpenFileEx(handle_, std::string(name).c_str(), 0, &file)) return std::nullopt;
        return File(file);
    }

private:
    // Case/slash-insensitive lookup key, like MPQ name hashing.
    static std::string key(std::string_view name) {
        std::string normalised(name);
        for (auto& letter : normalised) letter = letter == '/' ? '\\' : char(std::tolower((unsigned char)letter));
        return normalised;
    }

    [[nodiscard]] std::optional<std::vector<std::byte>>
    read_raw(std::string_view name) const {
        const std::string namez(name);
        HANDLE file{};
        if (!SFileOpenFileEx(handle_, namez.c_str(), 0, &file)) return std::nullopt;
        const DWORD size = SFileGetFileSize(file, nullptr);
        std::vector<std::byte> buf(size);
        DWORD got = 0;
        const bool ok = SFileReadFile(file, buf.data(), size, &got, nullptr)
                     && got == size;
        SFileCloseFile(file);
        if (!ok) throw std::runtime_error("MPQ read failed: " + namez);
        return buf;
    }

    HANDLE handle_ = nullptr;
    std::unordered_map<std::string, std::string> remap_;   // installer only
};

class Stack {
public:
    // Highest priority first. D2 pushes patch_d2.mpq before base archives.
    void push(const std::filesystem::path& path) { archives_.emplace_back(path); sources_.emplace_back(path, false); }
    void push_installer(const std::filesystem::path& path) {
        archives_.push_back(Archive::installer(path));
        sources_.emplace_back(path, true);
    }
    // The same archives opened again: handles for another thread
    // (StormLib's aren't shared across threads).
    [[nodiscard]] Stack reopen() const {
        Stack copy;
        for (const auto& [path, inst] : sources_) inst ? copy.push_installer(path) : copy.push(path);
        return copy;
    }

    [[nodiscard]] bool empty() const noexcept { return archives_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return archives_.size(); }

    [[nodiscard]] bool contains(std::string_view name) const {
        for (const auto& archive : archives_) if (archive.contains(name)) return true;
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
            const auto& archive = archives_[i];
            if (auto data = archive.try_read(name)) return data;
            if (!archive.is_installer()) continue;
            const auto entry = archive.installer_entry(name);
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
        for (const auto& archive : archives_)
            if (auto file = archive.open(name)) return file;
        return std::nullopt;
    }

private:
    std::vector<Archive> archives_;
    std::vector<std::pair<std::filesystem::path, bool>> sources_;   // what was pushed: path, installer
};

}  // namespace d2d::mpq
