// Create an include/macro guard (for the same reasons as common.h header file)
#ifndef clox_value_h
#define clox_value_h

#include <string.h>

// include the standard libraries
#include "common.h"

// found in object file, the "base class" for all objects
typedef struct Obj Obj;
// separate struct for string obj
typedef struct ObjString ObjString;

// to maintain support for both the old tagged union implementation of Value and the new NaN-boxed form
// If NaN_BOXING is defined, the VM uses the new form. Otherwise, it reverts to the old style
#ifdef NAN_BOXING

#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN     ((uint64_t)0x7ffc000000000000)

// claim the two lowest bits of our unused mantissa space as a “type tag”
#define TAG_NIL   1 // 01.
#define TAG_FALSE 2 // 10.
#define TAG_TRUE  3 // 11.

typedef uint64_t Value;

#define IS_BOOL(value)      (((value) | 1) == TRUE_VAL)
#define IS_NIL(value)       ((value) == NIL_VAL)
#define IS_NUMBER(value)    (((value) & QNAN) != QNAN)
#define IS_OBJ(value) \
    (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_BOOL(value)      ((value) == TRUE_VAL)
#define AS_NUMBER(value)    valueToNum(value)
// the tilda (~) is bitwise NOT operation
#define AS_OBJ(value) \
    ((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))

#define BOOL_VAL(b)     ((b) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL       ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL        ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define NIL_VAL         ((Value)(uint64_t)(QNAN | TAG_NIL))
#define NUMBER_VAL(num) numToValue(num)
#define OBJ_VAL(obj) \
    (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))

static inline double valueToNum(Value value) {
    double num;
    memcpy(&num, &value, sizeof(Value));
    return num;
}

static inline Value numToValue(double num) {
    Value value;
    memcpy(&value, &num, sizeof(double));
    return value;
}

#else

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

#endif

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