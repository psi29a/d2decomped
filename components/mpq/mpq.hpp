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

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::mpq {

class Archive {
public:
    explicit Archive(const std::filesystem::path& path) {
        if (!SFileOpenArchive(path.string().c_str(), 0,
                              MPQ_OPEN_READ_ONLY | STREAM_FLAG_READ_ONLY,
                              &h_)) {
            throw std::runtime_error("MPQ open failed: " + path.string());
        }
    }
    ~Archive() { if (h_) SFileCloseArchive(h_); }

    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    Archive(Archive&& other) noexcept
        : h_(std::exchange(other.h_, nullptr)) {}

    Archive& operator=(Archive&& other) noexcept {
        if (this != &other) {
            if (h_) SFileCloseArchive(h_);
            h_ = std::exchange(other.h_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] bool contains(std::string_view name) const {
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

private:
    HANDLE h_ = nullptr;
};

class Stack {
public:
    // Highest priority first. D2 pushes patch_d2.mpq before base archives.
    void push(const std::filesystem::path& p) { archives_.emplace_back(p); }

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

    [[nodiscard]] std::optional<std::vector<std::byte>>
    try_read(std::string_view name) const {
        for (const auto& a : archives_) {
            if (auto data = a.try_read(name)) return data;
        }
        return std::nullopt;
    }

private:
    std::vector<Archive> archives_;
};

}  // namespace d2d::mpq
