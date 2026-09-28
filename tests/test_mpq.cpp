// Exercise the MPQ wrapper against real D2 archives if available.
// Skips (return 0) when D2_MPQ_DIR isn't set — the test can't fabricate a
// real MPQ and we don't want CI without game data to fail.
//
// D2_MPQ_DIR must contain at least d2char.mpq; d2data.mpq is a bonus that
// exercises the Stack. QSettings-picked default lets the plain developer
// setup ("run the launcher, install, run this") work with zero env fuss.
#include <mpq.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

namespace fs = std::filesystem;

int main() {
    const char* env = std::getenv("D2_MPQ_DIR");
    const fs::path dir = env ? env
        // Match the launcher's default install location on this dev box.
        : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "")
            / "Workspace/private/diablo2";

    const auto d2char = dir / "d2char.mpq";
    if (!fs::exists(d2char)) {
        std::printf("SKIP: %s not found (set D2_MPQ_DIR to run)\n",
                    d2char.string().c_str());
        return 0;
    }

    using namespace d2d::mpq;

    // Archive: open, existence check, read.
    {
        Archive archive(d2char);
        assert(archive.contains("(listfile)"));
        const auto listfile = archive.read("(listfile)");
        assert(!listfile.empty());

        assert(!archive.contains("this/file/does/not/exist"));
        assert(!archive.try_read("nope").has_value());

        // A real D2 file we spotted in the archive listing.
        const auto cof = archive.read(R"(data\global\CHARS\NE\COF\NEWL1HT.COF)");
        assert(cof.size() == 1269);
    }

    // Move construction leaves the source safely closable.
    {
        Archive archive(d2char);
        Archive moved(std::move(archive));
        assert(moved.contains("(listfile)"));
    }

    // Stack: two archives, first-added wins.
    if (fs::exists(dir / "d2data.mpq")) {
        Stack mpqs;
        mpqs.push(d2char);
        mpqs.push(dir / "d2data.mpq");
        assert(mpqs.size() == 2);
        assert(mpqs.contains("(listfile)"));   // both have it; d2char answers first
        const auto data = mpqs.read("(listfile)");
        assert(!data.empty());
    }

    // 1.14d patch installer as the patch_d2 layer (D2_PATCH_INSTALLER, or
    // LODPatch_114d.exe next to the MPQs). Raw entries come back unwrapped
    // through patch.lst names; compressed ones fall through to lower layers.
    const char* patch_installer = std::getenv("D2_PATCH_INSTALLER");
    const fs::path inst = patch_installer ? fs::path(patch_installer) : dir / "LODPatch_114d.exe";
    if (fs::exists(inst)) {
        d2d::mpq::Stack mpqs;
        mpqs.push_installer(inst);
        const auto patch_strings = mpqs.try_read(R"(data\local\LNG\ENG\patchstring.tbl)");
        assert(patch_strings && patch_strings->size() > 1000);
        const auto compcode = mpqs.try_read("data/global/excel/COMPCODE.txt");   // any case/slash
        assert(compcode && std::memcmp(compcode->data(), "component\tcode", 14) == 0);
        assert(!mpqs.contains(R"(data\global\excel\armor.txt)"));      // delta, no base yet
        // With the base MPQs underneath, deltas are applied: 1.14d armor.txt
        // (76370 bytes, "namestr" column) from the CD's.
        if (fs::exists(dir / "d2exp.mpq")) {
            mpqs.push(dir / "d2exp.mpq");
            mpqs.push(dir / "d2data.mpq");
            const auto armor = mpqs.try_read(R"(data\global\excel\armor.txt)");
            assert(armor && armor->size() == 76370);
            const std::string head(reinterpret_cast<const char*>(armor->data()), 400);
            assert(head.find("\tnamestr\t") != std::string::npos);
            std::printf("installer delta applied: armor.txt %zu bytes\n", armor->size());
        }
        assert(!mpqs.contains("patch.lst"));                             // not a game path
        std::printf("installer layer OK\n");
    } else {
        std::printf("SKIP installer: %s not found\n", inst.string().c_str());
    }

    std::printf("OK\n");
    return 0;
}
