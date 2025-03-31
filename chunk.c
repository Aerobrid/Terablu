#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

void initChunk(Chunk* chunk) {
    chunk->count = 0;       // No instructions added yet
    chunk->capacity = 0;    // No allocated space initially
    chunk->code = NULL;     // No memory allocated for bytecode
    chunk->lineCount = 0;    // <--
    chunk->lineCapacity = 0; // <--
    chunk->lines = NULL;    // No memory allocated for tracking line numbers
    initValueArray(&chunk->constants);                                  // Initialize the constants array
}

void freeChunk(Chunk* chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    freeValueArray(&chunk->constants);
    initChunk(chunk);                                                   // Resets chunk to the safe defaults
    FREE_ARRAY(LineStart, chunk->lines, chunk->lineCapacity);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
        // Don't grow line array here...
    }
  
    chunk->code[chunk->count] = byte;
    chunk->count++;
  
    // See if we're still on the same line.
    if (chunk->lineCount > 0 &&
        chunk->lines[chunk->lineCount - 1].line == line) {
        return;
    }
  
    // Append a new LineStart.
    if (chunk->lineCapacity < chunk->lineCount + 1) {
        int oldCapacity = chunk->lineCapacity;
        chunk->lineCapacity = GROW_CAPACITY(oldCapacity);
        chunk->lines = GROW_ARRAY(LineStart, chunk->lines, oldCapacity, chunk->lineCapacity);
    }
  
    LineStart* lineStart = &chunk->lines[chunk->lineCount++];
    lineStart->offset = chunk->count - 1;
    lineStart->line = line;
}

int getLine(Chunk* chunk, int instruction) {
    int start = 0;
    int end = chunk->lineCount - 1;
  
    for (;;) {
      int mid = (start + end) / 2;
      LineStart* line = &chunk->lines[mid];
      if (instruction < line->offset) {
            end = mid - 1;
        } else if (mid == chunk->lineCount - 1 ||
            instruction < chunk->lines[mid + 1].offset) {
            return line->line;
        } else {
            start = mid + 1;
        }
    }
}

int addConstant(Chunk* chunk, Value value) {
    writeValueArray(&chunk->constants, value);
    return chunk->constants.count - 1;                                 // Return the index where value is stored
}

void writeConstant(Chunk* chunk, Value value, int line) {
    int constant = addConstant(chunk, value);
    
    if (constant < 256) { // Fits in one byte
        writeChunk(chunk, OP_CONSTANT, line);
        writeChunk(chunk, (uint8_t)constant, line);
    } else { // Needs 3 bytes (24-bit index)
        writeChunk(chunk, OP_CONSTANT_LONG, line);
        // Uses bitwise operations 
        writeChunk(chunk, (uint8_t)(constant & 0xFF), line);        // Lower byte
        writeChunk(chunk, (uint8_t)((constant >> 8) & 0xFF), line); // Middle byte
        writeChunk(chunk, (uint8_t)((constant >> 16) & 0xFF), line); // Upper byte
    }
}