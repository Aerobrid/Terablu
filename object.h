// include guard
#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"

// extracts the object type tag from a given Value
#define OBJ_TYPE(value)        (AS_OBJ(value)->type)

// to ensure that the Obj* pointer you have does point to the obj field of an actual ObjString
#define IS_STRING(value)       isObjType(value, OBJ_STRING)

// take a Value that is expected to contain a pointer to a valid ObjString on the heap
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)

// different type tags for each obj
typedef enum {
    OBJ_STRING,
} ObjType;

struct Obj {
    ObjType type;
    struct Obj* next;           // next pointer (intrusive list) for a Linked list to store every obj created/allocated onto heap 
};

struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;              // each ObjString stores the hash code for its string
};

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