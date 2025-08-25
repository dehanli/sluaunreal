# lua-trace Project

This project is mainly a modification to Lua Source, integrated to the slua_unreal plugin for Unreal Engine. It implements the functionality of recording Lua Closure calls/returns at the boundary of Lua VM and write events to `trace.bin`, grouped by UE frames. Python scripts are also provided to decode `trace.bin` to a human-readable format.

Author: LI Dehan (dehanli@tencent.com)

## Overview
- **Symbol mapping**: outputs `symbols.txt`, mapping `trace_id -> source:linedefined` for all Protos.

- **New files**: implement the symbol writer and tracer (buffering, frame headers, file output) in  `External/lua/ltrace.h`, `External/lua/ltrace.cpp`.

- **Hook points** : tracer is called from `luaD_precall` (CALL) and `luaD_poscall` (RETURN) for tracing LClosures only.

- **Working with `slua_unreal`**: `LuaState.cpp` finalizes each UE tick by calling `trace_flush_with_timestamp;` at the end of `LuaState::Tick(float dtime)` creating one FRAME header per tick and flushes buffered events.

- **Python decoders**: `scripts/parse_trace_bin.py` is for binary -> text events + frame headers, while `scripts/decode_trace.py` join with `symbols.txt` to show file:line and function names.


##  `symbols.txt`: Symbol Mappping of Protos

### Output Location
Unreal Engine's working directory. e.g. `C:\Program Files\Epic Games\UE_4.26\Engine\Binaries\Win64\symbols.txt`

### Format
- One entry per Lua Proto (recursively), emitted at successful `lua_load` time.
- Text, one line per entry:
  - `trace_id,source_label:line_number`
    - `trace_id`: unsigned decimal, from `Proto->trace_id` (assigned during parse/load)
    - `source_label`: the chunk source string (e.g. `@path.lua`, `=stdin`, `[sluacode]`)
    - `line_number`: `Proto->linedefined` (0 for main chunks)
- Example: `78,@/Project/Content/Lua/Test.lua:71`

## `trace.bin`: Binary Output

### Output Location
Unreal Engine's working directory. e.g. `C:\Program Files\Epic Games\UE_4.26\Engine\Binaries\Win64\trace.bin`

### Format
- a sequence of records; each frame begins with a 16‑byte header:
  - `"FRME"` (4 bytes)
  - `timestamp_us` (8 bytes, `=Q` little‑endian) — UTC epoch microseconds
  - `frame_id` (4 bytes, `=I` little‑endian)
- Followed by N entries of 2 bytes each (little-endian) until the next frame header:
  - Layout per entry: `[ id(15 bits) | event(1 bit) ]`
  - `event=0` -> CALL, `event=1` -> RETURN
  - `id` equals `Proto->trace_id` and matches `symbols.txt`
- Frames are opened lazily on first event and finalized when `trace_flush_with_timestamp` is called (timestamp/frame_id back‑filled), then data is flushed.

## Runtime behavior
- A frame header is emitted lazily on first event per tick; timestamp/frame_id are back‑filled on flush.
- Events are buffered (`TRACE_BUFFER_SIZE=4096`) and flushed either when full or at end of frame via `trace_flush_with_timestamp(now_us())`.
- Only Lua closures are recorded; C closures/native calls are ignored.

## Decoding `trace.bin`

When decoding, make sure to run from the directory containing `trace.bin` and `symbols.txt`.

1) Binary -> text:
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

## Detailed table of changed contents
### Lua Source
- `lapi.cpp` - record symbol in `lua_load`
- `ldo.cpp` - trace call and return here
- `ldump.cpp` - DumpVar(f->trace_id, D);
- `lundump.cpp` - LoadVar(S, f->trace_id);
- `linit.cpp` - initialize symbol and trace logic TODO: could put elsewhere?
- `lparser.cpp` - trace_id assignment & increment (two modifications since there are two places Protos created)

- `lobject.h` - add field to struct Proto 

- `lua/README_ltrace.md` - lua-trace project README file
- `ltrace.h` - new file
- `ltrace.cpp` - new file

### `slua_unreal`
- `Plugins/slua_unreal/CMakeLists.txt` - add new files to build
- `Plugins/slua_unreal/Source/slua_unreal/Private/LuaState.cpp` - flush on tick logic 

### scripts
`scripts/decode_trace.py`
`scripts/parse_trace_bin.py`

## Caveats

- **CALL without RETURN**: Calls like `Test.lua:update(dt)` invoked from C++ each frame may appear as repeated CALLs without matching RETURN, if the return crosses the C/Lua boundary (RETURN not emitted by the Lua->C path). Function calls within Lua show paired CALL/RETURN.

- **Explicit flush of table entry**: Entries are flushed immediately since I found in UE PIE Mode, `atexit` doesn't run when you stop PIE, without the explicit flush, some table entries are interrupted, leading to unknown symbols at runtime.