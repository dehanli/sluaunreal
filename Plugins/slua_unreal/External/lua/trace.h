#ifndef trace_h
#define trace_h

#include <stdint.h>
#include "lobject.h"

namespace NS_SLUA {

#define TRACE_BUFFER_SIZE 4096

// Event type for a trace entry
typedef enum TraceEventType {
	TRACE_EVENT_CALL = 1,
	TRACE_EVENT_RETURN = 2
} TraceEventType;

typedef struct TraceEntry {
	uint16_t id;        // trace_id of the Proto
	uint8_t  event;     // TraceEventType: call or return
	uint8_t  _pad[5];   // padding to keep 16-byte struct alignment
	uint64_t timestamp; // nanoseconds since epoch
} TraceEntry;

// Symbol recording for debugging/analysis
void symbol_record(const Proto* f);
void symbol_init(const char* filename);
void symbol_cleanup(void);

void trace_init(void);
void trace_record(const Proto* p, uint8_t event);
void trace_cleanup(void);

} // end NS_SLUA

#endif 