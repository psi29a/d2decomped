// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: userdir
// .cfg parsing + per-platform user dir shape.
#include <userdir.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <stdlib.h>   // setenv / unsetenv (POSIX), _putenv_s (MSVC)
#include <string>

namespace fs = std::filesystem;

namespace {

// An empty value unsets it (user_dir treats empty as not set either way).
void set_env(const char* name, const char* value) {
#if defined(_WIN32)
    (void)::_putenv_s(name, value);
#else
    if (*value) (void)::setenv(name, value, 1);
    else (void)::unsetenv(name);
#endif
}

}  // namespace

int main() try {
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

    set_env("D2D_USER_DIR", "");                       // empty: not set
    const auto user = d2d::userdir::user_dir("d2d");
    assert(user.filename() == "d2d");
#if defined(__APPLE__)
    assert(user.parent_path().filename() == "Preferences");
#elif !defined(_WIN32)
    assert(user.parent_path().filename() == ".config");
#endif
    // D2D_USER_DIR overrides the whole dir (the test scripts' throwaway one).
    const auto forced = fs::temp_directory_path() / "d2d_forced_user";
    set_env("D2D_USER_DIR", forced.string().c_str());
    assert(d2d::userdir::user_dir("d2d") == forced);
#if defined(_WIN32)
    // Outside the code page (u-umlaut, two CJK characters): read wide, the path stays whole.
    const auto unicode = fs::temp_directory_path() / L"d2d_\u00fc_\u7528\u6237";
    (void)::_wputenv_s(L"D2D_USER_DIR", unicode.c_str());
    assert(d2d::userdir::user_dir("d2d") == unicode);
#endif
    set_env("D2D_USER_DIR", "");
    assert(d2d::userdir::user_dir("d2d") == user);
    std::printf("user dir: %s\nOK\n", user.string().c_str());
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_userdir: %s\n", error.what());
    return 1;
}
