#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"
#include "memory.h"

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

// initialized VM
void initVM() {
    vm.stack = NULL;                // Nothing in VM stack since VM has just been created
    vm.stackCapacity = 0;           
    resetStack();                   // stack initially empty
    vm.objects = NULL;              // Nothing in LL since VM has just been created

    initTable(&vm.globals);
    initTable(&vm.strings);         // string table initially empty
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);         
    freeObjects();                  // to free every object from user program
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

// to "add" 2 strings together
// calculates length of result string based on 2 strings given -> allocate char array for result -> then copy the 2 strings in
static void concatenate() {
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());
  
    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';
  
    ObjString* result = takeString(chars, length);
    push(OBJ_VAL(result));
}

static InterpretResult run() {
    #define READ_BYTE() (*vm.ip++)                                          // Macro that reads next bytecode instruction using instruction pointer
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])       // Macro that reads constant value from chunk, next line is long const
    #define READ_CONSTANT_LONG() ((vm.chunk->constants.values[(READ_BYTE() << 16) | (READ_BYTE() << 8) | READ_BYTE()]))
    #define READ_STRING() AS_STRING(READ_CONSTANT())
    // Macro that Pops 2 values from stack, applies the operation (op) to the 2 values, then pushes the result back to stack
    // you can pass macros as parameters to macros
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
            case OP_POP: pop(); break;
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(vm.stack[slot]); 
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                vm.stack[slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name); 
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                  double b = AS_NUMBER(pop());
                  double a = AS_NUMBER(pop());
                  push(NUMBER_VAL(a + b));
                } else {
                  runtimeError("Operands must be two numbers or two strings.");
                  return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
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
            case OP_PRINT: {
                printValue(pop());
                printf("\n");
                break;
            }                                  
            case OP_RETURN: {
                // Exit interpreter.
                return INTERPRET_OK;
            }
        }
    }
    
    #undef READ_BYTE
    #undef READ_CONSTANT
    #undef READ_STRING
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