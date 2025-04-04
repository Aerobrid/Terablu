// include guard
#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"
#include "object.h"

// macro functions using reallocate() (kindof a memory allocator) 
/* Important detail: 
If all allocation and freeing goes through reallocate(), 
it’s easy to keep a running count of the number of bytes of allocated memory.  
*/
#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

// tiny wrapper around reallocate() that “resizes” an allocation down to zero bytes
#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount) \
reallocate(pointer, sizeof(type) * (oldCount), 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);
void freeObjects();

#endif   // end include guard