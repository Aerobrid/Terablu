// Include guard 
#ifndef clox_vm_h
#define clox_vm_h

// required for handling bytecode storage
#include "chunk.h"
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
    Value stack[STACK_MAX];                     // A fixed-size stack for storing temporary values.
    Value* stackTop;                            // Points to the top of the stack, simplifying push/pop operations.
} VM;

// The VM runs the chunk and then responds with a value from this enum:
typedef enum {
    INTERPRET_OK,                               // Execution successful
    INTERPRET_COMPILE_ERROR,                    // syntax/parsing error
    INTERPRET_RUNTIME_ERROR                     // runtime error (e.g., stack overflow, invalid operation)
} InterpretResult;
  

// Declare VM functions
void initVM();
void freeVM();
InterpretResult interpret(Chunk* chunk);       // Runs the bytecode stored in the chunk
void push(Value value);
Value pop();

// end the Include guard
#endif