#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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

// native clock function using time header returning time started since program started (in seconds)
static Value clockNative(int argCount, Value* args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

// native function to delete a field from an instance
static Value deleteFieldNative(int argCount, Value* args) {
    if (argCount != 2) return NIL_VAL;
    if (!IS_INSTANCE(args[0])) return NIL_VAL;
    if (!IS_STRING(args[1])) return NIL_VAL;
  
    ObjInstance* instance = AS_INSTANCE(args[0]);
    tableDelete(&instance->fields, AS_STRING(args[1]));
    return NIL_VAL;
}

// to reset/initialize vm's value stack
static void resetStack() {
    vm.stackCount = 0;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
    if (vm.stack == NULL) { 
        vm.stackCapacity = 8; // Initial capacity of VM Stack
        vm.stack = (Value*)malloc(vm.stackCapacity * sizeof(Value));
    }
}

// prints out stack trace and where error occured if any, useful in debugging
static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
  
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }    
    resetStack();
}

// gives a native function a name, so it can be used in CLOX language with other user-defined functions
static void defineNative(const char* name, NativeFn function) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

// initialized VM
void initVM() {
    vm.stack = NULL;                // Nothing in VM stack since VM has just been created
    vm.stackCapacity = 0;           
    resetStack();                   // stack initially empty
    vm.objects = NULL;              // Nothing in LL since VM has just been created
    vm.bytesAllocated = 0;          // when VM starts up, no memory has been allocated
    vm.nextGC = 1024 * 1024;        // initial threshold is arbitrary, goal is to not trigger the first few GCs too quickly but also to not wait too long
    
    vm.grayCount = 0;               // gray stack is initially empty
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    initTable(&vm.globals);                     // global var table initially empty
    initTable(&vm.strings);                     // string table initially empty
    vm.initString = NULL;
    vm.initString = copyString("init", 4);      // create and intern string when VM boots up

    defineNative("clock", clockNative);                                         // our native functions
    defineNative("deleteField", deleteFieldNative);

}

// frees memory from VM processes
void freeVM() {
    freeTable(&vm.globals);         // free global var hashtable from heap
    freeTable(&vm.strings);         // free string hashtable from heap
    vm.initString = NULL;           // prevent dangling pointers 
    freeObjects();                  // to free every object from user program
    free(vm.stack);                 // free the value stack
    vm.stack = NULL;                // Prevents dangling pointers
    vm.stackCapacity = 0;           // set to 0
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

// sets up new CallFrame and stack slots for a function
static bool call(ObjClosure* closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stack + vm.stackCount - argCount - 1;
    return true;
}

// Determines whether the given Value (callee) is callable and executes it if possible.
// Handles bound methods, classes (constructors), closures (user-defined functions), and native functions.
// If the callee is not callable, reports a runtime error.
static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm.stack[vm.stackCount - argCount - 1] = bound->receiver;
                return call(bound->method, argCount);
            }
            case OBJ_CLASS: {
                ObjClass* klass = AS_CLASS(callee);
                vm.stack[vm.stackCount - argCount - 1] = OBJ_VAL(newInstance(klass));
                if (!IS_NIL(klass->initializer)) {                      
                    return call(AS_CLOSURE(klass->initializer), argCount);
                } else if (argCount != 0) {
                    runtimeError("Expected 0 arguments but got %d.", argCount);
                    return false;
                }
                return true;
            }
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, vm.stack + vm.stackCount - argCount);
                vm.stackCount -= argCount + 1;
                push(result);
                return true;
            }
            default:
                break; // Non-callable object type.
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

// look up the method by name in the class’s method table, if not found, report runtime error and exit
// Otherwise, take the method’s closure and push a call to it onto the CallFrame stack
static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }
    return call(AS_CLOSURE(method), argCount);
}

// grabs the reciever (method) off the stack
// arguments passed to the method are above it on the stack, so we peek that many slots down
// cast the object to an instance and invoke the method on it
static bool invoke(ObjString* name, int argCount) {
    Value receiver = peek(argCount);
    
    if (!IS_INSTANCE(receiver)) {
        runtimeError("Only instances have methods.");
        return false;
    }

    ObjInstance* instance = AS_INSTANCE(receiver);

    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        vm.stack[vm.stackCount - argCount - 1] = value;
        return callValue(value, argCount);
    }

    return invokeFromClass(instance->klass, name, argCount);
}

// look for a method with the given name in the class’s method table
// if not found report runtime error, otherwise, take the method and wrap it in a new ObjBoundMethod
// grab the receiver at top of stack, then pop instance (receiver) and replace top of stack with the bound method
static bool bindMethod(ObjClass* klass, ObjString* name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }
  
    ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
}

// Captures a local variable from the stack and wraps it in an ObjUpvalue
// allows a nested function to reference a variable from its enclosing function
static ObjUpvalue* captureUpvalue(Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }
  
    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }
  
    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }
  
    return createdUpvalue;
}

// If function ends and its local variables are about to be popped off the stack,
// any open upvalues pointing to those locals must be closed (saved on heap)
static void closeUpvalues(Value* last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

// gets a method and adds it to its respective class's method table
// When a method is defined, if it's the initializer, then we also store it in that field
static void defineMethod(ObjString* name) {
    Value method = peek(0);
    ObjClass* klass = AS_CLASS(peek(1));
    tableSet(&klass->methods, name, method);
    if (name == vm.initString) klass->initializer = method;
    pop();
}

// determine whether given value is falsey
static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

// to "add" 2 strings together
// calculates length of result string based on 2 strings given -> allocate char array for result -> then copy the 2 strings in
static void concatenate() {
    ObjString* b = AS_STRING(peek(0));
    ObjString* a = AS_STRING(peek(1));
  
    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';
  
    ObjString* result = takeString(chars, length);
    pop();
    pop();
    push(OBJ_VAL(result));
}

// bytecode interpreter loop
// endlessly reads next instruction, performs appropriate instruction, and updates stack/ip accordingly
// macros simplify the process
static InterpretResult run() {
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

    // reads single byte from bytecode and advances instruction pointer
    #define READ_BYTE() (*frame->ip++)
    
    #define READ_SHORT() \
        (frame->ip += 2, \
        (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
    
    #define READ_CONSTANT() \
        (frame->closure->function->chunk.constants.values[READ_BYTE()])
    
    #define READ_CONSTANT_LONG() \
        (frame->closure->function->chunk.constants.values[ \
        (READ_BYTE() << 16) | (READ_BYTE() << 8) | READ_BYTE()])

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
        disassembleInstruction(&frame->closure->function->chunk, (int)(frame->ip - frame->closure->function->chunk.code));
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
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
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
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }
            case OP_GET_PROPERTY: {
                if (!IS_INSTANCE(peek(0))) {
                    runtimeError("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }
          
                ObjInstance* instance = AS_INSTANCE(peek(0));
                ObjString* name = READ_STRING();
        
                Value value;
                if (tableGet(&instance->fields, name, &value)) {
                    pop(); // Instance.
                    push(value);
                    break;
                }

                if (!bindMethod(instance->klass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE(peek(1))) {
                    runtimeError("Only instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }
          
                ObjInstance* instance = AS_INSTANCE(peek(1));
                tableSet(&instance->fields, READ_STRING(), peek(0));
                Value value = pop();
                pop();
                push(value);
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
            case OP_DUP: push(peek(0)); break;
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
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }   
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            } 
            case OP_CALL: {
                int argCount = READ_BYTE();
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            } 
            case OP_INVOKE: {
                ObjString* method = READ_STRING();
                int argCount = READ_BYTE();
                if (!invoke(method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                push(OBJ_VAL(closure));
                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            } 
            case OP_CLOSE_UPVALUE:
                closeUpvalues(vm.stack + vm.stackCount - 1);
                pop();
                break;                          
            case OP_RETURN: {
                Value result = pop();
                closeUpvalues(frame->slots);
                vm.frameCount--;
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }
        
                vm.stackCount = (int)(frame->slots - vm.stack);
                push(result);
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_CLASS:
                push(OBJ_VAL(newClass(READ_STRING())));
                break; 
            case OP_METHOD:
                defineMethod(READ_STRING());
                break;
        }
    }
    
    #undef READ_BYTE
    #undef READ_SHORT
    #undef READ_CONSTANT
    #undef READ_CONSTANT_LONG
    #undef READ_STRING
    #undef BINARY_OP
}

// create a new empty chunk and pass it over to the compiler,
// which will fill it up with bytecode IF program does not have compile errors
// then send completed chunk over to the VM to be executed, then free it
InterpretResult interpret(const char* source) {
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;
  
    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);
  
    return run();
}