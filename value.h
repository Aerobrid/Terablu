// Create an include/macro guard (for the same reasons as common.h header file)
#ifndef clox_value_h
#define clox_value_h

// include the standard libraries
#include "common.h"

// Integers, decimals, ... (values) will be floating-points
typedef double Value;

typedef struct {
    int capacity;  // Max number of values the array can hold
    int count;     // Current number of values stored
    Value* values; // Pointer to an array of stored values
} ValueArray;

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);


// End the include/macro guard
#endif