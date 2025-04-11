#include <stdlib.h>

#include "compiler.h"
#include "memory.h"
#include "vm.h"


#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

// useful for reallocating memory
// if newsize 0 then we know to free it
// if reallocating more memory then use our garbage collector function
// Might trigger GC if memory use exceeds threshold
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
	vm.bytesAllocated += newSize - oldSize;
	if (newSize > oldSize) {
  #ifdef DEBUG_STRESS_GC
		collectGarbage();
  #endif
		
  		if (vm.bytesAllocated > vm.nextGC) {
			collectGarbage();
  		}
	}
		
	if (newSize == 0) {
    	free(pointer);
    	return NULL;
	}
	// If newsize is not 0, no need to free pointer, just resize it (array will be doubled)
  	void* result = realloc(pointer, newSize);
  		if (result == NULL) exit(1);
  		return result;
}

// checks if it is a valid obj to be marked (#'s, booleans, and nil require no heap allocation)
// avoids double-marking, marks object as reachable or not, and adds to grayStack worklist to be explored later by blackenObject()
// When an object turns gray, in addition to setting the mark field we’ll also add it to the gray entry worklist
void markObject(Obj* object) {
	if (object == NULL) return;
	if (object->isMarked) return;

  #ifdef DEBUG_LOG_GC
	printf("%p mark ", (void*)object);
	printValue(OBJ_VAL(object));
	printf("\n");
  #endif

	object->isMarked = true;
	
	if (vm.grayCapacity < vm.grayCount + 1) {
		vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
		vm.grayStack = (Obj**)realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);

		if (vm.grayStack == NULL) exit(1);
	}
	
	vm.grayStack[vm.grayCount++] = object;
}

// marks value if it is an object on the heap
// ensures that numbers or booleans do not get marked, as they do not need the GC
void markValue(Value value) {
	if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

// Loops through an array of values (like constants in a chunk) and marks them with markValue()
static void markArray(ValueArray* array) {
	for (int i = 0; i < array->count; i++) {
	  	markValue(array->values[i]);
	}
}

// processes a gray object/entry and marks everything it references (its children) to make it a black entry 
// OBJ_CLOSURE → Marks function + all upvalues.
// OBJ_FUNCTION → Marks function name and all constants in its bytecode
// OBJ_UPVALUE → Marks the closed-over value.
// OBJ_STRING/OBJ_NATIVE → No child references; no action needed
static void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
	printf("%p blacken ", (void*)object);
	printValue(OBJ_VAL(object));
	printf("\n");
#endif

	switch (object->type) {
		case OBJ_CLOSURE: {
			ObjClosure* closure = (ObjClosure*)object;
			markObject((Obj*)closure->function);
			for (int i = 0; i < closure->upvalueCount; i++) {
			  	markObject((Obj*)closure->upvalues[i]);
			}
			break;
		}
		case OBJ_FUNCTION: {
			ObjFunction* function = (ObjFunction*)object;
			markObject((Obj*)function->name);
			markArray(&function->chunk.constants);
			break;
		}
		case OBJ_UPVALUE:
			markValue(((ObjUpvalue*)object)->closed);
			break;
		case OBJ_NATIVE:
		case OBJ_STRING:
			break;
	}
}

// function frees a SINGLE object, NOT multiple
// free character array, then the ObjString
// free objFunction if any function is allocated bits of memory, and free its respective chunk
// VM also needs to know how to deallocate a nativefunction obj
// when done with a closure, you free it's memory
// Same thing with an upvalue, for when we do not need the local variable anymore.
static void freeObject(Obj* object) {
  #ifdef DEBUG_LOG_GC
  	printf("%p free type %d\n", (void*)object, object->type);
  #endif

	switch (object->type) {
		case OBJ_CLOSURE: {
			ObjClosure* closure = (ObjClosure*)object;
			FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
			FREE(ObjClosure, object);
			break;
		}
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
		case OBJ_UPVALUE:
		  	FREE(ObjUpvalue, object);
		  	break;
	}
}

// root is an object or value that is reachable and serves as a starting point for determining which objects in memory are still in use
// roots often reference other objects in memory
// like a local variable referencing a string (char array), a closure referencing an array of upvalues, or a global var referencing a function
// think of the roots and its references/children as a graph, which it essentially is.
// Starting from the roots, the garbage collector traverses this graph to mark all reachable objects.
// Any object not reachable from a root is considered garbage and can be freed.
// Most roots are local variables or temporaries sitting right in the VM’s value stack, so we start by walking that
// Some roots are in another separate stack, the CallFrame stack, and some are in the open upvalue list 
static void markRoots() {
	for (Value* slot = vm.stack; slot < vm.stack + vm.stackCount; slot++) {
	  	markValue(*slot);
	}

	for (int i = 0; i < vm.frameCount; i++) {
		markObject((Obj*)vm.frames[i].closure);
	}

	
	for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
	 	markObject((Obj*)upvalue);
    }

	markTable(&vm.globals);
	markCompilerRoots();
}

// Recursively marks all objects referenced by the roots through walking the object graph either from white to gray or gray to black until grayStack empties
// (Until the stack empties, we keep pulling out gray objects, traversing their references, and then marking them black)
static void traceReferences() {
	while (vm.grayCount > 0) {
		Obj* object = vm.grayStack[--vm.grayCount];
		blackenObject(object);
	}
}

// frees all unreachable objects 
// If an object is marked, reset the mark and move on. If an object is not marked, it’s unreachable
// Remove non-marked object from list and free its memory
static void sweep() {
	Obj* previous = NULL;
	Obj* object = vm.objects;
	while (object != NULL) {
		if (object->isMarked) {
			object->isMarked = false;
			previous = object;
			object = object->next;
		} else {
			Obj* unreached = object;
			object = object->next;
			if (previous != NULL) {
				previous->next = object;
			} else {
				vm.objects = object;
			}
	
			freeObject(unreached);
	    }
	}
}

// uses tri-color abstraction, orchestrates a full GC Cycle
// The mark-and-sweep garbage collector for interpreter, recycles memory that can no longer be used (unreachable)
// white entries = objects that are not marked and may possibly be unreachable
// Gray entries = marked, but references haven’t been followed yet
// black entries = marked and fully traced (all children marked)
// 1. Mark roots (makes the root a gray entry, initially everything is a white entry)
// 2. Trace references from roots (mark everything reachable from root, which makes root a black entry)
// 3. Remove white entries from the interned strings table
// 4. Remove everything unreachable (sweep the white entries) 
// 5. Adjust the GC threshold to avoid future GC triggers
void collectGarbage() {
#ifdef DEBUG_LOG_GC
  	printf("-- gc begin\n");
	size_t before = vm.bytesAllocated;
#endif

markRoots();
traceReferences();
tableRemoveWhite(&vm.strings);
sweep();

vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
  	printf("-- gc end\n");
	printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
        before - vm.bytesAllocated, before, vm.bytesAllocated,
        vm.nextGC);
#endif
}

// Frees all objects in the VM's object linked list
// also frees the gray stack used during GC
void freeObjects() {
	Obj* object = vm.objects;
	while (object != NULL) {
	  	Obj* next = object->next;
	  	freeObject(object);
	  	object = next;
	}
	
	free(vm.grayStack);
}