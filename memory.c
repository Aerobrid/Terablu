#include <stdlib.h>

#include "memory.h"
#include "vm.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
	if (newSize == 0) {
    	free(pointer);
    	return NULL;
	}
	// If newsize is not 0, no need to free pointer, just resize it (array will be doubled)
  	void* result = realloc(pointer, newSize);
  		if (result == NULL) exit(1);
  		return result;
}

// free character array, then the ObjString
// free objFunction if any function is allocated bits of memory, and free its respective chunk
static void freeObject(Obj* object) {
	switch (object->type) {
		case OBJ_FUNCTION: {
			ObjFunction* function = (ObjFunction*)object;
			freeChunk(&function->chunk);
			FREE(ObjFunction, object);
			break;
		}
		case OBJ_NATIVE:
			FREE(ObjNative, object);
			break;
	  	case OBJ_STRING: {
			ObjString* string = (ObjString*)object;
			FREE_ARRAY(char, string->chars, string->length + 1);
			FREE(ObjString, object);
			break;
	  	}
	}
}

void freeObjects() {
	Obj* object = vm.objects;
	while (object != NULL) {
	  	Obj* next = object->next;
	  	freeObject(object);
	  	object = next;
	}
}