// include guard
#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

// bytecode vm instructions
// added arithmetic
typedef enum {
    OP_CONSTANT,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NEGATE,
    OP_MODULUS,
    OP_CONSTANT_LONG,
    OP_RETURN,
} OpCode;

// Defining the chunk structure
typedef struct {
    int count;              //  Number of bytes currently in the chunk.
    int capacity;           //  Maximum bytes the chunk can hold before resizing.
    uint8_t* code;          //  Pointer to a dynamic array of bytecode instructions (opcodes).
    int* lines;             //  Stores line numbers for debugging.
    ValueArray constants;   //  Each chunk will carry with it a list of the values/constants that appear in the program
} Chunk;

// chunk function declarations
void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);
void writeConstant(Chunk* chunk, Value value, int line);

// end include guard
#endif
