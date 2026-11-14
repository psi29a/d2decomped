# SPDX-License-Identifier: GPL-3.0-or-later
"""Trace FUN_004750f0's static-light shadow cache in game.exe 1.14d.

Run: uv run python static_light_cache.py --exe /path/to/Game.exe [--mutation-check]
Collision/lookup are scripted. The two original shadow propagation helpers run
for real. Python independently models sampling, the full scratch buffer,
propagation, and the cache copy. This checks stock code, not d2d parity.
"""
import argparse
import hashlib
import struct
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

SHA256 = '631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf'
FUNCTION, WRAPPER = 0x4750F0, 0x4755A0
SHADOW, COLLISION, CELLS = 0x7A8A50, 0x7ACA50, 64 * 64
LOOKUP_A, LOOKUP_B, ROOM, GET_X, GET_Y, COLLIDE = 0x463990, 0x4639B0, 0x620BB0, 0x45ADF0, 0x45AE20, 0x64CB30
FATAL_STRING, REPORT, EXIT = 0x408090, 0x408A60, 0x681E09
MUTATION = None


def check(actual, expected, label):
    """Stop on the first discrepancy with a reproducible case label."""
    if actual != expected:
        raise AssertionError(f'{label}: {actual!r} != {expected!r}')


def make_light(kind, ident, alternate, x, y, radius, cache, built=0, category=2, target=None, frozen=1):
    """Pack a synthetic 0x34-byte light record using its traced fields."""
    words = [kind, ident, alternate, category, x, y, radius,
             radius if target is None else target, frozen, 0, 0, built, cache]
    return struct.pack('<13I', *(value & 0xffffffff for value in words))


def model(radius, light_x, light_y, source_x, source_y, obstacle, old_shadow):
    """Compute the sampled map, ring propagation, and packed cache window."""
    grid = old_shadow.copy()
    clear = CELLS if MUTATION == 'clear-all' else 0x1000 // 4
    grid[:clear] = [0] * clear
    blocks = [(15 if MUTATION == 'block-value' else 16) if obstacle(source_x + col - 32, source_y + row - 32) & 0xffff else 0
              for row in range(64) for col in range(64)]
    def shade(pixel_x, pixel_y, row, col):
        """Interpolate the prior ring using the original fixed-point weights."""
        dx, dy = light_x - pixel_x, light_y - pixel_y
        if not (dx or dy):
            return
        step_x, step_y = (-1 if dx < 0 else 1), (-1 if dy < 0 else 1)
        dx, dy = abs(dx), abs(dy)
        def neighbor(index):
            """Return the collision value or the previously propagated shade."""
            return blocks[index] if blocks[index] else grid[index]
        index = row * 64 + col
        if not dx:
            value = neighbor((row + step_y) * 64 + col)
        elif not dy:
            value = neighbor(index + step_x)
        elif dy <= dx:
            fraction = (dy << 8) // dx
            value = ((256 - fraction) * neighbor(index + step_x) + fraction * neighbor((row + step_y) * 64 + col + step_x)) >> 8
        else:
            fraction = (dx << 8) // dy
            value = ((256 - fraction) * neighbor((row + step_y) * 64 + col) + fraction * neighbor((row + step_y) * 64 + col + step_x)) >> 8
        grid[index] = value
    center_x, center_y = light_x >> 3, light_y >> 3
    for ring in range(2, (radius >> 3) + (0 if MUTATION == 'skip-outer-ring' else 1)):
        top, bottom = center_y * 8 + 4 - ring * 8, center_y * 8 + 4 + ring * 8
        left, right = center_x * 8 + 4 - ring * 8, center_x * 8 + 4 + ring * 8
        for offset in range(ring + 1):
            near_x, far_x = center_x * 8 + 4 - offset * 8, center_x * 8 + 4 + offset * 8
            near_y, far_y = center_y * 8 + 4 - offset * 8, center_y * 8 + 4 + offset * 8
            for args in ((near_x, top, 32-ring, 32-offset), (far_x, top, 32-ring, 32+offset),
                         (near_x, bottom, 32+ring, 32-offset), (far_x, bottom, 32+ring, 32+offset),
                         (right, near_y, 32-offset, 32+ring), (right, far_y, 32+offset, 32+ring),
                         (left, near_y, 32-offset, 32-ring), (left, far_y, 32+offset, 32-ring)):
                shade(*args)
    side = (radius * 2 >> 3) + 1
    start = 32 - (radius >> 3) - int(MUTATION == 'copy-origin')
    combined = grid + blocks
    cache = [combined[(start + row) * 64 + start + col] for row in range(side) for col in range(side)]
    return grid, blocks, cache


class Rig:
    """One emulated image with scripted room, collision and cache allocation."""
    def __init__(self, exe):
        """Set up hooks and sentinel arguments while leaving shadow helpers real."""
        self.e = emu.Emu(str(exe))
        self.log = []
        self.sample_count = 0
        self.present = True
        self.room = 0x33330000
        self.unit = 0x11223344
        self.world_x, self.world_y = 100, -50
        self.obstacle = lambda x, y: 0
        self.fatal = None
        self.allocated = []
        self.e.hook(LOOKUP_A, self.lookup(LOOKUP_A), 0)
        self.e.hook(LOOKUP_B, self.lookup(LOOKUP_B), 0)
        self.e.hook(ROOM, self.get_room, 1)
        self.e.hook(GET_X, self.coordinate(GET_X), 0)
        self.e.hook(GET_Y, self.coordinate(GET_Y), 0)
        self.e.hook(COLLIDE, self.collision, 4)
        self.e.hook(FATAL_STRING, self.fatal_string, 0)
        self.e.hook(REPORT, self.report_fatal, 0)
        self.e.hook(EXIT, self.terminate, 0)
        self.e.hook(0x40B380, self.allocate, 2)
        self.e.hook(0x40B3C0, self.free, 2)
        self.e.hook(0x475420, self.draw, 0)

    def lookup(self, address):
        """Return the synthetic unit and log table choice and lookup arguments."""
        def run(machine):
            """Capture ECX unit id and EDX unit type."""
            self.log.append(('lookup', address, machine.mu.reg_read(UC_X86_REG_ECX), machine.mu.reg_read(UC_X86_REG_EDX)))
            return self.unit if self.present else 0
        return run

    def get_room(self, machine):
        """Return the configured room for the requested unit."""
        self.log.append(('room', machine.arg(0)))
        return self.room

    def coordinate(self, address):
        """Return the selected synthetic unit coordinate."""
        def run(machine):
            """Capture the unit in ECX."""
            self.log.append(('coordinate', address, machine.mu.reg_read(UC_X86_REG_ECX)))
            return self.world_x if address == GET_X else self.world_y
        return run

    def collision(self, machine):
        """Validate every row-major room collision request and return its value."""
        row, col = divmod(self.sample_count, 64)
        x, y = self.world_x + col - 32, self.world_y + row - 32
        check(tuple(machine.arg(index) for index in range(4)),
              (self.room, x & 0xffffffff, y & 0xffffffff, 0x22), f'collision sample {self.sample_count}')
        self.sample_count += 1
        return self.obstacle(x, y)

    def fatal_string(self, machine):
        """Record the fatal source line and return a synthetic message."""
        self.fatal = machine.arg(0)
        self.log.append(('fatal-string', self.fatal))
        return 0x6CC837

    def report_fatal(self, machine):
        """Record the error-report call without invoking any native UI."""
        self.log.append(('fatal-report', machine.arg(0), machine.arg(1)))
        return 0

    def terminate(self, machine):
        """Stop the emulated process at the stock fatal exit."""
        self.log.append(('exit', machine.arg(0)))
        machine.mu.emu_stop()
        return 0

    def allocate(self, machine):
        """Allocate a poisoned cache and log its requested size and metadata."""
        size = machine.mu.reg_read(UC_X86_REG_ECX)
        file = machine.mu.reg_read(UC_X86_REG_EDX)
        memory = machine.alloc(size + 1024)
        machine.mu.mem_write(memory, b'\xa5' * (size + 1024))
        self.log.append(('alloc', size, file, machine.arg(0), machine.arg(1), memory))
        self.allocated.append((memory, size))
        return memory

    def free(self, machine):
        """Record cache release without reclaiming emulated memory."""
        self.log.append(('free', machine.mu.reg_read(UC_X86_REG_ECX), machine.mu.reg_read(UC_X86_REG_EDX), machine.arg(0), machine.arg(1)))
        return 0

    def draw(self, machine):
        """Record the caller's final cache-stamp operation."""
        self.log.append(('draw',))
        return 0

    def run(self, light, wrapper=False, fatal=None):
        """Run with a bounded instruction count, checking normal/fatal exit."""
        self.log.clear()
        self.sample_count = 0
        self.fatal = None
        if wrapper:
            self.e.call(WRAPPER, 2, regs={'eax': light}, limit=4_000_000)
        else:
            self.e.call(FUNCTION, regs={'esi': light}, limit=4_000_000)
        check(self.fatal, fatal, 'fatal line')
        if fatal is None:
            check(self.e.mu.reg_read(UC_X86_REG_EIP), emu.RET_MAGIC, 'function return')
            check(self.e.mu.reg_read(UC_X86_REG_ESP), emu.STACK + emu.STACK_SIZE - 0x1000, 'stack cleanup')
        else:
            check(self.log[-1], ('exit', 0xffffffff), 'fatal exit')


def read_grid(machine, address):
    """Read a 64×64 table of 32-bit elements."""
    return list(struct.unpack('<4096I', machine.read(address, 0x4000)))


def computed_cases(rig):
    """Compare both entire scratch grids and cached windows to the model."""
    cases = 0
    patterns = [lambda x, y: 0, lambda x, y: 1,
                lambda x, y: int(x == 101 or y == -48),
                lambda x, y: int((x * 73 + y * 47) % 11 < 3),
                lambda x, y: 0x10000]
    radii = (1, 7, 8, 15, 16, 24, 41, 72, 144, 248, 255)
    for index, radius in enumerate(radii):
        for initial in (0, 7):
            rig.obstacle = patterns[index % len(patterns)]
            rig.present, rig.room = True, 0x33330000
            light_x = 100 * 8 + (index % 8)
            light_y = -50 * 8 + ((index * 3) % 8)
            previous = [initial] * CELLS
            rig.e.mu.mem_write(SHADOW, struct.pack('<4096I', *previous))
            rig.e.mu.mem_write(COLLISION, b'\x55' * 0x4000)
            side = (radius * 2 >> 3) + 1
            cache = rig.e.alloc(side * side * 4 + 16)
            rig.e.mu.mem_write(cache, b'\xa5' * (side * side * 4 + 16))
            alternate = index % 2
            light = rig.e.put(make_light(1, 0x12345678, alternate, light_x, light_y, radius, cache))
            rig.run(light)
            shade, blocks, wanted = model(radius, light_x, light_y, rig.world_x, rig.world_y, rig.obstacle, previous)
            check(rig.sample_count, 4096, 'collision count')
            check(rig.log, [('lookup', LOOKUP_B if alternate else LOOKUP_A, 0x12345678, 1), ('room', rig.unit),
                            ('coordinate', GET_X, rig.unit), ('coordinate', GET_Y, rig.unit)], 'lookup order')
            check(read_grid(rig.e, SHADOW), shade, 'shadow table')
            check(read_grid(rig.e, COLLISION), blocks, 'collision table')
            check(rig.e.read(cache, side * side * 4), struct.pack(f'<{side * side}I', *wanted), 'cache contents')
            check(rig.e.read(cache + side * side * 4, 16), b'\xa5' * 16, 'cache end guard')
            check(rig.e.view('static_light', light).cache_ready, 1, 'cache flag')
            cases += 1
    return cases


def gates(rig):
    """Exercise invalid radii, type 6, missing unit and fatal guard paths."""
    count = 0
    for radius in (0, -1, 256, 0x7fffffff):
        rig.e.mu.mem_write(SHADOW, b'\xaa' * 0x4000)
        light = rig.e.put(make_light(1, 7, 0, 800, -400, radius, 0))
        rig.run(light)
        check(rig.e.read(SHADOW, 0x4000), b'\xaa' * 0x4000, 'invalid radius leaves grid')
        check((rig.log, rig.sample_count), ([], 0), 'invalid radius has no calls')
        count += 1
    for kind, present in ((6, True), (1, False)):
        rig.present = present
        rig.e.mu.mem_write(SHADOW, b'\xaa' * 0x4000)
        light = rig.e.put(make_light(kind, 7, 0, 800, -400, 8, 0))
        rig.run(light)
        check(rig.e.read(SHADOW, 0x4000), bytes(0x1000) + b'\xaa' * 0x3000, 'early return clears prefix only')
        check(rig.sample_count, 0, 'early return no samples')
        check(rig.log, [] if kind == 6 else [('lookup', LOOKUP_A, 7, 1)], 'early return lookup')
        count += 1
    rig.present = True
    for room, built, pointer, fatal in ((0, 0, 1, 0x414), (0x33330000, 1, 1, 0x454), (0x33330000, 0, 0, 0x455)):
        rig.room = room
        rig.obstacle = lambda x, y: 0
        cache = rig.e.alloc(64) if pointer else 0
        light = rig.e.put(make_light(1, 7, 0, 800, -400, 8, cache, built=built))
        rig.run(light, fatal=fatal)
        check(rig.sample_count, 0 if not room else 4096, 'fatal sampling count')
        count += 1
    return count


def cache_lifecycle(rig):
    """Check wrapper reuse and invalidation on a radius change."""
    rig.present, rig.room = True, 0x33330000
    rig.obstacle = lambda x, y: 0
    rig.e.mu.mem_write(SHADOW, bytes(0x4000))
    light = rig.e.put(make_light(1, 7, 0, 800, -400, 16, 0))
    rig.run(light, wrapper=True)
    first = rig.e.view('static_light', light).cache
    check(rig.e.view('static_light', light).cache_ready, 1, 'first cache built')
    check(rig.sample_count, 4096, 'first samples')
    check(rig.allocated[-1][1], (2 * (16 >> 3) + 1) ** 2 * 4, 'initial cache allocation size')
    before = rig.e.read(first, rig.allocated[-1][1])
    rig.obstacle = lambda x, y: 1
    rig.run(light, wrapper=True)
    check((rig.sample_count, rig.e.view('static_light', light).cache), (0, first), 'cached reuse')
    check(rig.e.read(first, len(before)), before, 'cached bytes unchanged')
    check(rig.log, [('draw',)], 'reuse only draws')
    previous = read_grid(rig.e, SHADOW)
    rig.e.view('static_light', light).target_radius = 24
    rig.run(light, wrapper=True)
    second = rig.e.view('static_light', light).cache
    check(rig.sample_count, 4096, 'radius change resamples')
    check(second != first, True, 'radius change allocates new cache')
    check(rig.log[0][0:2], ('free', first), 'old cache released')
    check(rig.allocated[-1][1], (2 * (24 >> 3) + 1) ** 2 * 4, 'new cache allocation size')
    _, _, expected = model(24, 800, -400, rig.world_x, rig.world_y, rig.obstacle, previous)
    check(rig.e.read(second, len(expected) * 4), struct.pack(f'<{len(expected)}I', *expected), 'rebuilt cache bytes')
    check(rig.e.view('static_light', light).cache_ready, 1, 'new cache built')
    return 3



def nonmultiple_radius(rig):
    """Check the wrapper allocation against the callee copy for synthetic radii."""
    rig.present, rig.room = True, 0x33330000
    rig.obstacle = lambda x, y: 0
    count = 0
    for radius in (4, 20, 23, 252):
        rig.e.mu.mem_write(SHADOW, bytes(0x4000))
        light = rig.e.put(make_light(1, 7, 0, 800, -400, radius, 0))
        rig.run(light, wrapper=True)
        cache, size = rig.allocated[-1]
        side = (radius * 2 >> 3) + 1
        expected_size = (2 * (radius >> 3) + 1) ** 2 * 4
        copied = side * side * 4
        check(size, expected_size, 'synthetic cache allocation')
        check(copied > size, True, 'synthetic cache overrun size')
        check(rig.e.read(cache + size, copied - size), bytes(copied - size), 'synthetic cache overwrite')
        check(rig.e.read(cache + copied, 16), b'\xa5' * 16, 'synthetic overwrite end')
        count += 1
    return count


def main():
    """Validate the image and run modeled paths and a targeted negative control."""
    global MUTATION
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--mutation-check', action='store_true')
    args = parser.parse_args()
    check(hashlib.sha256(args.exe.read_bytes()).hexdigest(), SHA256, 'game.exe SHA-256')
    rig = Rig(args.exe)
    for group in (computed_cases, gates, cache_lifecycle, nonmultiple_radius):
        print(f'{group.__name__}: {group(rig)} cases passed', flush=True)
    if args.mutation_check:
        probes = (('clear-all', 'shadow table'), ('block-value', 'collision table'),
                  ('skip-outer-ring', 'shadow table'), ('copy-origin', 'cache contents'))
        for mutation, label in probes:
            MUTATION = mutation
            try:
                computed_cases(Rig(args.exe))
            except AssertionError as error:
                if not str(error).startswith(label + ':'):
                    raise
                print(f'caught mutation: {mutation}', flush=True)
            else:
                raise AssertionError(f'{mutation} mutation survived')
            finally:
                MUTATION = None


if __name__ == '__main__':
    main()
