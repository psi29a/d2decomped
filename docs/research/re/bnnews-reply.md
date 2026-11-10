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
