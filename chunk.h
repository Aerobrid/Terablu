// include guard
#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

// bytecode vm instructions
// added arithmetic
typedef enum {
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_GET_SUPER,
    OP_DUP,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_NEGATE,
    OP_PRINT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_CALL,
    OP_INVOKE,
    OP_SUPER_INVOKE,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,
    OP_MODULUS,
    OP_CONSTANT_LONG,
    OP_RETURN,
    OP_CONDITIONAL,
    OP_CLASS,
    OP_INHERIT,
    OP_METHOD
} OpCode;

//  maps bytecode instructions back to source code lines
typedef struct {
    int offset;             // index in bytecode array where source code line begins
    int line;               // line number in integer form
} LineStart;

// Defining the chunk structure
typedef struct {
    int count;              //  Number of bytes currently in the chunk.
    int capacity;           //  Maximum bytes the chunk can hold before resizing.
    uint8_t* code;          //  Pointer to a dynamic array of bytecode instructions (opcodes).
    int lineCount;          // how many linestart entries are currently stored
    int lineCapacity;       // allocated size (# of LineStart entries) for the dynamic array lines
    LineStart* lines;       //  pointer to a dynamic array of LineStart structs
    ValueArray constants;   //  Each chunk will carry with it a list of the values/constants that appear in the program
} Chunk;

// chunk function declarations
void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);
void writeConstant(Chunk* chunk, Value value, int line);
int getLine(Chunk* chunk, int instruction);

// end include guard
#endif
