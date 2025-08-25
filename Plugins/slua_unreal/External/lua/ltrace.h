#ifndef ltrace_h
#define ltrace_h

#include <stdint.h>
#include "lobject.h"

// Lua trace: records Lua closure CALL/RETURN at the VM boundary and writes
// 2-byte entries to trace.bin grouped by per-tick frame headers.
// Also writes symbols.txt mapping trace_id -> source:linedefined.

namespace NS_SLUA {

#define TRACE_BUFFER_SIZE 4096

// Event identifiers from the Lua VM; output normalizes to 1-bit (call=0, return=1)
typedef enum TraceEventType {
	TRACE_EVENT_CALL = 1,
	TRACE_EVENT_RETURN = 2
} TraceEventType;

// 16 bits total: [ id (15) | event (1) ]
typedef struct TraceEntry {
	uint8_t bytes[2];
} TraceEntry;

static constexpr int TRACE_EVENT_BITS = 1;
static constexpr int TRACE_ID_BITS    = 15;

// Pack id + event_bit into 2 bytes (little-endian)
static inline TraceEntry trace_pack(uint16_t id, uint8_t event_bit) {
	TraceEntry e;
	uint16_t v = (uint16_t)(((uint16_t)(id & ((1u << TRACE_ID_BITS) - 1)) << TRACE_EVENT_BITS) | (uint16_t)(event_bit & 0x1u));
	e.bytes[0] = (uint8_t)(v & 0xFFu);
	e.bytes[1] = (uint8_t)((v >> 8) & 0xFFu);
	return e;
}

// Symbols API: write "trace_id,source:line" entries to symbols.txt
// Call symbol_init() before recording; symbol_cleanup() closes the file.
void symbol_record(const Proto* f);
void symbol_init(const char* filename);
void symbol_cleanup(void);

// Trace API: capture events, manage per-tick frames, and write trace.bin
void trace_init(void);
void trace_record(const Proto* p, uint8_t event);
void trace_flush(void);
void trace_flush_with_timestamp(uint64_t timestamp_us);
void trace_cleanup(void);

} // end NS_SLUA

#endif 