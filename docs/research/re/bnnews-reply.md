# BNNews reply: stock 1.14d execution

`FUN_0051d860` merges a `SID_NEWS_INFO` reply. The bounded oracle
[`bnnews_reply.py`](../../../tools/emu/bnnews_reply.py) runs the original
function and `FUN_0051d510` from game.exe 1.14d (SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`).
The cache writer is hooked and the array preallocated; callbacks are hooked
and recorded. It does not exercise real allocation, the cache writer or
reader, malformed packet bounds, concurrent access, or a live server.

On the tested records, nonzero timestamps are inserted in ascending order;
a duplicate timestamp replaces its text and clears its flag. A zero timestamp
is delivered immediately as a MOTD and is not stored. After the merge, the
handler finds the first timestamp at least the reply's oldest timestamp,
stores the newest timestamp, attempts to save, then calls the header callback
and delivers the selected records in descending timestamp order. The reply
still returns success when the hooked save returns failure. Null callbacks
are skipped.

The count byte is **sign extended** before an unsigned comparison with 0x100:
0x80–0xff therefore take the fatal path, even though the earlier description
called it an unsigned count. The oracle exercises 0x7f, 0x80 and 0xff. It
uses well-formed NUL-terminated text; it does not claim bounds validation for
untrusted packets. The callback order and merge facts are bounded to the test
cases and do not establish a d2d port or live Battle.net compatibility.

## Cache and array checks

[`bnnews_cache.py`](../../../tools/emu/bnnews_cache.py) runs the stock save
and load functions with synthetic cache I/O, scripted clock and allocator,
and a preallocated array. Save serializes the newest timestamp and unread
count as two little-endian dwords, followed by each unread timestamp and its
NUL-terminated text in array order. The write receives two `-1` lifetimes;
the save returns the writer's result. In the checked cases an unread string
of 0x400 bytes returns failure without writing, and 0x100 unread items take
the fatal path. Load accepts the tested nondecreasing timestamps, including
an equal pair, but clears the array and timestamps for descending timestamps,
a 0x400-byte string, 0x101 items, or a missing cache. A valid empty list and a synthetic 256-item list load successfully; 255
unread entries save successfully. The test does not exercise actual archive
I/O, reallocation during load, truncated buffers or time conversion.

[`bnnews_array.py`](../../../tools/emu/bnnews_array.py) exercises the array's
growth-step helper, resize, count setter, rounded and exact reserve, append,
clear, and deleting destructor. It forces Storm realloc to fail and supplies a
fresh block, checking that the old items are copied and the old block freed.
The checked growth steps for requested sizes 0–3 are 1, 1, 2, 2; sizes 8 and
larger select and store step 8. A rounded reserve grows a count of 3 to
capacity 8 for four more; an exact reserve grows count 4 to capacity 9 for
five more. The deleting destructor frees its header only when flag bit 0 is
set. This synthetic allocator cannot establish behavior of a real heap,
allocation failures or arithmetic overflow.

## Subscription initialization

[`bnnews_init.py`](../../../tools/emu/bnnews_init.py) runs `FUN_0051dbf0`
with scripted cache loading, notification, freeing and sending. It stores
the supplied item and header callbacks, marks the subsystem active, frees an
existing array and resets its capacity, count, data, first index and newest
timestamp. It attempts to load the cache and calls notification on successful
load, then sends packet 0x46 with the resulting newest timestamp as a
little-endian dword. The function returns 1 in both tested load outcomes,
even when the hooked sender reports failure. This establishes the tested
call sequence, not actual cache, callback or socket behavior.
