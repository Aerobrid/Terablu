#include <stdio.h> 

#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);

    // ***different instructions take up different numbers of bytes***
    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t constant = chunk->code[offset + 1]; // 1 byte for the constant index
    printf("%-16s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    // instruction = 1 byte, constant index = 1 byte (so increment offset by 2)
    return offset + 2;
}

static int constantLongInstruction(const char* name, Chunk* chunk, int offset) {
    // 3 bytes for the constant index
    uint8_t byte1 = chunk->code[offset + 1];
    uint8_t byte2 = chunk->code[offset + 2];
    uint8_t byte3 = chunk->code[offset + 3];
    
    // Reconstruct the full constant index (24-bit)
    int constant = (byte3 << 16) | (byte2 << 8) | byte1;

    printf("%-16s %6d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");

    // Move offset by 4 because we read 1 byte for the opcode and 3 bytes for the index
    return offset + 4;
}

static int simpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    // simple instructions only take up a byte (so return offset + 1)
    return offset + 1;          
}

int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    // for disassembly support and easier debugs; Ex: Instruction executed -> return information regarding it
    switch (instruction) {
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_ADD:
            return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT:
            return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:
            return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:
            return simpleInstruction("OP_DIVIDE", offset);
        case OP_NEGATE:
            return simpleInstruction("OP_NEGATE", offset);
        case OP_MODULUS:
            return simpleInstruction("OP_MODULUS", offset);
        case OP_CONSTANT_LONG:
            return constantLongInstruction("OP_CONSTANT_LONG", chunk, offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}
