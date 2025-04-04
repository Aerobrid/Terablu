// Create an include/macro guard (for the same reasons as common.h header file)
#ifndef clox_value_h
#define clox_value_h

// include the standard libraries
#include "common.h"

// found in object file, the "base class" for all objects
typedef struct Obj Obj;
// separate struct for string obj
typedef struct ObjString ObjString;

typedef enum {
    VAL_BOOL,               // boolean datatype
    VAL_NIL,                // Clox's NULL datatype
    VAL_NUMBER,             // Could either be an int or decimal(double in C), maybe separating the 2 in future 
    VAL_OBJ                 // Refers to all heap-allocated types (strings, instances, functions, etc.)
} ValueType;

// Struct wastes memory since a value can’t simultaneously be both a number and a boolean, so optimize by using union instead
// The size of a union is the size of its largest field
/* IMPORTANT DETAIL FROM BOOK: 
    Most architectures prefer values be aligned to their size.
    Since the union field contains an eight-byte double, the compiler adds four bytes of padding after the type field to keep that double
    on the nearest eight-byte boundary. This means that our value struct is CURRENTLY 16 bytes (will add strings, arrays, etc. later )
*/
typedef struct {
    ValueType type;             // Tag to indicate the type of value (4-bytes)
    union {
        bool boolean;           // boolean value (1-byte)
        double number;          // Numeric value (8-bytes, could be int or double)
        Obj* obj;
    } as;                       // union to store the actual value
} Value;

// takes a C value of the appropriate type and produces a Value that has the correct type tag and contains the underlying value
#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

// given a Value of the right type, these macros unwrap it and return the corresponding raw C value
#define AS_OBJ(value)     ((value).as.obj)
#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)

// return true if the Value has that type
#define OBJ_VAL(object)   ((Value){VAL_OBJ, {.obj = (Obj*)object}})
#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})

typedef struct {
    int capacity;  // Max number of values the array can hold
    int count;     // Current number of values stored
    Value* values; // Pointer to an array of stored values
} ValueArray;

bool valuesEqual(Value a, Value b);
// Declaring functions for managing the ValueArray
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);


// End the include/macro guard
#endif