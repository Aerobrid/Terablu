// include guard
#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"

// extracts the object type tag from a given Value
#define OBJ_TYPE(value)        (AS_OBJ(value)->type)

// checks if given Value is a function by calling isObjType()
#define IS_FUNCTION(value)     isObjType(value, OBJ_FUNCTION)

#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)

// to ensure that the Obj* pointer you have does point to the obj field of an actual ObjString
#define IS_STRING(value)       isObjType(value, OBJ_STRING)

// casts the value to ObjFunction pointer
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))

#define AS_NATIVE(value) \
    (((ObjNative*)AS_OBJ(value))->function)
// take a Value that is expected to contain a pointer to a valid ObjString on the heap
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)

// different type tags for each obj
typedef enum {
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_STRING,
} ObjType;

struct Obj {
    ObjType type;
    struct Obj* next;           // next pointer (intrusive list) for a Linked list to store every obj created/allocated onto heap 
};

typedef struct {
    Obj obj;                    // functions are 1st-class, so they need to be an obj
    int arity;                  // stores the # of parameters function is expecting
    Chunk chunk;                // Each function has its own chunk to be processed
    ObjString* name;            // function name
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
    Obj obj;
    NativeFn function;
} ObjNative;

struct ObjString {
    Obj obj;                    // string is a type of obj
    int length;                 // length of string
    char* chars;                // points to a dynamically allocated array of characters making up the string (is null-terminated as well '\0')
    uint32_t hash;              // each ObjString stores the hash code for its string
};

ObjFunction* newFunction();
ObjNative* newNative(NativeFn function);
ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
void printObject(Value value);

// Q: Why not just put the body of this function right in the macro? 
// A: Vecause body uses "value" twice, which can make expression get evaluated multiple times 
static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}
  
// end include guard
#endif