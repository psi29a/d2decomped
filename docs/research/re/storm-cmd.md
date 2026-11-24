# Storm's command-line parser (SOURCE/SCMD.CPP)

Traced 2026-11-23 by reading game.exe 1.14d, not run: the decompile, the
assembly for register arguments and `ret n`, `game-calls.tsv` for
callers, and a byte scan of the whole exe for pointers and `E8`/`E9`
calls to each function with no listed caller. The module is
`0x6c3cb0..0x6c50cc`: the `.\SOURCE\SCMD.CPP` entries in
`game-srcfiles.tsv` and the functions between them. Its source file name
appears at lines 0x8b, 0x8c, 0x120, 0x121, 0x153, 0x164 and 0x212, and
its allocation tag is `.?AUCMDDEF@@` (`0x74c47c`).

```sh
python3 tools/ghidra/fn.py 006c3cb0 006c3d20 006c3da0 006c3e10 006c3f50 006c42e0 006c4580
python3 tools/ghidra/fn.py 006c4820 006c4980 006c4a00 006c4bc0 006c4e50 006c4ec0 006c4fb0
python3 tools/ghidra/calls.py 0044b6b0 --to 006c4e50
python3 tools/ghidra/calls.py 00451260 --to 006c4580
```

d2d has no counterpart. Its switches are game.exe's own table
([switches.md](switches.md)), read by `components/switches` and CLI11.
None of the switches below is in that table.

## The definition record (`CMDDEF`, 0x40 bytes)

| Offset | Field |
|---|---|
| +0x00 | link: previous |
| +0x04 | link: next (a negative value is the `~`-tagged list terminator) |
| +0x08 | flags: `& 0x30000` the type (0 bool, 0x10000 number, 0x20000 string); `& 0x3000000` the kind (0 a named switch, 0x1000000 an optional positional argument, 0x2000000 a required one); 0x100 a case-sensitive name; bit 0 a bool that starts on (a bare switch turns it off), or a signed number |
| +0x0c | id |
| +0x10 | name, 16 bytes |
| +0x20 | name length |
| +0x24 | set value (what a bool ORs in) |
| +0x28 | set mask (what a bool clears first) |
| +0x2c | the caller's variable, or 0 |
| +0x30 | its size |
| +0x34 | callback, or 0 |
| +0x38 | seen: 1 once a value was applied |
| +0x3c | the value: a dword, or for a string a copy the module owns |

Two lists hold the records, each with a link offset of 0. One is
`0x74c498` (head `0x74c4a0`) for named switches; the other is `0x74c48c`
(head `0x74c494`) for positional arguments, in the order they were
registered. Lookups by id walk `0x74c48c` first, then `0x74c498`.
`DAT_00989c38` is set once an optional positional argument is registered.
Two static destructors, `LAB_006cb4f0` and `LAB_006cb550`, unlink the
nodes of each list at exit (`FUN_006c43f0`) without freeing them.

## Registering

`FUN_006c4580(flags, id, name, var, size, set value, set mask, callback)`
(stack, 8 arguments) fails with error 0x57 and returns 0 when any of these hold:

- the name is longer than 15 characters (a null name is read as "");
- a size is given with no variable;
- a required positional argument comes after an optional one
  (`DAT_00989c38`);
- a named switch has an empty name;
- a bool has a variable whose size isn't 4.

Otherwise it allocates a 0x40-byte record (tag `.?AUCMDDEF@@`) and links
it at the tail of its list. It fills in the fields above. The value
starts at the set value for a bool with bit 0, else at 0. It returns 1.

`FUN_006c4e50(entries, count)` registers an array of 16-byte entries
{flags, id, name, callback}, each as
`FUN_006c4580(flags, id, name, 0, 0, 1, 0xffffffff, callback)`. It stops
at the first failure; a null array is error 0x57.

## Processing a line, in call order

`FUN_006c4fb0(text, skip first, extra callback, error callback)` (stack,
`ret 0x10`):

- a null text is error 0x57;
- with `skip first`, the first token (the program name) is skipped;
- a 0x18-byte parse state {pending switch, name[16], matched length} is
  zeroed;
- the positional cursor starts at the first positional record;
- `FUN_006c4ec0` processes the text.

If a required positional argument is left at the cursor, it reports
0x8510006d and returns 0. Otherwise it returns 1.

`FUN_006c4ec0` (`ECX` the text pointer's address, `EDX` the state, stack:
the cursor's address, the extra and error callbacks) splits the text with
`str_token` (`FUN_00413b80`) at any of `" ,;\"\t\n\r\x1a"` (`0x6fcc4c`).
Each non-empty token goes to `FUN_006c4bc0` with its quoted flag. It
stops at the first failure and returns 1 only when the whole text was
read.

`FUN_006c4bc0` handles one token (`EDX` the token, `ECX` the state, `EAX`
the cursor's address). The first case that applies wins:

- **`@` and not quoted:** `FUN_006c3f50` reads the rest as a response
  file.
- **`-` or `/` (`0x6fcc48`) and not quoted:** the pending switch is
  cleared and `FUN_006c4a00` parses a switch.
- **A switch is pending:** the token is its value (`FUN_006c4980`).
- **The positional cursor is set:** the token is that argument's value
  (`FUN_006c4820`), and the cursor moves to the next record (0 at the
  end).
- **Otherwise:** the extra callback gets the token and its result is
  returned; with no extra callback, 0x85100065 is reported with the
  token and 0 returned.

`FUN_006c4a00` (`EDX` the text after the dash, `ECX` the error callback,
stack: the state) finds the longest registered switch name that prefixes
the text, with `FUN_006c42e0` on the named list. An unknown name reports
0x85100065 with the rest of the text and returns 0. After a match, any
`=` or `:` (`0x6fcc44`) is skipped and the record, the name and its
length go in the state. At the end of the token a number or string switch
is left pending, so the next token is its value. Otherwise
`FUN_006c4980` applies the rest as the value and the loop goes on with
what the value didn't consume, so `-cr` sets two bools. The next name is
matched with the previous one's leading characters in front, all of them
but the last first, then fewer, down to none
(`0x6c4a83`..`0x6c4adb`). But the text then moves on by the whole
matched name (`0x6c4ae8`..`0x6c4aec`), counting characters that came
from the previous name: bugs.md #134. game.exe's own switches can't
reach it (its only longer bool, `help`, stops the parse).

`FUN_006c42e0` (`EAX` the first record, `EDI` the text, stack: a minimum
length) returns the record whose name is the longest prefix of the text.
The name must be at least the minimum and at most the text's length.
`strncmp` is used with flag 0x100, else `_strnicmp`. It returns 0 for no
match.

`FUN_006c4980` (`ESI` the state, stack: the text and the consumed count
out) takes the pending switch out of the state and applies the text to
it. It then applies the text to every later record registered under the
same name: `FUN_006c42e0` from the next record, with the state's name
and length as the minimum. The consumed count is the largest of these
calls.

`FUN_006c4820` applies a value to one record (`EBX` the record, `EAX` the
text, `EDX` the consumed count out). It parses by type:

- `FUN_006c3cb0` bool:
  - `-` turns it off and `+` on, each consuming one character;
  - anything else turns it on, or off with flag bit 0, consuming
    nothing;
  - the value and the variable get `(x & ~mask) | (on ? set value : 0)`.
- `FUN_006c3d20` number: `strtol` with flag bit 0, else `strtoul`, base
  0. The consumed count is up to where parsing stopped, or the whole
  length. min(size, 4) bytes go into the variable.
- `FUN_006c3da0` string: the whole rest is consumed. The value is a new
  copy (the old one freed, lines 0x8b/0x8c), also copied into the
  variable up to its size.

Any other type returns 0. Then it sets seen and calls the callback
(`0x6c48a5`..`0x6c48ad`, stdcall) with a 0x1c-byte record {flags, id,
name, variable, set value, set mask, value} and the text; a callback
returning 0 fails it. Every other record with the
same id and type, in both lists, then gets seen and the same value, a
string as its own copy (lines 0x120/0x121).

`FUN_006c3f50` (`EAX` the file name, stack: state, cursor, extra and
error callbacks) opens the file with `CreateFileA` (read, shared read,
open existing, sequential scan); a failure reports 0x6e and returns 0.
The whole file is read into a buffer (line 0x153) with a 0 after it,
processed with `FUN_006c4ec0` in the same state, and freed (line 0x164).
A response file can name another one.

`FUN_006c3e10` reports an error (`EDI` the code, `EBX` the text, stack:
the error callback). It picks a message by code:

| Code | String id | Built-in text |
|---|---|---|
| 0x85100065 | 0x5201 | `Invalid argument: %s` |
| 0x8510006d | 0x5202 | `The syntax of the command is incorrect.` |
| 0x6e | 0x5203 | `Unable to open response file: %s` |

Any other code returns without a message.

The string is loaded with `LoadStringA` from Storm's module handle
(`FUN_0041bef0`). The built-in text (`0x6fcc24`) is used when the load
leaves it empty. Whether game.exe has those string resources wasn't
checked. A message containing `%s` is formatted with the text, and a
non-empty one gets `\n` appended. Then it sets the error code
(`FUN_0041c340`) and calls the callback with {code, text, message}.

## Reading values back, and teardown

Nothing reaches these: each has no caller, except `FUN_006c4d70`, whose
only caller `FUN_006c4f90` has none (the table at the end):

- `FUN_006c4ca0(id)`: the seen flag.
- `FUN_006c4d70(id)`: the value.
- `FUN_006c4f90(id)`: whether the value is non-zero.
- `FUN_006c4dc0(id, buffer, size)` copies a string value.
  - The buffer is cleared first. A null buffer or a size of 0 is error
    0x57.
  - It returns 1 for a known id, else 0.
- `FUN_006c4cf0` frees every string value (line 0x212), deletes the
  records of both lists and clears `DAT_00989c38`.

## How the game uses it

`app_args_init` (`FUN_0044b6b0`, unnamed-frontier.md) registers the
eight entries at `0x6d5be8` (`FUN_006c4e50`). It then runs its init table
`0x6d5be0`, whose one entry, `FUN_00451260`, registers `lng` (a string,
id -1, into `DAT_007a0710`, 0x104 bytes). Last, it processes the
registry's `Diablo II\Default Command Line` with no program name to
skip: `FUN_006c4fb0(line, 0, LAB_0044b5c0, LAB_0044b5d0)`. Those are the
module's only callers in game.exe (byte scan: calls at `0x44b6c1`,
`0x44b724`, `0x45127c`). `FUN_006c50b0`, which would process
`GetCommandLineA()`, has no caller. So these switches are read from that
registry value only, never from the real command line.

The entries at `0x6d5be8` (id -1 each):

| Name | Type | Callback | What it does |
|---|---|---|---|
| `?`, `h`, `help` | bool | `FUN_0044b4e0` | Once (`DAT_007a0448`), a message box listing `-set` and `-lng`, shown unless the Windows user name is `rseis` (`0x44b51c`: `str_ncmp` 0 skips it); returns 0, which stops the parse |
| `set` | string | `FUN_0044b5e0` | Writes the value to the registry's `Diablo II\Default Command Line` (`FUN_00415070`) |
| `mode` | string | `FUN_0044b600` | The value matched without case against the 7 names at `0x70ee70` (only slot 2, `Game`, is set in the file); a match stores its index in `0x70ee4c`, else the help box and 0 |
| `c` | bool | `LAB_0044b550` | Allocates 0x6000000 bytes (`FUN_0040b380`, `.\CORE\WINMAIN.CPP` line 0x121), writes a 0 every 0x1000 bytes and frees it (line 0x124) |
| `r` | bool | `LAB_0044b5a0` | `LAB_004519a0`: clears `DAT_007a2784` when set, else sets `DAT_007a277c`, the flag `record_on` (`FUN_00451910`) returns |
| `p` | bool | `LAB_0044b5b0` | `LAB_004518e0`: clears `DAT_007a277c` when set, else sets `DAT_007a2784` and calls `FUN_00451710` and `FUN_0052c320(DAT_007a2778)` |

`LAB_0044b5c0` (the extra callback: a token that is no switch) shows the
help box and returns 0. `LAB_0044b5d0` (the error callback) shows it
too.

## The functions

Callers from `game-calls.tsv`; "orphan" means no caller and no pointer
anywhere in the exe (byte scan), "orphan's callee" a function called only
by orphans.

| Address | Reading | Callers |
|---|---|---|
| `FUN_006c3cb0` | Bool parse (see above) | `FUN_006c4820` |
| `FUN_006c3d20` | Number parse | `FUN_006c4820` |
| `FUN_006c3da0` | String parse | `FUN_006c4820` |
| `FUN_006c3e10` | Error report | `FUN_006c3f50`, `FUN_006c4a00`, `FUN_006c4bc0`, `FUN_006c4fb0` |
| `FUN_006c3f50` | Response file | `FUN_006c4bc0` |
| `FUN_006c4040` | List template: a link's next pointer as a link address (the terminator decoded; the offset from the argument, or from the previous link when negative) | orphan |
| `FUN_006c4160` | List template: the given link, or the list's own when null | orphan |
| `FUN_006c4180` | List template: unlinks a node, then links it beside a neighbour (default the terminator), after it with mode 1, else before | orphan's callee (`FUN_006c44c0`) |
| `FUN_006c4230` | List template: a node's next node, or the first when null; 0 at the terminator | orphan |
| `FUN_006c4290` | List template: unlinks a node (default the terminator) | orphan |
| `FUN_006c42e0` | Longest-prefix name lookup | `FUN_006c4980`, `FUN_006c4a00` |
| `FUN_006c4380` | List template: a node's raw next field, or the list's first | orphan |
| `FUN_006c43f0` | List template: unlinks every node without freeing | `FUN_006c4430`, the static destructors `LAB_006cb4f0`, `LAB_006cb550` |
| `FUN_006c4430` | List template destructor: `FUN_006c43f0`, then unlinks the terminator | orphan |
| `FUN_006c44c0` | List template: a new node of 0x40 + n bytes (tag `.?AUCMDDEF@@`, flags \| 8), links zeroed, linked by `FUN_006c4180` when a neighbour is given | orphan |
| `FUN_006c4520` | Node deleting destructor: unlinks, freed (tag `delete`) with bit 0 | orphan |
| `FUN_006c4580` | Register one definition | `FUN_00451260`, `FUN_006c4e50` |
| `FUN_006c4760` | List template: unlinks and frees every node | orphan's callee (`FUN_006c4cf0`) |
| `FUN_006c47b0` | List template: unlinks and frees one node, returns its next (0 at the end) | orphan |
| `FUN_006c4820` | Apply a value to a record | `FUN_006c4980`, `FUN_006c4bc0` |
| `FUN_006c4980` | Apply the pending switch | `FUN_006c4a00`, `FUN_006c4bc0` |
| `FUN_006c4a00` | Switch parse | `FUN_006c4bc0` |
| `FUN_006c4bc0` | One token | `FUN_006c4ec0` |
| `FUN_006c4ca0` | Seen flag by id | orphan |
| `FUN_006c4cf0` | Teardown | orphan |
| `FUN_006c4d70` | Value by id | orphan's callee (`FUN_006c4f90`) |
| `FUN_006c4dc0` | String value by id | orphan |
| `FUN_006c4e50` | Register an array | `app_args_init` |
| `FUN_006c4ec0` | Tokenise and process | `FUN_006c3f50`, `FUN_006c4fb0` |
| `FUN_006c4f90` | Value is non-zero | orphan |
| `FUN_006c4fb0` | Process a line | `app_args_init`, `FUN_006c50b0` |
| `FUN_006c50b0` | Process `GetCommandLineA()`, skipping the program name | orphan |
