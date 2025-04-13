// Include guard 
#ifndef clox_vm_h
#define clox_vm_h

// required for access to ObjFunction
#include "object.h"
// required for hash-table implementation
#include "table.h"
// manages runtime values used in the VM
#include "value.h"

// redefining size for VM's value stack
// to make sure we have plenty of stack slots even in very deep call trees
#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

// for functions (call-stack), represents single ongoing function call
// Each time a function is called, we create one of these structs
typedef struct {
    ObjClosure* closure;                        // pointer to the function being called 
    uint8_t* ip;                                // the caller stores its own instruction pointer, so it acts like a return address (remember it points to current line to be executed)
    Value* slots;                               //  points into the VM’s value stack at the first slot that this function can use
} CallFrame;

// A stack-based VM structure that takes in a chunk to run/execute
// IP = instruction pointer
// CallFrame array replaces the chunk and ip fields
// Now each CallFrame has its own ip and its own pointer to the ObjFunction that it’s executing. From there, we can get to the function’s chunk.
// Every closure maintains an array of upvalues, one for each surrounding local variable that the closure uses
typedef struct {
    CallFrame frames[FRAMES_MAX];               // array of CallFrame structs, treated like a stack like with the value array (call-stack)
    int frameCount;                             // stores current height of the CallFrame stack (# of ongoing function calls)

    Value* stack;                               // VM Dynamic Stack
    int stackCount;                             // Current element count within VM stack
    int stackCapacity;                          // Total capacity of VM stack
    Table globals;                              // hash-table storing global variables
    Table strings;                              // hash-table storing unique strings (for string interning)
    ObjString* initString;                      // for initializer strings
    ObjUpvalue* openUpvalues;                   // A linked list of "open" upvalues — variables that are captured by closures, but still live on the stack
    
    size_t bytesAllocated;                      // tracks total # of bytes currently allocated by VM, used to monitor memory usage and trigger the GC
    size_t nextGC;                              // when bytesAllocated > nextGC, GC is triggered (after GC, is updated to a higher threshold based on the current memory usage)
    Obj* objects;                               // VM stores a pointer to the head of a linked list used to find every allocated object (to avoid memory leakage)
    int grayCount;                              // Tracks the number of objects in the gray stack during garbage collection
    int grayCapacity;                           // total capacity of the gray stack
    Obj** grayStack;                            // A dynamically allocated stack used during garbage collection (GC)
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