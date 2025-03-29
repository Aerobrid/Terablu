#include <stdio.h>

#include "memory.h"
#include "value.h"

// initially array is empty
void initValueArray(ValueArray* array) {
  	array->values = NULL;
  	array->capacity = 0;
  	array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    // if array is full, call upon memory allocation macros
	// effectively resize array
    if (array->capacity < array->count + 1) {
      	int oldCapacity = array->capacity;
      	array->capacity = GROW_CAPACITY(oldCapacity);
      	array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
    }
	
	// store the new value, increment count
    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array) {
    FREE_ARRAY(Value, array->values, array->capacity);
	// resets array
    initValueArray(array);
}


void printValue(Value value) {
    printf("%g", value);                        // We use a %g placeholder here for formatting and printing out the floating-point #'s appropriately 
}