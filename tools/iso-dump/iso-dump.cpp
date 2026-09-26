// iso-dump — list or extract an ISO 9660 image using components/iso9660.
//
// usage:
//   iso-dump list <iso>
//   iso-dump extract <iso> <dest-dir>

#include <iso9660.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "usage:\n"
            "  %s list <iso>\n"
            "  %s extract <iso> <dest-dir>\n", argv[0], argv[0]);
        return 2;
    }
    namespace fs = std::filesystem;
    const std::string cmd = argv[1];
    const fs::path iso = argv[2];

    auto r = iso9660::Reader::open(iso);
    if (!r) { std::fprintf(stderr, "cannot open %s\n", iso.string().c_str()); return 1; }

    if (cmd == "list") {
        for (const auto& e : r->entries()) {
            std::printf("%c %10u  %s\n",
                        e.directory ? 'd' : '-', e.size, e.path.c_str());
        }
        return 0;
    }
    if (cmd == "extract") {
        if (argc < 4) { std::fprintf(stderr, "missing dest-dir\n"); return 2; }
        const fs::path dest = argv[3];
        std::error_code ec;
        fs::create_directories(dest, ec);
        std::uint64_t total = 0;
        std::size_t count = 0;
        for (const auto& e : r->entries()) {
            const fs::path out = dest / e.path.substr(1);
            if (e.directory) { fs::create_directories(out, ec); continue; }
            fs::create_directories(out.parent_path(), ec);
            auto bytes = r->read(e);
            std::ofstream(out, std::ios::binary).write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            total += bytes.size();
            ++count;
        }
        std::printf("extracted %zu files, %llu bytes to %s\n",
                    count, (unsigned long long)total, dest.string().c_str());
        return 0;
    }
    std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
    return 2;
}
