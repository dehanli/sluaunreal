# lua-trace Feature

Record Lua Closure calls/returns at the Lua VM boundary and write events to `trace.bin`, grouped by UE frames. Provide Python tools to decode to human-readable output with file:line/function names.

## Changes made
- **New files**: `External/lua/ltrace.h`, `External/lua/ltrace.cpp` implementing the tracer (buffering, frame headers, file output) and symbol writer.
- **Hook points** : tracer is called from `luaD_precall` (CALL) and `luaD_poscall` (RETURN) for Lua closures only.
- **Symbols**: write `symbols.txt` mapping `trace_id -> source:linedefined` for all Protos.
- **Python decoders**: `scripts/parse_trace_bin.py` (binary → text events + frame headers), `scripts/decode_trace.py` (join with `symbols.txt` to show file:line and function names).
- **Engine integration**: In `LuaState.cpp` include `ltrace.h` and finalize each UE tick by calling `trace_flush_with_timestamp;` at the end of `LuaState::Tick(float dtime)`. This creates one FRAME header per tick and flushes buffered events.

## Binary output
- File is a sequence of records; each frame begins with a 16‑byte header:
  - `"FRME"` (4 bytes)
  - `timestamp_us` (8 bytes, `=Q` little‑endian) — UTC epoch microseconds
  - `frame_id` (4 bytes, `=I` little‑endian)
- Followed by N 2‑byte entries:
  - Layout: `[ id(15 bits) | event(1 bit) ]`, little‑endian
  - `event=0` → CALL, `event=1` → RETURN

## Runtime behavior
- A frame header is emitted lazily on first event per tick; timestamp/frame_id are back‑filled on flush.
- Events are buffered (`TRACE_BUFFER_SIZE=4096`) and flushed either when full or at end of frame via `trace_flush_with_timestamp(now_us())`.
- Only Lua closures are recorded; C closures/native calls are ignored.

## Generated files
- `symbols.txt`: lines of `trace_id,source_label:line_number`
- `trace.bin`: binary event stream with per‑frame headers

## Decoding binary
1) Binary → text:
```bash
python3 scripts/parse_trace_bin.py
```
Outputs:
- `FRAME id=<n> ts=<ISO8601>`
- `[index] event=call|return trace_id=<id>`

2) Enrich with file:line + function names:
```bash
python3 scripts/parse_trace_bin.py | python3 scripts/decode_trace.py > decoded.txt
```
- `decode_trace.py` loads `symbols.txt`, maps `trace_id` to `source:line`, and infers function names by scanning Lua files.

## Caveats
- Calls like `Test.lua:update(dt)` invoked from C++ each frame may appear as repeated CALLs without matching RETURN if the return crosses the C/Lua boundary (RETURN not emitted by the Lua->C path). Intra‑Lua calls show paired CALL/RETURN.
- When decoding, make sure to run from the directory containing `trace.bin` and `symbols.txt`.
- One `FRAME` header is produced per UE tick (on first event of the tick), then finalized and flushed at the end of `LuaState::Tick`.
