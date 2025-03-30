#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

int main(int argc, const char* argv[]) {
    initVM();                                           // Sets up vm and prepares stack so it is ready to execute bytecode

    Chunk chunk;                                        // Creates chunk obj which represents a collection of bytecode instructions and constants
    initChunk(&chunk);                                  // Initializes the chunk, setting up an empty array to store bytecode and constants

    int constant = addConstant(&chunk, 1.2);            // Adds 1.2 to chunks constants array, returns index of constant in array
    writeChunk(&chunk, OP_CONSTANT, 123);               // writes the opcode for pushing a constant onto the stack
    writeChunk(&chunk, constant, 123);                  // writes the index of the constant added earlier (1.2)

    constant = addConstant(&chunk, 5.8);
    writeChunk(&chunk, OP_CONSTANT, 123);
    writeChunk(&chunk, constant, 123);

    writeChunk(&chunk, OP_ADD, 123);

    constant = addConstant(&chunk, 3);
    writeChunk(&chunk, OP_CONSTANT, 123);
    writeChunk(&chunk, constant, 123);

    writeChunk(&chunk, OP_MODULUS, 123);
    writeChunk(&chunk, OP_NEGATE, 123);
  
    writeChunk(&chunk, OP_RETURN, 123);

    disassembleChunk(&chunk, "test chunk");
    interpret(&chunk);
    freeVM();
    freeChunk(&chunk);
    return 0;
}