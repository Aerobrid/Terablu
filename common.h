// Include/macro guard (prevents header file from being included more than once anywhere, which will give error)
// Exp: if the preprocessor has the name "CLOX_COMMON_H" defined then it skips entire file and goes to #endif
#ifndef clox_common_h  // If "CLOX_COMMON_H" (it can be another name) is not defined...
#define clox_common_h  // Define CLOX_COMMON_H


// Standard library headers the interpreter will use
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// #pragma once (might be better to use instead of #endif but might not work with all compilers)
#endif                // Ends the include/macro guard