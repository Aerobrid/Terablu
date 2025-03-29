#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

void initChunk(Chunk* chunk) {
    chunk->count = 0;       // No instructions added yet
    chunk->capacity = 0;    // No allocated space initially
    chunk->code = NULL;     // No memory allocated for bytecode
    chunk->lines = NULL;    // No memory allocated for tracking line numbers
    initValueArray(&chunk->constants);                                  // Initialize the constants array
}

void freeChunk(Chunk* chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    freeValueArray(&chunk->constants);
    initChunk(chunk);                                                   // Resets chunk to the safe defaults
}

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity); 
        chunk->lines = GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
    }
  
    // store bytecode instruction and source code line # (number)
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
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