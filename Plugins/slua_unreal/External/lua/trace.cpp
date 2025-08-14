#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "trace.h"
#include "lobject.h"
#include "lstring.h"

namespace NS_SLUA {

static TraceEntry trace_buffer[TRACE_BUFFER_SIZE];
static size_t     buffer_pos   = 0;
static FILE*      symbol_file  = NULL;
static FILE*      trace_file   = NULL;
static int        symbol_atexit_registered = 0;
static int        trace_atexit_registered = 0;



// Helper: open a file in current working directory
static FILE* fopen_trace_output(const char* name, const char* mode) {
	return fopen(name, mode);
}


// Initialize symbol recording
void symbol_init(const char* filename) {
	// Close existing file if any
	if (symbol_file) {
		fclose(symbol_file);
		symbol_file = NULL;
	}
	
	symbol_file = fopen_trace_output(filename, "w");
	if (!symbol_file) {
		fprintf(stderr, "trace: Failed to create %s\n", filename);
		return;
	}
	
	// Register cleanup only once
	if (!symbol_atexit_registered) {
		atexit(symbol_cleanup);
		symbol_atexit_registered = 1;
	}
}

void symbol_cleanup(void) {
	if (symbol_file) {
		fclose(symbol_file);
		symbol_file = NULL;
		printf("trace: symbols.txt created.\n");
	}
}

void symbol_record(const Proto* f) {
	if (!symbol_file) return;
	
	if (f->source) {
		fprintf(symbol_file, "%u,%s:%d\n",
			f->trace_id,
			getstr(f->source),
			f->linedefined);
	}
	for (int i = 0; i < f->sizep; i++) {
		symbol_record(f->p[i]);
	}
}

// Opens trace.bin and registers cleanup at exit
void trace_init(void) {
	// Close existing file if any
	if (trace_file) {
		fclose(trace_file);
		trace_file = NULL;
	}
	
	trace_file = fopen_trace_output("trace.bin", "wb");
	if (!trace_file) {
		fprintf(stderr, "trace: Failed to create trace.bin\n");
		return;
	}
	
	printf("trace: trace.bin opened successfully\n");
	
	// Register cleanup only once
	if (!trace_atexit_registered) {
		atexit(trace_cleanup);
		trace_atexit_registered = 1;
	}
}

// Flushes buffer to file
static void flush_buffer(void) {
	if (buffer_pos == 0 || !trace_file) return;
	fwrite(trace_buffer, sizeof(TraceEntry), buffer_pos, trace_file);
	fflush(trace_file);
	buffer_pos = 0;
}

// Cleanup: flush and close 
void trace_cleanup(void) {
	if (trace_file) {
		flush_buffer();
		fclose(trace_file);
		trace_file = NULL;
		printf("trace: trace.bin created.\n");
	}
}

// Captures trace_id, event and timestamp, then stores in buffer
void trace_record(const Proto* p, uint8_t event) {
	if (!trace_file) return;

	uint16_t id = p->trace_id;
	printf("trace: recording event %d for trace_id %d\n", event, id);

	// Get timestamp
	uint64_t timestamp;
#ifdef _WIN32
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	// Convert Windows FILETIME to nanoseconds since epoch
	uint64_t winTime = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	// Windows epoch is 1601-01-01, Unix epoch is 1970-01-01
	// Difference is 11644473600 seconds
	timestamp = (winTime - 116444736000000000ULL) * 100; // Convert to nanoseconds
#else
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	timestamp = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif

	// Write entry to buffer
	TraceEntry entry;
	entry.id = id;
	entry.event = event;
	memset(entry._pad, 0, sizeof(entry._pad));
	entry.timestamp = timestamp;
	trace_buffer[buffer_pos++] = entry;

	// If buffer is full, flush to file
	if (buffer_pos >= TRACE_BUFFER_SIZE) {
		flush_buffer();
	}

	// Force flush every record to ensure data is written during PIE/editor runs
	flush_buffer();
}

} // end NS_SLUA

 