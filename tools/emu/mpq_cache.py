# SPDX-License-Identifier: GPL-3.0-or-later
"""Cache MPQ reads by archive version, preserving tool errors as errors."""
import hashlib
import os
import subprocess
import threading
from pathlib import Path


class MpqCache:
    def __init__(self, mpqs, cat, patch):
        self.mpqs = str(mpqs)
        self.cat = str(cat)
        self.patch = str(patch)
        inputs = [Path(mpqs) / name for name in ("patch_d2.mpq", "d2exp.mpq", "d2data.mpq", "d2char.mpq", "d2xtalk.mpq", "d2speech.mpq", "d2sfx.mpq", "d2xmusic.mpq", "d2music.mpq")]
        inputs.append(Path(patch))
        fingerprint = [(str(path.resolve()), path.stat().st_size, path.stat().st_mtime_ns) for path in inputs if path.exists()]
        self.directory = Path(__file__).resolve().parent / ".cache" / hashlib.sha256(repr(fingerprint).encode()).hexdigest()[:20]
        self.files = {}

    def read(self, name):
        key = name.lower().replace("/", "\\")
        if key not in self.files:
            cache = self.directory / hashlib.sha256(key.encode()).hexdigest()
            missing = cache.with_suffix(".missing")
            if not cache.exists():
                result = subprocess.run([self.cat, self.mpqs, name], capture_output=True,
                                        env={**os.environ, "D2_PATCH_INSTALLER": self.patch})
                if result.returncode not in (0, 3):
                    raise RuntimeError(f"mpq-cat {name}: {result.stderr.decode(errors='replace').strip() or f'exited {result.returncode}'}")
                cache.parent.mkdir(parents=True, exist_ok=True)
                temporary = cache.with_name(f"{cache.name}.tmp.{os.getpid()}.{threading.get_ident()}")
                try:
                    temporary.write_bytes(result.stdout if result.returncode == 0 else b"")
                    if result.returncode == 3: missing.touch()
                    else: missing.unlink(missing_ok=True)
                    os.replace(temporary, cache)
                finally:
                    temporary.unlink(missing_ok=True)
            self.files[key] = None if missing.exists() else cache.read_bytes()
        return self.files[key]
