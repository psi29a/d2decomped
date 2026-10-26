// SPDX-License-Identifier: GPL-3.0-or-later
// mpq-cat <mpq dir> <file>... — write files from the game's MPQ stack to
// stdout, in d2d's order (1.14d patch via $D2_PATCH_INSTALLER, then
// d2exp, d2data, d2char, sounds). For reading excel tables while RE'ing.
// mpq-cat -d <out dir> [-n <bytes>] <mpq dir> <file>... writes each to
// <out dir>/<file> instead (its first <bytes> only), skipping missing ones:
// many files' headers in one run (tools/anim_coverage.py --audit).
#include <mpq.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

int main(int argc, char** argv) try {
    std::filesystem::path out_dir;
    std::size_t head = 0;
    for (; argc > 2 && (std::string(argv[1]) == "-d" || std::string(argv[1]) == "-n"); argc -= 2, argv += 2) {
        if (argv[2][0] == '-') { std::fprintf(stderr, "mpq-cat: %s needs a value, not %s\n", argv[1], argv[2]); return 2; }
        if (std::string(argv[1]) == "-d") out_dir = argv[2]; else head = std::stoul(argv[2]);
    }
    if (argc < 3) { std::fprintf(stderr, "usage: mpq-cat [-d <out dir> [-n <bytes>]] <mpq dir> <file>...\n"); return 2; }
    const std::filesystem::path dir = argv[1];
    d2d::mpq::Stack mpqs;
    if (std::filesystem::exists(dir / "patch_d2.mpq")) mpqs.push(dir / "patch_d2.mpq");
    else if (const char* patch = std::getenv("D2_PATCH_INSTALLER")) mpqs.push_installer(patch);
    for (const char* name : { "d2exp.mpq", "d2data.mpq", "d2char.mpq", "d2xtalk.mpq", "d2speech.mpq", "d2sfx.mpq", "d2xmusic.mpq", "d2music.mpq" })
        if (std::filesystem::exists(dir / name)) mpqs.push(dir / name);
    for (int i = 2; i < argc; ++i) {
        const auto bytes = mpqs.try_read(argv[i]);
        if (!bytes && !out_dir.empty()) continue;
        if (!bytes) { std::fprintf(stderr, "mpq-cat: %s not found\n", argv[i]); return 1; }
        if (out_dir.empty()) { std::fwrite(bytes->data(), 1, bytes->size(), stdout); continue; }
        std::string name = argv[i];
        for (auto& letter : name) if (letter == '\\') letter = '/';
        const auto path = out_dir / name;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes->data()), std::streamsize(head && head < bytes->size() ? head : bytes->size()));
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
