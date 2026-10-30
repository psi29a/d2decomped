// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the MPQ wrapper against real D2 archives if available.
// Skips (D2D_TEST_SKIPPED) without game data; the installer tests also create a
// small synthetic base MPQ to exercise a failed delta.
//
// D2_MPQ_DIR must contain at least d2char.mpq; d2data.mpq is a bonus that
// exercises the Stack. QSettings-picked default lets the plain developer
// setup ("run the launcher, install, run this") work with zero env fuss.
#include <log.hpp>
#include <mpq.hpp>
#include <StormLib.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;

int main() try {
    const char* env = std::getenv("D2_MPQ_DIR");
    const fs::path dir = env ? env
        // Match the launcher's default install location on this dev box.
        : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "")
            / "Workspace/private/diablo2";

    const auto d2char = dir / "d2char.mpq";
    if (!fs::exists(d2char)) {
        std::printf("SKIP: %s not found (set D2_MPQ_DIR to run)\n",
                    d2char.string().c_str());
        return D2D_TEST_SKIPPED;
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
        bool open_error = false;
        try { (void)archive.try_read(""); } catch (const std::exception&) { open_error = true; }
        assert(open_error);  // StormLib rejects an empty name; it is not a missing entry.

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

    // A corrupt entry must not be mistaken for a missing file. Create a
    // small archive, then write an impossible uncompressed size into its
    // block table through StormLib's archive structures.
    {
        const auto scratch = fs::temp_directory_path() / ("d2d_corrupt_mpq_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assert(fs::create_directory(scratch));
        const auto path = scratch / "corrupt.mpq";
        HANDLE raw_archive = nullptr;
        assert(SFileCreateArchive(path.string<TCHAR>().c_str(), MPQ_CREATE_ARCHIVE_V1, 16, &raw_archive));
        HANDLE raw_file = nullptr;
        constexpr char contents[] = "data";
        assert(SFileCreateFile(raw_archive, "broken.txt", 0, sizeof(contents), 0, 0, &raw_file));
        assert(SFileWriteFile(raw_file, contents, sizeof(contents), 0));
        assert(SFileFinishFile(raw_file));
        assert(SFileOpenFileEx(raw_archive, "broken.txt", 0, &raw_file));
        static_cast<TMPQFile*>(raw_file)->pFileEntry->dwFileSize = 0x7fffffff;
        static_cast<TMPQArchive*>(raw_archive)->dwFlags |= MPQ_FLAG_CHANGED;
        assert(SFileCloseFile(raw_file));
        assert(SFileCloseArchive(raw_archive));
        {
            Stack stack;
            stack.push(path);
            assert(!stack.try_read("missing.txt") && !stack.failed("missing.txt"));
            assert(!stack.try_read("broken.txt") && stack.failed("broken.txt"));
        }
        fs::remove_all(scratch);
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
        assert(!mpqs.try_read("") && mpqs.failed(""));
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
        // A listed delta with an incompatible base may fall back during play,
        // but patch generation must never publish the unpatched base as 1.14d.
        {
            const auto scratch = fs::temp_directory_path() / ("d2d_test_mpq_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            assert(fs::create_directory(scratch));
            const auto wrong_base = scratch / "wrong_base.mpq";
            HANDLE archive = nullptr;
            assert(SFileCreateArchive(wrong_base.string<TCHAR>().c_str(), MPQ_CREATE_ARCHIVE_V1, 16, &archive));
            HANDLE file = nullptr;
            constexpr char contents[] = "not the expected base";
            assert(SFileCreateFile(archive, R"(data\global\excel\armor.txt)", 0, sizeof(contents), 0, 0, &file));
            assert(SFileWriteFile(file, contents, sizeof(contents), 0));
            assert(SFileFinishFile(file));
            assert(SFileCloseArchive(archive));
            {
                Stack permissive;
                permissive.push_installer(inst);
                permissive.push(wrong_base);
                const auto fallback = permissive.try_read(R"(data\global\excel\armor.txt)");
                assert(fallback && fallback->size() == sizeof(contents));
                Stack strict;
                strict.push_installer(inst);
                strict.push(wrong_base);
                assert(!strict.try_read(R"(data\global\excel\armor.txt)", true));
                assert(strict.failed(R"(data\global\excel\armor.txt)"));
            }
            fs::remove_all(scratch);
        }
        // With the base MPQs underneath, deltas are applied: 1.14d armor.txt
        // (76370 bytes, "namestr" column) from the CD's.
        if (fs::exists(dir / "d2exp.mpq")) {
            mpqs.push(dir / "d2exp.mpq");
            mpqs.push(dir / "d2data.mpq");
            const auto armor = mpqs.try_read(R"(data\global\excel\armor.txt)");
            assert(armor && armor->size() == 76370);
            assert(mpqs.try_read(R"(data\global\excel\armor.txt)", true) == armor);
            const std::string head(reinterpret_cast<const char*>(armor->data()), 400);
            assert(head.find("\tnamestr\t") != std::string::npos);
            std::printf("installer delta applied: armor.txt %zu bytes\n", armor->size());
        }
        assert(!mpqs.contains("patch.lst"));                             // not a game path
        std::printf("installer layer OK\n");
    } else {
        std::printf("SKIP installer: %s not found\n", inst.string().c_str());
    }

    // verify: every file of an intact archive reads; junk written over a
    // copy's middle shows up as damaged files.
    if (const auto sfx = dir / "d2sfx.mpq"; fs::exists(sfx)) {
        const auto keep_going = [](std::uint64_t) { return true; };
        const auto intact = verify(sfx, keep_going);
        assert(intact.files > 2000 && intact.damaged.empty());
        const auto copy = fs::temp_directory_path() / "test_mpq_damaged.mpq";
        fs::copy_file(sfx, copy, fs::copy_options::overwrite_existing);
        {
            std::fstream file(copy, std::ios::in | std::ios::out | std::ios::binary);
            file.seekp(std::streamoff(fs::file_size(copy) / 2));
            const std::string junk(1 << 16, '\xff');
            file.write(junk.data(), std::streamsize(junk.size()));
        }
        const auto damaged = verify(copy, keep_going);
        assert(damaged.files == intact.files && !damaged.damaged.empty());
        // A Stack reads a damaged file as missing, with one warning however
        // often it's asked for (a looping sound asks every play).
        {
            static int warnings = 0;
            d2d::log::set_sink([](d2d::log::Level, std::string_view) { ++warnings; });
            Stack mpqs;
            mpqs.push(copy);
            assert(!mpqs.try_read(damaged.damaged.front()) && !mpqs.try_read(damaged.damaged.front()));
            assert(warnings == 1);
            d2d::log::set_sink(nullptr);
        }
        fs::remove(copy);
        std::printf("verify: %zu files, %zu damaged in the junked copy\n", intact.files, damaged.damaged.size());
    }

    std::printf("OK\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
