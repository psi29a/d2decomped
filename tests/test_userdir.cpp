// .cfg parsing + per-platform user dir shape.
#include <userdir.hpp>

#include <cassert>
#include <cstdio>
#include <fstream>

namespace fs = std::filesystem;

int main() {
    const auto dir = fs::temp_directory_path() / "d2d_test_userdir";
    fs::create_directories(dir);
    const auto a = dir / "a.cfg", b = dir / "b.cfg";
    std::ofstream(a) << "# comment\n\n  data = /opt/d2 lod  \nno equals sign\n"
                        "=novalue\nscale=2\r\nempty =\n";
    std::ofstream(b) << "scale = 3\n";

    d2d::userdir::Config cfg;
    d2d::userdir::load_cfg(a, cfg);
    assert(cfg.at("data") == "/opt/d2 lod");   // trimmed, inner space kept
    assert(cfg.at("scale") == "2");            // CRLF tolerated
    assert(cfg.at("empty").empty());
    assert(cfg.size() == 3);                   // comment / no '=' / empty key skipped

    d2d::userdir::load_cfg(b, cfg);            // later file wins
    assert(cfg.at("scale") == "3");
    d2d::userdir::load_cfg(dir / "missing.cfg", cfg);
    assert(cfg.size() == 3);
    fs::remove_all(dir);

    const auto u = d2d::userdir::user_dir("d2d");
    assert(u.filename() == "d2d");
#if defined(__APPLE__)
    assert(u.parent_path().filename() == "Preferences");
#elif !defined(_WIN32)
    assert(u.parent_path().filename() == ".config");
#endif
    std::printf("user dir: %s\nOK\n", u.string().c_str());
    return 0;
}
