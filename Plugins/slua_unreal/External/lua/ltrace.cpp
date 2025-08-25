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

#include "ltrace.h"
#include "lobject.h"
#include "lstring.h"

// Lua trace runtime: writes per-frame headers and 2-byte entries to trace.bin.
// Manages symbols.txt lifecycle. See README_ltrace.md for binary format.

namespace NS_SLUA {

static TraceEntry trace_buffer[TRACE_BUFFER_SIZE];
static size_t     buffer_pos   = 0;
static FILE*      symbol_file  = NULL;
static FILE*      trace_file   = NULL;
static int        symbol_atexit_registered = 0;
static int        trace_atexit_registered = 0;

// Track current per-tick frame being written
static int        frame_open = 0;
static long       frame_header_pos = 0; // file offset where header starts
static uint32_t   frame_id_counter = 0;
static uint32_t   current_frame_id = 0;

// Helper: open a file in current working directory
static FILE* fopen_trace_output(const char* name, const char* mode) {
	return fopen(name, mode);
}

// Frame header: magic(4) + timestamp_us(8) + frame_id(4)
static const uint8_t FRAME_MAGIC[4] = { 'F','R','M','E' };

// Get current wall-clock time in microseconds since Unix epoch
static uint64_t now_us(void) {
#ifdef _WIN32
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
	// Convert from 100-ns intervals since Jan 1, 1601 to microseconds since Unix epoch
	return (t - 116444736000000000ULL) / 10ULL;
#else
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000ULL);
#endif
}

// Lazily begin a frame: write placeholder header; entries follow immediately
static void frame_begin_if_needed() {
	if (frame_open || !trace_file) return;
	frame_open = 1;
	current_frame_id = ++frame_id_counter;
	// Record header start position
	frame_header_pos = ftell(trace_file);
	// Write magic and placeholder for timestamp and frame_id
	fwrite(FRAME_MAGIC, sizeof(FRAME_MAGIC), 1, trace_file);
	uint64_t zero_ts = 0;
	uint32_t zero_frame_id = 0;
	fwrite(&zero_ts, sizeof(zero_ts), 1, trace_file);
	fwrite(&zero_frame_id, sizeof(zero_frame_id), 1, trace_file);
}

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

void trace_init(void) {
	// Close existing file if any
	if (trace_file) {
		fclose(trace_file);
		trace_file = NULL;
	}
	
	trace_file = fopen_trace_output("trace.bin", "wb");
	if (!trace_file) {
		fprintf(stderr, "ltrace: Failed to create trace.bin\n");
		return;
	}
	
	printf("ltrace: trace.bin opened successfully\n");
	
	// Register cleanup only once
	if (!trace_atexit_registered) {
		atexit(trace_cleanup);
		trace_atexit_registered = 1;
	}
}

// Write buffered 2-byte entries to file
void trace_flush(void) {
	if (buffer_pos == 0 || !trace_file) return;
	// Write contiguous 2-byte entries
	for (size_t i = 0; i < buffer_pos; ++i) {
		fwrite(trace_buffer[i].bytes, sizeof(trace_buffer[i].bytes), 1, trace_file);
	}
	fflush(trace_file);
	buffer_pos = 0;
}

// Public API: finalize current frame (backfill timestamp and frame_id), then flush entries
void trace_flush_with_timestamp(uint64_t timestamp_us) {
	if (!trace_file || !frame_open) return;
	// Backfill timestamp and frame_id
	long cur = ftell(trace_file);
	fseek(trace_file, frame_header_pos + (long)sizeof(FRAME_MAGIC), SEEK_SET);
	fwrite(&timestamp_us, sizeof(timestamp_us), 1, trace_file);
	fwrite(&current_frame_id, sizeof(current_frame_id), 1, trace_file);
	// Return to end and write buffered entries
	fseek(trace_file, cur, SEEK_SET);
	trace_flush();
	printf("trace: frame %u flushed at %llu us\n", (unsigned)current_frame_id, (unsigned long long)timestamp_us);
	frame_open = 0;
	current_frame_id = 0;
}

// Cleanup: flush any open frame and close file
void trace_cleanup(void) {
	if (trace_file) {
		// Flush any open frame as a final frame
		trace_flush_with_timestamp(now_us());
		fclose(trace_file);
		trace_file = NULL;
		printf("ltrace: trace.bin created.\n");
	}
}

// Capture trace_id + event, normalize to 1-bit (call=0, return=1), ensure a frame is open, then buffer
void trace_record(const Proto* p, uint8_t event) {
	if (!trace_file) return;

	uint16_t id = p->trace_id;

	// Normalize event to 1-bit: call=0, return=1
	uint8_t event_bit = (event == TRACE_EVENT_RETURN) ? 1 : 0;

	// Ensure a frame is open (write placeholder header if not)
	frame_begin_if_needed();
	TraceEntry entry = trace_pack(id, event_bit);
	trace_buffer[buffer_pos++] = entry;

	// If buffer is full, flush mid-tick
	if (buffer_pos >= TRACE_BUFFER_SIZE) {
		trace_flush();
	}
}

} // end NS_SLUA
