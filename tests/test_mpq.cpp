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
#include <filesystem>
#include <string>

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
        Archive a(d2char);
        assert(a.contains("(listfile)"));
        const auto listfile = a.read("(listfile)");
        assert(!listfile.empty());

        assert(!a.contains("this/file/does/not/exist"));
        assert(!a.try_read("nope").has_value());

        // A real D2 file we spotted in the archive listing.
        const auto cof = a.read(R"(data\global\CHARS\NE\COF\NEWL1HT.COF)");
        assert(cof.size() == 1269);
    }

    // Move construction leaves the source safely closable.
    {
        Archive a(d2char);
        Archive b(std::move(a));
        assert(b.contains("(listfile)"));
    }

    // Stack: two archives, first-added wins.
    if (fs::exists(dir / "d2data.mpq")) {
        Stack s;
        s.push(d2char);
        s.push(dir / "d2data.mpq");
        assert(s.size() == 2);
        assert(s.contains("(listfile)"));   // both have it; d2char answers first
        const auto data = s.read("(listfile)");
        assert(!data.empty());
    }

    std::printf("OK\n");
    return 0;
}
