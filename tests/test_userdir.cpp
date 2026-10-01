// .cfg parsing + per-platform user dir shape.
#include <userdir.hpp>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

int main() {
    const auto dir = fs::temp_directory_path() / "d2d_test_userdir";
    fs::create_directories(dir);
    const auto first = dir / "a.cfg", second = dir / "b.cfg";
    std::ofstream(first) << "# comment\n\n  data = /opt/d2 lod  \nno equals sign\n"
                        "=novalue\nscale=2\r\nempty =\n";
    std::ofstream(second) << "scale = 3\n";

    d2d::userdir::Config cfg;
    d2d::userdir::load_cfg(first, cfg);
    assert(cfg.at("data") == "/opt/d2 lod");   // trimmed, inner space kept
    assert(cfg.at("scale") == "2");            // CRLF tolerated
    assert(cfg.at("empty").empty());
    assert(cfg.size() == 3);                   // comment / no '=' / empty key skipped

    d2d::userdir::load_cfg(second, cfg);            // later file wins
    assert(cfg.at("scale") == "3");
    d2d::userdir::load_cfg(dir / "missing.cfg", cfg);
    assert(cfg.size() == 3);

    // save_cfg: only the given keys change; other lines and comments stay.
    const auto written = dir / "sub" / "d2d.cfg";
    assert(d2d::userdir::save_cfg(written, { { "data", "/a" }, { "patch", "/p.exe" } }));   // creates sub/
    std::ofstream(written, std::ios::app) << "# mine\nscale = 2\ndata=/dup\n";
    assert(d2d::userdir::save_cfg(written, { { "data", "/b c" }, { "patch", "" } }));
    d2d::userdir::Config saved;
    d2d::userdir::load_cfg(written, saved);
    assert(saved.at("data") == "/b c" && saved.at("scale") == "2" && !saved.contains("patch") && saved.size() == 2);
    {   // closed before remove_all: Windows won't delete an open file
        std::ifstream back(written);
        const std::string text((std::istreambuf_iterator<char>(back)), std::istreambuf_iterator<char>());
        assert(text == "data = /b c\n# mine\nscale = 2\n");
    }
    fs::remove_all(dir);

    const auto user = d2d::userdir::user_dir("d2d");
    assert(user.filename() == "d2d");
#if defined(__APPLE__)
    assert(user.parent_path().filename() == "Preferences");
#elif !defined(_WIN32)
    assert(user.parent_path().filename() == ".config");
#endif
    std::printf("user dir: %s\nOK\n", user.string().c_str());
    return 0;
}
