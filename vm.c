#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"
#include "memory.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// global instance makes it easier to manage, not necessarily the best implementation when compared to a VM pointer
VM vm; 

static void resetStack() {
    vm.stackCount = 0;
    if (vm.stack == NULL) { 
        vm.stackCapacity = 8; // Initial capacity of VM Stack
        vm.stack = (Value*)malloc(vm.stackCapacity * sizeof(Value));
    }
}

static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
  
    size_t instruction = vm.ip - vm.chunk->code - 1;
    int line = getLine(vm.chunk, instruction);
    fprintf(stderr, "[line %d] in script\n", line);
    resetStack();
}

void initVM() {
    vm.stack = NULL;
    vm.stackCapacity = 0;
    resetStack();
}

void freeVM() {
    free(vm.stack);
    vm.stack = NULL;                // Prevents dangling pointers
    vm.stackCapacity = 0;
    resetStack();
}

// adds value to where count is, then moves count to where the next value to be pushed will go
void push(Value value) {
    if (vm.stackCapacity < vm.stackCount + 1) {
      int oldCapacity = vm.stackCapacity;
      vm.stackCapacity = GROW_CAPACITY(oldCapacity);
      vm.stack = GROW_ARRAY(Value, vm.stack, oldCapacity, vm.stackCapacity);
    }
  
    vm.stack[vm.stackCount] = value;
    vm.stackCount++;
}
// move count to last used slot in stack, then return the value
Value pop() {
    vm.stackCount--;
    return vm.stack[vm.stackCount];
}

static Value peek(int distance) {
    return vm.stack[vm.stackCount - 1 - distance];
}

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static InterpretResult run() {
    #define READ_BYTE() (*vm.ip++)                                          // Macro that reads next bytecode instruction using instruction pointer
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])       // Macro that reads constant value from chunk, next line is long const
    #define READ_CONSTANT_LONG() ((vm.chunk->constants.values[(READ_BYTE() << 16) | (READ_BYTE() << 8) | READ_BYTE()]))
    // Macro that Pops 2 values from stack, applies the operation (op) to the 2 values, then pushes the result back to stack
    #define BINARY_OP(valueType, op) \
    do { \
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
            runtimeError("Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        double b = AS_NUMBER(pop()); \
        double a = AS_NUMBER(pop()); \
        push(valueType(a op b)); \
    } while (false)
    
    for (;;) {

    // For VM disassembly and stack tracing (looks at stack internally for better debugs)
    #ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (Value *slot = vm.stack; slot < vm.stack + vm.stackCount; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(vm.chunk,(int)(vm.ip - vm.chunk->code));
    #endif
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_CONSTANT_LONG: { 
                Value constant = READ_CONSTANT_LONG();
                push(constant);
                break;
            }
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD:      BINARY_OP(NUMBER_VAL, +); break;
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;                                    
            case OP_RETURN: {
                printValue(pop());
                printf("\n");
                return INTERPRET_OK;
            }
        }
    }
    
    #undef READ_BYTE
    #undef READ_CONSTANT
    #undef BINARY_OP
}

// create a new empty chunk and pass it over to the compiler,
// which will fill it up with bytecode IF program does not have compile errors
// then send completed chunk over to the VM to be executed, then free it
InterpretResult interpret(const char* source) {
    Chunk chunk;
    initChunk(&chunk);
  
    if (!compile(source, &chunk)) {
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }
  
    vm.chunk = &chunk;
    vm.ip = vm.chunk->code;
  
    InterpretResult result = run();
  
    freeChunk(&chunk);
    return result;
}