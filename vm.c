#include "common.h"
#include "debug.h"
#include "vm.h"
#include <stdio.h>
#include <math.h>

// global instance makes it easier to manage, not necessarily the best implementation when compared to a VM pointer
VM vm; 

// resets stackTop, is a helper function
// points to beginning of array
static void resetStack() {
    vm.stackTop = vm.stack;
}

void initVM() {
    resetStack();
}

void freeVM() {
}

// adds value to where top of stack is, then moves top to where the next value to be pushed will go
void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

// move top to last used slot in stack (by decrementing, remember that current top is empty), then return the value
Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

static InterpretResult run() {
    #define READ_BYTE() (*vm.ip++)                                          // Macro that reads next bytecode instruction using instruction pointer
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])       // Macro that reads constant value from chunk
    // Macro that Pops 2 values from stack, applies the operation (op) to the 2 values, then pushes the result back to stack
    #define BINARY_OP(op) \
    do { \
        double b = pop(); \
        double a = pop(); \
        push(a op b); \
    } while (false)
    
    for (;;) {

    // For VM disassembly and stack tracing (looks at stack internally for better debugs)
    #ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
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
            case OP_ADD:      BINARY_OP(+); break;
            case OP_SUBTRACT: BINARY_OP(-); break;
            case OP_MULTIPLY: BINARY_OP(*); break;
            case OP_DIVIDE:   BINARY_OP(/); break;
            case OP_MODULUS: {  
                Value b = pop();
                Value a = pop();
            
                if (b == 0) {  
                    printf("Runtime Error: Modulo by zero.\n");
                    return INTERPRET_RUNTIME_ERROR;
                }
            
                if ((a == (int)a) && (b == (int)b)) {  // Ensure both are integers
                    push((int)a % (int)b);  // Perform integer modulus
                } else {
                    printf("Runtime Error: Modulo requires integer operands.\n");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }                 
            case OP_NEGATE:   push(-pop()); break;
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

InterpretResult interpret(Chunk* chunk) {
    vm.chunk = chunk;
    vm.ip = vm.chunk->code;
    return run();
}