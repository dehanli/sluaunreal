#!/usr/bin/env python3
"""
Lua trace decoder.

Reads text events from stdin produced by scripts/parse_trace_bin.py and enriches
them using symbols.txt to show file:line and function names.

Usage:
  python3 scripts/parse_trace_bin.py | python3 scripts/decode_trace.py

Input:
- symbols.txt: mapping trace_id -> source_label:line
- stdin: lines like [index] event=call|return trace_id=X

Output:
- FRAME id=<n> ts=<ISO8601>
- [index] EVENT trace_id=X  file:line (function_name)
"""

import re
import sys
import os

# Configurations
SYMBOL_TABLE_FILE = "symbols.txt"

def extract_function_names_from_file(filename):
    """Extract function names and their line numbers from a Lua source file"""
    function_map = {}
    
    try:
        with open(filename, 'r') as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                # Match Lua function definitions: function name() or function name()
                match = re.match(r'^function\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(', line)
                if match:
                    function_name = match.group(1)
                    function_map[line_num] = function_name
    except FileNotFoundError:
        # If file doesn't exist, return empty map
        pass
    
    return function_map

def parse_symbol_table(symbol_table_file):
    """Parse the symbol table file to get trace_id -> (file, line) mapping"""
    trace_map = {}
    
    with open(symbol_table_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            
            # Parse format: trace_id,source_label:line_number
            # Accept any source label (e.g. @file.lua, =custom, [sluacode], [ProfilerScript], etc.)
            match = re.match(r'^(\d+),(.*):(\d+)$', line)
            if match:
                trace_id = int(match.group(1))
                filename = match.group(2)
                line_num = int(match.group(3))
                
                # Keep all entries, including line 0 and synthetic code entries
                trace_map[trace_id] = (filename, line_num)
    
    return trace_map

def get_function_name_for_line(filename, line_num, function_cache):
    """Get function name for a specific line number in a file"""
    if filename not in function_cache:
        function_cache[filename] = extract_function_names_from_file(filename)
    
    # Find the function that starts at or before this line
    function_name = None
    closest_line = 0
    
    for func_line, func_name in sorted(function_cache[filename].items()):
        if func_line <= line_num and func_line > closest_line:
            function_name = func_name
            closest_line = func_line
    
    return function_name

def decode_trace_output(trace_output, trace_map):
    """Decode trace output replacing trace_ids with filename:line_number and function names"""
    function_cache = {}  # Cache function maps for each file
    
    for line in trace_output:
        line = line.strip()
        if not line:
            continue
        
        # Parse trace line format: [index] event=call|return trace_id=X
        match = re.match(r'\[(\d+)\] event=(\w+) trace_id=(\d+)$', line)
        if match:
            index = match.group(1)
            event = match.group(2)
            trace_id = int(match.group(3))
            
            if trace_id in trace_map:
                filename, line_num = trace_map[trace_id]
                
                if line_num == 0:
                    # Handle line 0 entries (main chunks and synthetic functions)
                    if filename == "(luac)":
                        location = f"{filename}:0\t(luac wrapper function)"
                    elif filename == "(load)":
                        location = f"{filename}:0\t(load function)"
                    elif filename == "(string)":
                        location = f"{filename}:0\t(string evaluation)"
                    elif filename.startswith("(") and filename.endswith(")"):
                        # Handle any other synthetic code cases
                        location = f"{filename}:0\t(synthetic code)"
                    else:
                        location = f"{filename}:0\t(main chunk)"
                else:
                    # Normal entries - try to get function name
                    location = f"{filename}:{line_num}"
                    function_name = get_function_name_for_line(filename, line_num, function_cache)
                    if function_name:
                        location = f"{filename}:{line_num}\t({function_name})"
            else:
                # Fallback for trace_ids not found in symbol table
                location = f"unknown\t(not found)"
            
            print(f"[{index}] {event.upper():<6} trace_id={trace_id:<3} {location}")
        else:
            # Pass through non-event lines as-is (e.g., frame headers)
            print(line)  # Print unmatched lines as-is

def main():
    symbol_table_file = SYMBOL_TABLE_FILE
    
    try:
        # Parse symbol table
        trace_map = parse_symbol_table(symbol_table_file) # trace_id -> (filename, line_num)
        
        # Read trace data from stdin
        print("Reading trace data from stdin...")
        print("Decoded trace output:")
        trace_lines = []
        for line in sys.stdin:
            trace_lines.append(line.rstrip('\n'))
        
        decode_trace_output(trace_lines, trace_map)
        
    except FileNotFoundError:
        print(f"Error: Symbol table file '{symbol_table_file}' not found.")
        print("Make sure the symbols.txt file exists in the current directory.")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main() 