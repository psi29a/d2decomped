# SPDX-License-Identifier: GPL-3.0-or-later
"""The headless test scripts' side of devctl (docs/control_channel.md).

connect(path, timeout): d2d's control socket, AF_UNIX on every OS. Python
on Windows has no socket.AF_UNIX, though Winsock has had it since Windows 10
1803 (what d2d listens on there), so on Windows the same calls go to ws2_32
through ctypes.

isolated(home): the user dir under a throwaway home and the environment that
makes d2d use it. On Windows d2d's dir is Documents\\My Games\\d2d whatever
HOME says, so D2D_USER_DIR names it on every OS.
"""
import ctypes
import os
import socket
import sys


def isolated(home):
    """(user dir, environment) for a d2d run kept apart from the real one."""
    if sys.platform == "darwin":
        user = os.path.join(home, "Library", "Preferences", "d2d")
    elif sys.platform == "win32":
        user = os.path.join(home, "My Games", "d2d")
    else:
        user = os.path.join(home, ".config", "d2d")
    return user, dict(os.environ, HOME=home, D2D_USER_DIR=user)


def connect(path, timeout):
    """A connected stream socket on d2d's control socket at `path`."""
    if hasattr(socket, "AF_UNIX"):
        sock = socket.socket(socket.AF_UNIX)
        sock.settimeout(timeout)
        sock.connect(path)
        return sock
    return _WinsockUnix(path, timeout)


class _SockaddrUn(ctypes.Structure):
    _fields_ = [("sun_family", ctypes.c_ushort), ("sun_path", ctypes.c_char * 108)]


class _WinsockUnix:
    """The few socket calls the scripts use, on a Winsock AF_UNIX socket.
    Python's socket module has already started Winsock (WSAStartup)."""

    _AF_UNIX, _SOCK_STREAM, _SOL_SOCKET, _SO_SNDTIMEO, _SO_RCVTIMEO = 1, 1, 0xFFFF, 0x1005, 0x1006
    _WSAETIMEDOUT = 10060

    def __init__(self, path, timeout):
        ws2 = ctypes.WinDLL("ws2_32", use_last_error=True)
        ws2.socket.restype = ctypes.c_size_t
        ws2.socket.argtypes = (ctypes.c_int, ctypes.c_int, ctypes.c_int)
        for name in ("connect", "send", "recv", "setsockopt", "shutdown", "closesocket"):
            getattr(ws2, name).restype = ctypes.c_int
        ws2.connect.argtypes = (ctypes.c_size_t, ctypes.c_void_p, ctypes.c_int)
        ws2.send.argtypes = (ctypes.c_size_t, ctypes.c_char_p, ctypes.c_int, ctypes.c_int)
        ws2.recv.argtypes = (ctypes.c_size_t, ctypes.c_void_p, ctypes.c_int, ctypes.c_int)
        ws2.setsockopt.argtypes = (ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int)
        ws2.shutdown.argtypes = (ctypes.c_size_t, ctypes.c_int)
        ws2.closesocket.argtypes = (ctypes.c_size_t,)
        self._ws2 = ws2
        self._handle = ws2.socket(self._AF_UNIX, self._SOCK_STREAM, 0)
        if self._handle == ctypes.c_size_t(-1).value:
            raise self._error("socket")
        millis = ctypes.c_uint32(int(timeout * 1000))
        for option in (self._SO_SNDTIMEO, self._SO_RCVTIMEO):   # without them a stuck d2d hangs recv
            if ws2.setsockopt(self._handle, self._SOL_SOCKET, option, ctypes.byref(millis), ctypes.sizeof(millis)) != 0:
                error = self._error(f"setsockopt {option:#x}")
                self.close()
                raise error
        address = _SockaddrUn(self._AF_UNIX, os.fsencode(path))
        if ws2.connect(self._handle, ctypes.byref(address), ctypes.sizeof(address)) != 0:
            error = self._error(f"connect {path}")
            self.close()
            raise error

    def _error(self, what):
        code = ctypes.get_last_error()
        if code == self._WSAETIMEDOUT:
            return TimeoutError(f"{what}: timed out")
        return ConnectionError(f"{what}: winsock error {code}")

    def sendall(self, data):
        view = memoryview(data)
        while view:
            sent = self._ws2.send(self._handle, bytes(view[:65536]), min(len(view), 65536), 0)
            if sent < 0:
                raise self._error("send")
            view = view[sent:]

    def recv(self, size):
        buffer = ctypes.create_string_buffer(size)
        received = self._ws2.recv(self._handle, buffer, size, 0)
        if received < 0:
            raise self._error("recv")
        return buffer.raw[:received]

    def shutdown(self, how):
        self._ws2.shutdown(self._handle, how)

    def close(self):
        if self._handle is not None:
            self._ws2.closesocket(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
