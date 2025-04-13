// include guard
#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

// extracts the object type tag from a given Value
#define OBJ_TYPE(value)        (AS_OBJ(value)->type)

// checks if given Value is a function by calling isObjType()
#define IS_FUNCTION(value)     isObjType(value, OBJ_FUNCTION)
// to check if a value is an instance
#define IS_INSTANCE(value)     isObjType(value, OBJ_INSTANCE)
// to check if a value is a native function
#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)
// to check if a value is a bound method
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
// testing obj type to be a class
#define IS_CLASS(value)        isObjType(value, OBJ_CLASS)
// to check if value is a closure
#define IS_CLOSURE(value)      isObjType(value, OBJ_CLOSURE)
// to ensure that the Obj* pointer you have does point to the obj field of an actual ObjString
#define IS_STRING(value)       isObjType(value, OBJ_STRING)

// casts value to an ObjBoundMethod pointer
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
// casts value to an ObjClass pointer
#define AS_CLASS(value)        ((ObjClass*)AS_OBJ(value))
// casts the value to ObjClosure pointer
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
// casts the value to ObjFunction pointer
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
// casts the value to ObjInstance pointer
#define AS_INSTANCE(value)     ((ObjInstance*)AS_OBJ(value))

#define AS_NATIVE(value) \
    (((ObjNative*)AS_OBJ(value))->function)
// take a Value that is expected to contain a pointer to a valid ObjString on the heap
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)

// different type tags for each obj
typedef enum {
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE
} ObjType;

struct Obj {
    ObjType type;               // the type of obj it is
    bool isMarked;              // has it been marked by the garbage collector (GC) or not
    struct Obj* next;           // next pointer (intrusive list) for a Linked list to store every obj created/allocated onto heap 
};

typedef struct {
    Obj obj;                    // functions are 1st-class, so they need to be an obj
    int arity;                  // stores the # of parameters function is expecting
    int upvalueCount;
    Chunk chunk;                // Each function has its own chunk to be processed
    ObjString* name;            // function name
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value* args);

// native functions is a type of obj, needs its own struct
typedef struct {
    Obj obj;                    // obj header (base struct for all things strings, functions, classes, etc.)
    NativeFn function;          // takes the argument count and a pointer to the first argument on the stack. It accesses the arguments through that pointer. Once it’s done, it returns the result value.
} ObjNative;

struct ObjString {
    Obj obj;                    // string is a type of obj
    int length;                 // length of string
    char* chars;                // points to a dynamically allocated array of characters making up the string (is null-terminated as well '\0')
    uint32_t hash;              // each ObjString stores the hash code for its string
};

typedef struct ObjUpvalue {
    Obj obj;                     // Every object starts with an Obj header
    Value* location;             // Points to the variable in the VM's stack
    Value closed;                // If the variable is closed, value is copied here
    struct ObjUpvalue* next;     // Linked list pointer (used by VM to track open upvalues)
} ObjUpvalue;

typedef struct {
    Obj obj;                    // closure will contain an object tag
    ObjFunction* function;      // the function being closed over
    ObjUpvalue** upvalues;      // array of pointers to upvalue objects
    int upvalueCount;           // how many upvalues it has
} ObjClosure;


typedef struct {
    Obj obj;                    // obj header
    ObjString* name;            // to store the class's name (for things like stack traces)
    Value initializer;          // cache the initializer directly in the ObjClass to avoid the hash table lookup (optimization)
    Table methods;              // each class stores a hash-table of methods (keys: method names, values: an ObjClosure for the body of the method)
} ObjClass;

typedef struct {
    Obj obj;                    // object header
    ObjClass* klass;            // pointer to the class it is an instance of
    Table fields;               // Each instance stores its fields in a hash-table
} ObjInstance;

// a bound method is a method that is tied to a specific instance of a class
typedef struct {
    Obj obj;                    // obj header
    Value receiver;             // the instance the method is bound to
    ObjClosure* method;         // The method (function) itself, stored as a closure
} ObjBoundMethod;
  
ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method);
ObjClass* newClass(ObjString* name);
ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjInstance* newInstance(ObjClass* klass);
ObjNative* newNative(NativeFn function);
ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
ObjUpvalue* newUpvalue(Value* slot);
void printObject(Value value);

// Q: Why not just put the body of this function right in the macro? 
// A: Vecause body uses "value" twice, which can make expression get evaluated multiple times 
static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}
  
// end include guard
#endif