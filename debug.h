// include guard
#ifndef clox_debug_h
#define clox_debug_h

#include "chunk.h"

// disassembler function declarations
void disassembleChunk(Chunk* chunk, const char* name);
int disassembleInstruction(Chunk* chunk, int offset);

// end include guard
#endif