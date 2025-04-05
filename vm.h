// Include guard 
#ifndef clox_vm_h
#define clox_vm_h

// required for handling bytecode storage
#include "chunk.h"
// required for hash-table implementation
#include "table.h"
// manages runtime values used in the VM
#include "value.h"

// Max size for VM's stack
#define STACK_MAX 256

// A stack-based VM structure that takes in a chunk to run/execute
// IP = instruction pointer ()
// it’s faster to dereference a pointer than look up an element in the bytecode array by index.
typedef struct {
    Chunk* chunk;                               // Stores a pointer to the chunk of bytecode that the VM will execute.
    uint8_t* ip;                                // Instruction pointer that tracks the position in the bytecode.
    Value* stack;                               // VM Dynamic Stack
    int stackCount;                             // Current element count within VM stack
    int stackCapacity;                          // Total capacity of VM stack
    Table globals;
    Table strings;
    Obj* objects;                               // VM stores a pointer to the head of a linked list used to find every allocated object (to avoid memory leakage)
} VM;

// The VM runs the chunk and then responds with a value from this enum:
typedef enum {
    INTERPRET_OK,                               // Execution successful
    INTERPRET_COMPILE_ERROR,                    // syntax/parsing error
    INTERPRET_RUNTIME_ERROR                     // runtime error (e.g., stack overflow, invalid operation)
} InterpretResult;

extern VM vm;                                   // exposes vm variable to other files as a global var

// Declare VM functions
void initVM();
void freeVM();
InterpretResult interpret(const char* source);      // Pass in a string of source code now
void push(Value value);
Value pop();

// end the Include guard
#endif