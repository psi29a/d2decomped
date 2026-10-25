// whisper: a dev-control socket inside the original game.exe (1.14d),
// so a live host game can be driven and read from another machine.
//
// It is loaded as ijl11.dll (game.exe imports ordinals 2, 3, 5 from it) and
// forwards those to the real one, renamed ijl11_real.dll. It listens only
// when whisper.txt sits beside game.exe: line 1 the token a client
// must send first, line 2 the port (default 4100).
//
// Everything that touches the game runs on the game's thread: the socket
// thread SendMessage()s a WM_APP to the subclassed game window.
// Verbs (one line each, reply ends "ok ..." or "err ..."):
//   ping (reply "ok <version>") | state | units | click X Y | rclick X Y | move X Y | realclick X Y | realrclick X Y | key VK | text WORDS
//   send HEX... (a raw C->S packet) | peek ADDR [N] | screenshot (reply "ok <n>\n" + n BMP bytes)

#include <winsock2.h>
#include <windows.h>

#ifndef WHISPER_VERSION
#define WHISPER_VERSION "unknown"   // build.sh stamps the git commit
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

// --- the ijl11.dll forwarders ---------------------------------------------

extern "C" {
void* real_ordinal_2 = nullptr;
void* real_ordinal_3 = nullptr;
void* real_ordinal_5 = nullptr;
__attribute__((naked)) void forward_ordinal_2() { asm volatile("jmp *_real_ordinal_2"); }
__attribute__((naked)) void forward_ordinal_3() { asm volatile("jmp *_real_ordinal_3"); }
__attribute__((naked)) void forward_ordinal_5() { asm volatile("jmp *_real_ordinal_5"); }
}

// --- game.exe 1.14d -------------------------------------------------------

// FUN_00463dd0 returns DAT_007a6a70, the client's player unit.
auto player_unit() -> std::uint8_t* { return *reinterpret_cast<std::uint8_t**>(0x007a6a70); }

// FUN_0052ae50: __stdcall (u16 size, int 0, u8* packet), RET 0xc; 0 when not
// connected (DAT_00882b34), refuses over 0x204 bytes.
using SendPacketFn = int(__stdcall*)(std::uint32_t size, int zero, const std::uint8_t* packet);
const auto send_packet = reinterpret_cast<SendPacketFn>(0x0052ae50);

// FUN_0052bc20: __fastcall (u8* packet, u32 have, int* size), RET 4: the
// size the host frames a C->S packet at (its table 0x730dc0, chat parsed).
// A packet it sizes otherwise would shift every later one, so send refuses it.
using PacketSizeFn = int(__attribute__((fastcall))*)(const std::uint8_t* packet, std::uint32_t have, int* size);
const auto packet_size = reinterpret_cast<PacketSizeFn>(0x0052bc20);

// FUN_00625480: __stdcall (unit, stat, layer), RET 0xc: a unit's stat
// (monster-ai.md); life / mana (6..9) are 8.8 fixed.
using UnitStatFn = int(__stdcall*)(const std::uint8_t* unit, int stat, int layer);
const auto unit_stat = reinterpret_cast<UnitStatFn>(0x00625480);

// The client's units (FUN_00463a50 links them): two tables of 6 types x 128
// buckets by id & 0x7f, 0x7a5270 when unit +0xc4 bit 21 is set, else
// 0x7a5e70; each bucket chained through +0xe4.
constexpr std::uintptr_t kUnitTables[] = { 0x007a5e70, 0x007a5270 };

auto field(const std::uint8_t* unit, int offset) -> std::uint32_t { return *reinterpret_cast<const std::uint32_t*>(unit + offset); }

// Subtile position: players, monsters, missiles walk a D2DynamicPath (unit
// +0x2c, 16.16 at +0 / +4, monster-ai.md); objects, items and warps sit on
// a static path, x / y at +0xc / +0x10 (matched d2d's waypoint and stash,
// 2026-10-17).
auto unit_position(const std::uint8_t* unit, std::uint32_t& x_pos, std::uint32_t& y_pos) -> void {
    const auto* path = reinterpret_cast<const std::uint32_t*>(field(unit, 0x2c));
    x_pos = y_pos = 0;
    if (!path) return;
    const std::uint32_t type = field(unit, 0);
    if (type == 0 || type == 1 || type == 3) { x_pos = path[0] >> 16; y_pos = path[1] >> 16; }
    else { x_pos = path[3]; y_pos = path[4]; }
}

// --- the game thread --------------------------------------------------------

constexpr UINT kRunOnGameThread = WM_APP + 0x2d2;
HWND g_window = nullptr;
std::string g_token;
WNDPROC g_game_proc = nullptr;

struct Job {
    std::string verb;
    std::istringstream args;
    std::string reply;
};

auto screenshot_bmp() -> std::string {
    RECT client{};
    GetClientRect(g_window, &client);
    const int width = client.right, height = client.bottom;
    BITMAPINFOHEADER info{ .biSize = sizeof(BITMAPINFOHEADER), .biWidth = width, .biHeight = -height,
                           .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB };
    void* pixels = nullptr;
    HDC window_dc = GetDC(g_window);
    HDC memory_dc = CreateCompatibleDC(window_dc);
    HBITMAP bitmap = CreateDIBSection(window_dc, reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS, &pixels, nullptr, 0);
    HGDIOBJ old = SelectObject(memory_dc, bitmap);
    BitBlt(memory_dc, 0, 0, width, height, window_dc, 0, 0, SRCCOPY);
    GdiFlush();
    const std::size_t bytes = std::size_t(width) * height * 4;
    BITMAPFILEHEADER file{ .bfType = 0x4d42, .bfSize = DWORD(sizeof(BITMAPFILEHEADER) + sizeof(info) + bytes),
                           .bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(info) };
    std::string out(reinterpret_cast<const char*>(&file), sizeof(file));
    out.append(reinterpret_cast<const char*>(&info), sizeof(info));
    out.append(static_cast<const char*>(pixels), bytes);
    SelectObject(memory_dc, old);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(g_window, window_dc);
    return out;
}

auto run(Job& job) -> void {
    char line[160];
    if (job.verb == "state") {
        const std::uint8_t* unit = player_unit();
        if (!unit) {
            std::snprintf(line, sizeof line, "err no player unit (game.exe at %p)\n", static_cast<void*>(GetModuleHandleA(nullptr)));
            job.reply = line;
            return;
        }
        // type +0, class +4 (FUN_00463d90), id +0xc (FUN_00463a50), mode +0x10
        // (0x11 dead, FUN_00463df0).
        std::uint32_t x_pos = 0, y_pos = 0;
        unit_position(unit, x_pos, y_pos);
        std::snprintf(line, sizeof line, "id %u type %u class %u mode %u x %u y %u life %d/%d mana %d/%d\nok\n", field(unit, 0xc),
                      field(unit, 0), field(unit, 4), field(unit, 0x10), x_pos, y_pos, unit_stat(unit, 6, 0) >> 8,
                      unit_stat(unit, 7, 0) >> 8, unit_stat(unit, 8, 0) >> 8, unit_stat(unit, 9, 0) >> 8);
        job.reply = line;
    } else if (job.verb == "units") {   // type id class mode x y life maxlife, one per line
        for (const std::uintptr_t table : kUnitTables)
            for (int bucket = 0; bucket < 6 * 128; ++bucket)
                for (auto* unit = *reinterpret_cast<std::uint8_t**>(table + bucket * 4); unit;
                     unit = reinterpret_cast<std::uint8_t*>(field(unit, 0xe4))) {
                    std::uint32_t x_pos = 0, y_pos = 0;
                    unit_position(unit, x_pos, y_pos);
                    std::snprintf(line, sizeof line, "%u %u %u %u %u %u %d %d\n", field(unit, 0), field(unit, 0xc), field(unit, 4),
                                  field(unit, 0x10), x_pos, y_pos, unit_stat(unit, 6, 0) >> 8, unit_stat(unit, 7, 0) >> 8);
                    job.reply += line;
                }
        job.reply += "ok\n";
    } else if (job.verb == "send") {
        std::vector<std::uint8_t> packet;
        unsigned value = 0;
        while (job.args >> std::hex >> value) packet.push_back(std::uint8_t(value));
        if (packet.empty()) { job.reply = "err send <hex bytes>\n"; return; }
        int framed = 0;
        packet_size(packet.data(), std::uint32_t(packet.size()), &framed);
        if (framed != int(packet.size())) {
            std::snprintf(line, sizeof line, "err the host frames this at %d bytes, not %u\n", framed, unsigned(packet.size()));
            job.reply = line;
            return;
        }
        std::snprintf(line, sizeof line, "ok %d\n", send_packet(std::uint32_t(packet.size()), 0, packet.data()));
        job.reply = line;
    } else if (job.verb == "peek") {   // peek ADDR [COUNT]: u32s, unreadable ones as "?"
        unsigned address = 0, count = 1;
        if (!(job.args >> std::hex >> address)) { job.reply = "err peek <hex address> [count]\n"; return; }
        job.args >> std::dec >> count;
        for (unsigned index = 0; index < std::min(count, 64u); ++index) {
            std::uint32_t value = 0;
            const bool read = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address + index * 4), &value, 4, nullptr);
            std::snprintf(line, sizeof line, read ? "%08x %08x\n" : "%08x ?\n", address + index * 4, value);
            job.reply += line;
        }
        job.reply += "ok\n";
    } else if (job.verb == "screenshot") {
        const std::string bmp = screenshot_bmp();
        job.reply = "ok " + std::to_string(bmp.size()) + "\n" + bmp;
    }
}

LRESULT CALLBACK game_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == kRunOnGameThread) {
        run(*reinterpret_cast<Job*>(lparam));
        return 0;
    }
    return CallWindowProcA(g_game_proc, window, message, wparam, lparam);
}

// --- the socket -------------------------------------------------------------

auto beside_exe(const char* name) -> std::string {
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    const std::string path = exe;
    return path.substr(0, path.find_last_of("\\/") + 1) + name;
}

// What happened, to whisper.log beside game.exe (no console to see).
auto note(const std::string& text) -> void {
    if (FILE* log = std::fopen(beside_exe("whisper.log").c_str(), "a")) {
        std::fprintf(log, "%s\n", text.c_str());
        std::fclose(log);
    }
}

auto mouse(UINT down, UINT up, int x_pos, int y_pos) -> void {
    const LPARAM where = MAKELPARAM(x_pos, y_pos);
    PostMessageA(g_window, WM_MOUSEMOVE, 0, where);
    if (down) PostMessageA(g_window, down, down == WM_LBUTTONDOWN ? MK_LBUTTON : MK_RBUTTON, where);
    if (up) PostMessageA(g_window, up, 0, where);
}

// A real click: the cursor put there and the button pressed through
// SendInput, for what game.exe reads past the window messages (a warp's or
// NPC's interact once the walk there ends: posted clicks walk, never enter).
// The game must be the foreground window; the machine's own cursor moves.
auto real_click(bool right, int x_pos, int y_pos) -> void {
    POINT spot{ x_pos, y_pos };
    ClientToScreen(g_window, &spot);
    SetCursorPos(spot.x, spot.y);
    Sleep(60);   // a frame for the client to light up what's under it
    INPUT press[2] = {};
    press[0].type = press[1].type = INPUT_MOUSE;
    press[0].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
    press[1].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
    SendInput(1, &press[0], sizeof(INPUT));
    Sleep(80);
    SendInput(1, &press[1], sizeof(INPUT));
}

auto handle(const std::string& text) -> std::string {
    Job job{ .args = std::istringstream(text) };
    job.args >> job.verb;
    int x_pos = 0, y_pos = 0;
    if (job.verb == "ping") return "ok " WHISPER_VERSION "\n";
    if (job.verb == "click" || job.verb == "rclick" || job.verb == "move") {
        if (!(job.args >> x_pos >> y_pos)) return "err " + job.verb + " <x> <y>\n";
        if (job.verb == "click") mouse(WM_LBUTTONDOWN, WM_LBUTTONUP, x_pos, y_pos);
        else if (job.verb == "rclick") mouse(WM_RBUTTONDOWN, WM_RBUTTONUP, x_pos, y_pos);
        else mouse(0, 0, x_pos, y_pos);
        return "ok\n";
    }
    if (job.verb == "realclick" || job.verb == "realrclick") {
        if (!(job.args >> x_pos >> y_pos)) return "err " + job.verb + " <x> <y>\n";
        if (GetForegroundWindow() != g_window) return "err the game isn't the foreground window\n";
        real_click(job.verb == "realrclick", x_pos, y_pos);
        return "ok\n";
    }
    if (job.verb == "key") {   // a virtual-key code: 13 Return, 27 Esc, 0x41 'A', ...
        unsigned key = 0;
        if (!(job.args >> std::setbase(0) >> key)) return "err key <vk>\n";
        PostMessageA(g_window, WM_KEYDOWN, key, 1);
        PostMessageA(g_window, WM_KEYUP, key, 1 | 0xc0000000);
        return "ok\n";
    }
    if (job.verb == "text") {
        std::string words;
        std::getline(job.args >> std::ws, words);
        for (const char letter : words) PostMessageA(g_window, WM_CHAR, WPARAM(std::uint8_t(letter)), 1);
        return "ok\n";
    }
    SendMessageA(g_window, kRunOnGameThread, 0, reinterpret_cast<LPARAM>(&job));
    return job.reply.empty() ? "err unknown verb '" + job.verb + "'\n" : job.reply;
}

constexpr std::size_t kMaxLine = 4096;                     // the longest line read (a send of ~1300 bytes fits)

auto read_line(SOCKET client, std::string& pending, std::string& line) -> bool {
    for (;;) {
        if (const auto end = pending.find('\n'); end != std::string::npos) {
            line = pending.substr(0, end);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pending.erase(0, end + 1);
            return true;
        }
        if (pending.size() > kMaxLine) return false;          // no line that long: a stuck or hostile client
        char buffer[512];
        const int got = recv(client, buffer, sizeof buffer, 0);
        if (got <= 0) return false;
        pending.append(buffer, got);
    }
}

auto send_all(SOCKET client, const std::string& data) -> void {
    for (std::size_t sent = 0; sent < data.size();) {
        const int put = send(client, data.data() + sent, int(data.size() - sent), 0);
        if (put <= 0) return;
        sent += std::size_t(put);
    }
}

DWORD WINAPI talk(void* socket_handle) {
    const auto client = SOCKET(reinterpret_cast<std::uintptr_t>(socket_handle));
    std::string pending, line;
    DWORD wait_ms = 10000;                                     // the token within 10 s, or the thread ends
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&wait_ms), sizeof wait_ms);
    if (read_line(client, pending, line) && line == g_token) {
        wait_ms = 0;                                           // then no limit: commands come when the driver sends them
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&wait_ms), sizeof wait_ms);
        send_all(client, "ok whisper\n");
        while (read_line(client, pending, line)) send_all(client, handle(line));
    }
    closesocket(client);
    return 0;
}

DWORD WINAPI serve(void*) {
    FILE* file = std::fopen(beside_exe("whisper.txt").c_str(), "r");
    if (!file) { note("no whisper.txt: not listening"); return 0; }
    char token_line[128] = {}, port_line[16] = {};
    std::fgets(token_line, sizeof token_line, file);
    std::fgets(port_line, sizeof port_line, file);
    std::fclose(file);
    std::string& token = g_token;
    token = token_line;
    while (!token.empty() && (token.back() == '\n' || token.back() == '\r')) token.pop_back();
    const int port = std::atoi(port_line) > 0 ? std::atoi(port_line) : 4100;
    if (token.empty()) { note("whisper.txt has no token: not listening"); return 0; }

    while (!(g_window = FindWindowA("Diablo II", nullptr))) Sleep(500);
    g_game_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(game_proc)));

    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{ .sin_family = AF_INET, .sin_port = htons(std::uint16_t(port)) };
    address.sin_addr.s_addr = INADDR_ANY;
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || listen(listener, 1) != 0) {
        note("can't listen on port " + std::to_string(port) + ": error " + std::to_string(WSAGetLastError()));
        return 0;
    }
    note("whisper " WHISPER_VERSION " listening on port " + std::to_string(port));
    for (;;) {   // a thread a client; the game thread still runs one command at a time
        SOCKET client = accept(listener, nullptr, nullptr);
        if (client != INVALID_SOCKET)
            CloseHandle(CreateThread(nullptr, 0, talk, reinterpret_cast<void*>(std::uintptr_t(client)), 0, nullptr));
    }
}

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    // Never stop the game starting (0xc0000142): without the real DLL its
    // three entries stay null, and only a JPEG call would reach them.
    HMODULE real = LoadLibraryA(beside_exe("ijl11_real.dll").c_str());
    if (!real) note("can't load ijl11_real.dll (error " + std::to_string(GetLastError()) + "): rename the original ijl11.dll to it");
    if (real) {
        real_ordinal_2 = reinterpret_cast<void*>(GetProcAddress(real, MAKEINTRESOURCEA(2)));
        real_ordinal_3 = reinterpret_cast<void*>(GetProcAddress(real, MAKEINTRESOURCEA(3)));
        real_ordinal_5 = reinterpret_cast<void*>(GetProcAddress(real, MAKEINTRESOURCEA(5)));
    }
    CloseHandle(CreateThread(nullptr, 0, serve, nullptr, 0, nullptr));
    return TRUE;
}
