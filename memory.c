#include <stdlib.h>

#include "memory.h"

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