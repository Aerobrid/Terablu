#include <stdio.h>
#include <string.h>

#include "object.h"
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

// now can handle nil, booleans, strings, and # values
// If the value is a heap-allocated object, it defers to a helper function over in the “object” module
void printValue(Value value) {
	switch (value.type) {
		case VAL_BOOL:
		  	printf(AS_BOOL(value) ? "true" : "false");
		  	break;
		case VAL_NIL: printf("nil"); break;
		case VAL_NUMBER: printf("%g", AS_NUMBER(value)); break;
		case VAL_OBJ: printObject(value); break;
	}
}

// to make sure two values (according to type with switch) are equal
bool valuesEqual(Value a, Value b) {
	if (a.type != b.type) return false;
	switch (a.type) {
	  	case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
	  	case VAL_NIL:    return true;
	  	case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
		case VAL_OBJ: {
			ObjString* aString = AS_STRING(a);
			ObjString* bString = AS_STRING(b);
			return aString->length == bString->length &&
				memcmp(aString->chars, bString->chars, aString->length) == 0;
		}
	  	default:         return false; // Unreachable.
	}
}