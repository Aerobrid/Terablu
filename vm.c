#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"
#include "memory.h"
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

static InterpretResult run() {
    #define READ_BYTE() (*vm.ip++)                                          // Macro that reads next bytecode instruction using instruction pointer
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])       // Macro that reads constant value from chunk, next line is long const
    #define READ_CONSTANT_LONG() ((vm.chunk->constants.values[(READ_BYTE() << 16) | (READ_BYTE() << 8) | READ_BYTE()]))
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
            case OP_ADD:      BINARY_OP(+); break;
            case OP_SUBTRACT: BINARY_OP(-); break;
            case OP_MULTIPLY: BINARY_OP(*); break;
            case OP_DIVIDE: {
                double b = pop();
                double a = pop();
                if (b == 0) {
                    printf("Runtime Error: Division by zero.\n");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(a / b);
                break;
            }
            case OP_NEGATE: {
                vm.stack[vm.stackCount - 1] = -vm.stack[vm.stackCount - 1];
                break;
            }           
            case OP_MODULUS: {  
                Value b = pop();
                Value a = pop();
            
                if (b == 0) {  
                    printf("Runtime Error: Modulo by zero.\n");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if ((a == (int)a) && (b == (int)b)) {   // Ensure both are integers
                    push((int)a % (int)b);              // Perform integer modulus
                } else {
                    printf("Runtime Error: Modulo requires integer operands.\n");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }                          
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

InterpretResult interpret(const char* source) {
    compile(source);
    return INTERPRET_OK;
}