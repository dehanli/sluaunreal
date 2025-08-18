#ifndef trace_h
#define trace_h

#include <stdint.h>
#include "lobject.h"

namespace NS_SLUA {

#define TRACE_BUFFER_SIZE 4096

// Event type for an entry: CALL or RETURN
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



void symbol_record(const Proto* f);
void symbol_init(const char* filename);
void symbol_cleanup(void);

void trace_init(void);
void trace_record(const Proto* p, uint8_t event);
void trace_cleanup(void);

} // end NS_SLUA

#endif 