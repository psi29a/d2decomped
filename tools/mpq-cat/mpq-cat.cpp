// SPDX-License-Identifier: GPL-3.0-or-later
// mpq-cat <mpq dir> <file>... — write files from the game's MPQ stack to
// stdout, in d2d's order (1.14d patch via $D2_PATCH_INSTALLER, then
// d2exp, d2data, d2char, sounds). For reading excel tables while RE'ing.
// mpq-cat -d <out dir> [-n <bytes>] <mpq dir> <file>... writes each to
// <out dir>/<file> instead (its first <bytes> only), skipping missing ones:
// many files' headers in one run (tools/anim_coverage.py --audit).
// mpq-cat -p <out.mpq> <mpq dir> builds patch_d2.mpq from $D2_PATCH_INSTALLER
// (its patch.lst's data files, deltas applied), as the installer would:
// game.exe 1.14d under Wine needs it (Makefile `wine`).
#include <mpq.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

int main(int argc, char** argv) try {
    std::filesystem::path out_dir, patch_out;
    std::size_t head = 0;
    for (; argc > 2 && (std::string(argv[1]) == "-d" || std::string(argv[1]) == "-n" || std::string(argv[1]) == "-p"); argc -= 2, argv += 2) {
        if (argv[2][0] == '-') { std::fprintf(stderr, "mpq-cat: %s needs a value, not %s\n", argv[1], argv[2]); return 2; }
        if (std::string(argv[1]) == "-d") out_dir = argv[2];
        else if (std::string(argv[1]) == "-p") patch_out = argv[2];
        else head = std::stoul(argv[2]);
    }
    if (argc < (patch_out.empty() ? 3 : 2)) { std::fprintf(stderr, "usage: mpq-cat [-d <out dir> [-n <bytes>]] <mpq dir> <file>...\n"); return 2; }
    const std::filesystem::path dir = argv[1];
    d2d::mpq::Stack mpqs;
    if (std::filesystem::exists(dir / "patch_d2.mpq")) mpqs.push(dir / "patch_d2.mpq");
    else if (const char* patch = std::getenv("D2_PATCH_INSTALLER")) mpqs.push_installer(patch);
    for (const char* name : { "d2exp.mpq", "d2data.mpq", "d2char.mpq", "d2xtalk.mpq", "d2speech.mpq", "d2sfx.mpq", "d2xmusic.mpq", "d2music.mpq" })
        if (std::filesystem::exists(dir / name)) mpqs.push(dir / name);
    if (!patch_out.empty()) {
        const char* installer = std::getenv("D2_PATCH_INSTALLER");
        if (!installer) { std::fprintf(stderr, "mpq-cat: -p needs $D2_PATCH_INSTALLER\n"); return 2; }
        const auto list = d2d::mpq::Archive(installer).read("patch.lst");
        std::string_view all(reinterpret_cast<const char*>(list.data()), list.size());
        std::vector<std::pair<std::string, std::vector<std::byte>>> files;
        while (!all.empty()) {
            const auto line = all.substr(0, all.find('\n'));
            all.remove_prefix(std::min(all.size(), line.size() + 1));
            if (!line.starts_with("data\\") && !line.starts_with("Data\\")) continue;   // Game.exe and the DLLs go beside it, not in it
            std::string name(line.substr(0, line.find(';')));
            auto bytes = mpqs.try_read(name);
            if (!bytes) { std::fprintf(stderr, "mpq-cat: %s not readable from the installer\n", name.c_str()); return 1; }
            files.emplace_back(std::move(name), std::move(*bytes));
        }
        std::filesystem::remove(patch_out);
        HANDLE archive = nullptr;
        if (!SFileCreateArchive(patch_out.string().c_str(), MPQ_CREATE_LISTFILE | MPQ_CREATE_ARCHIVE_V1, DWORD(files.size() + 16), &archive)) {
            std::fprintf(stderr, "mpq-cat: can't create %s\n", patch_out.string().c_str());
            return 1;
        }
        for (const auto& [name, bytes] : files) {
            HANDLE file = nullptr;
            const bool ok = SFileCreateFile(archive, name.c_str(), 0, DWORD(bytes.size()), 0, MPQ_FILE_COMPRESS | MPQ_FILE_REPLACEEXISTING, &file)
                && SFileWriteFile(file, bytes.data(), DWORD(bytes.size()), MPQ_COMPRESSION_ZLIB) && SFileFinishFile(file);
            if (!ok) {
                std::fprintf(stderr, "mpq-cat: can't add %s\n", name.c_str());
                SFileCloseArchive(archive);
                std::filesystem::remove(patch_out);   // no half archive for `make run og` to find
                return 1;
            }
        }
        if (!SFileCloseArchive(archive)) {   // writes the hash and block tables
            std::fprintf(stderr, "mpq-cat: can't finish %s\n", patch_out.string().c_str());
            std::filesystem::remove(patch_out);
            return 1;
        }
        std::printf("%s: %zu files\n", patch_out.string().c_str(), files.size());
        return 0;
    }
    for (int i = 2; i < argc; ++i) {
        std::string name = argv[i];
        for (auto& letter : name) if (letter == '\\') letter = '/';
        const std::filesystem::path relative = name;   // under <out dir> only: no root, no ".."
        if (!out_dir.empty() && (relative.has_root_path() || std::find(relative.begin(), relative.end(), std::filesystem::path("..")) != relative.end())) {
            std::fprintf(stderr, "mpq-cat: %s would land outside %s\n", argv[i], out_dir.string().c_str());
            return 1;
        }
        const auto bytes = mpqs.try_read(argv[i]);
        if (!bytes && !out_dir.empty()) continue;
        if (!bytes) { std::fprintf(stderr, "mpq-cat: %s not found\n", argv[i]); return 3; }
        if (out_dir.empty()) { std::fwrite(bytes->data(), 1, bytes->size(), stdout); continue; }
        const auto path = out_dir / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes->data()), std::streamsize(head && head < bytes->size() ? head : bytes->size()));
        out.close();
        if (!out) { std::fprintf(stderr, "mpq-cat: can't write %s\n", path.string().c_str()); return 1; }   // not a missing file: the audit would read it as one
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
