// mpq-cat <mpq dir> <file>... — write files from the game's MPQ stack to
// stdout, in d2d's order (1.14d patch via $D2_PATCH_INSTALLER, then
// d2exp, d2data, d2char, sounds). For reading excel tables while RE'ing.
#include <mpq.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: mpq-cat <mpq dir> <file>...\n"); return 2; }
    const std::filesystem::path dir = argv[1];
    d2d::mpq::Stack mpqs;
    if (std::filesystem::exists(dir / "patch_d2.mpq")) mpqs.push(dir / "patch_d2.mpq");
    else if (const char* p = std::getenv("D2_PATCH_INSTALLER")) mpqs.push_installer(p);
    for (const char* n : { "d2exp.mpq", "d2data.mpq", "d2char.mpq", "d2xtalk.mpq", "d2speech.mpq", "d2sfx.mpq", "d2xmusic.mpq", "d2music.mpq" })
        if (std::filesystem::exists(dir / n)) mpqs.push(dir / n);
    for (int i = 2; i < argc; ++i) {
        const auto b = mpqs.try_read(argv[i]);
        if (!b) { std::fprintf(stderr, "mpq-cat: %s not found\n", argv[i]); return 1; }
        std::fwrite(b->data(), 1, b->size(), stdout);
    }
}
