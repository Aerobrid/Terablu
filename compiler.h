// Include guard
#ifndef clox_compiler_h
#define clox_compiler_h 

#include "object.h"
#include "vm.h"

ObjFunction* compile(const char* source);
void markCompilerRoots();

// End include guard
#endif