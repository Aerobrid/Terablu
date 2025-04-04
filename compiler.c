#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

// debugging support
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

// This compiler is single-pass, it ALSO includes the parser to read user's source code (makes it simpler but does not work for every language)
// (combines parsing and bytecode generation into one step)
typedef struct {
	Token current;						// current token being processed
	Token previous;						// previous token being processed
	bool hadError;						// Indicates if an error occurred during parsing
	bool panicMode;						// this prevents further error messages from being generated after an error is encountered
} Parser;

// defines levels (lowest to highest from top to bottom) of operator precedence
// IMPORTANT for determining the order of operations when parsing expressions
typedef enum {
	PREC_NONE,
	PREC_ASSIGNMENT,  // =
	PREC_CONDITIONAL, // ?:
	PREC_OR,          // or
	PREC_AND,         // and
	PREC_EQUALITY,    // == !=
	PREC_COMPARISON,  // < > <= >=
	PREC_TERM,        // + - %
	PREC_FACTOR,      // * /
	PREC_UNARY,       // ! -
	PREC_CALL,        // . ()
	PREC_PRIMARY
} Precedence;

// Function pointer type 
typedef void (*ParseFn)();

// Rules for parsing each tokenType
typedef struct {
	ParseFn prefix;						// Pointer to a function for prefix parsing (handles prefix operators like - FOR negation)
	ParseFn infix;						// Pointer to a function for infix parsing (handles infix operators like + FOR addition)
	Precedence precedence;				// Precedence of operator
} ParseRule;
  
Parser parser;

// holds compiled bytecode
Chunk* compilingChunk;

// returns current chunk being compiled
static Chunk* currentChunk() {
  	return compilingChunk;
}

// prints where error occured, then error msg, then set hadError flag (member of parser struct)
// If panicMode is set, return and keep on compiling. If we encounter an error, set panicMode.
// bytecode corresponding to the error will never get executed, so keep on trucking through
static void errorAt(Token* token, const char* message) {
	if (parser.panicMode) return;
	parser.panicMode = true;
	fprintf(stderr, "[line %d] Error", token->line);
  
	if (token->type == TOKEN_EOF) {
	  fprintf(stderr, " at end");
	} else if (token->type == TOKEN_ERROR) {
	  // Nothing.
	} else {
	  fprintf(stderr, " at '%.*s'", token->length, token->start);
	}
  
	fprintf(stderr, ": %s\n", message);
	parser.hadError = true;
}

// Convenient wrappers for calling errorAt() with correct token location, either previous or current one
static void error(const char* message) {
	errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
	errorAt(&parser.current, message);
}

// Advances to the next token in the source code
// asks the scanner for the next token and stores it for later use
// also stores old current token in the prevous field for later use
// parser REPORTS errors if any, not the scanner it just creates special error-tokens
// keep looping, reading tokens and reporting the errors, until we hit a non-error one or reach the end.
static void advance() {
    parser.previous = parser.current;

    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

// Consumes a token if it matches a specified type, if not, report an error
// very useful for syntax errors
static void consume(TokenType type, const char* message) {
	if (parser.current.type == type) {
	  advance();
	  return;
	}
  
	errorAtCurrent(message);
}

// Emits one bytecode instruction to the current chunk
// sends in the previous token’s line information so that runtime errors are associated with that line
static void emitByte(uint8_t byte) {
	writeChunk(currentChunk(), byte, parser.previous.line);
}

// Emits two bytecode instructions to the current chunk
static void emitBytes(uint8_t byte1, uint8_t byte2) {
	emitByte(byte1);
	emitByte(byte2);
}

// to print value we temporarily use OP_RETURN
// Emits a RETURN bytecode instruction, signaling the end of a function
static void emitReturn() {
	emitByte(OP_RETURN);
}

// Adds given value to chunk's constant table (effectively creating it) and returns its index
// Since the OP_CONSTANT instruction uses a single byte for the index operand, we can store and load only up to 256 constants in a chunk
static uint8_t makeConstant(Value value) {
	int constant = addConstant(currentChunk(), value);
	if (constant > UINT8_MAX) {
	  	error("Too many constants in one chunk.");
	  	return 0;
	}
  
	return (uint8_t)constant;
}

// Emits a constant value to the bytecode
static void emitConstant(Value value) {
	emitBytes(OP_CONSTANT, makeConstant(value));
}

// Finalizes the compilation process and optionally prints the bytecode
// uses our existing “debug” module to print out the chunk’s bytecode.
static void endCompiler() {
	emitReturn();
  #ifdef DEBUG_PRINT_CODE
  	if (!parser.hadError) {
    	disassembleChunk(currentChunk(), "code");
  	}
  #endif
}

static void expression();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

// handles binary operator parsing
static void binary() {
	TokenType operatorType = parser.previous.type;
	ParseRule* rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1));
  
	// Emit the operator instruction.
	switch (operatorType) {
		case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
		case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
		case TOKEN_GREATER:       emitByte(OP_GREATER); break;
		case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
		case TOKEN_LESS:          emitByte(OP_LESS); break;
		case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
	  	case TOKEN_PLUS:          emitByte(OP_ADD); break;
		// Having both OP_NEGATE and OP_SUBTRACT is redundant. We can replace subtraction with negate-then-add:
		case TOKEN_MINUS:         emitBytes(OP_NEGATE, OP_ADD); break; // <--
	  	case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
	  	case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
		case TOKEN_PERCENT:		  emitByte(OP_MODULUS); break;
	  	default: return; // Unreachable.
	}
}

static void literal() {
	switch (parser.previous.type) {
	  	case TOKEN_FALSE: emitByte(OP_FALSE); break;
	  	case TOKEN_NIL: emitByte(OP_NIL); break;
	  	case TOKEN_TRUE: emitByte(OP_TRUE); break;
	  	default: return; // Unreachable.
	}
}

// handles expressions inside of parentheses
static void grouping() {
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

// handles # literals parsing
static void number() {
	double value = strtod(parser.previous.start, NULL);
	emitConstant(NUMBER_VAL(value));
}

// handles unary operator parsing
static void unary() {
	TokenType operatorType = parser.previous.type;
  
	// Compile the operand.
	parsePrecedence(PREC_UNARY);
  
	// Emit the operator instruction.
	switch (operatorType) {
		case TOKEN_BANG: emitByte(OP_NOT); break;
	  	case TOKEN_MINUS: emitByte(OP_NEGATE); break;
	  	default: return; // Unreachable.
	}
}

// parsing rules for each token type
// table-based pratt parser that hands control when called to a function by pointer if possible
// new operators and datatype support
ParseRule rules[] = {
	[TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
	[TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
	[TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, 
	[TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
	[TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
	[TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
	[TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
	[TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
	[TOKEN_PERCENT]       = {NULL,     binary, PREC_TERM},
	[TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
	[TOKEN_QUESTION]      = {NULL, conditional, PREC_CONDITIONAL},
	[TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
	[TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
	[TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
	[TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
	[TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
	[TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
	[TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
	[TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
	[TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
	[TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
	[TOKEN_IDENTIFIER]    = {NULL,     NULL,   PREC_NONE},
	[TOKEN_STRING]        = {NULL,     NULL,   PREC_NONE},
	[TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
	[TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
	[TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
	[TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
	[TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
	[TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
	[TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
	[TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
	[TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
	[TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
	[TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
	[TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
	[TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
	[TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
	[TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
	[TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
	[TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
	[TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
	[TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

// parses according to precedence
// Reads the next token and look ups the corresponding ParseRule in table
// parseRule then hands it to approprate parsing function (unary(), binary(), etc.)
static void parsePrecedence(Precedence precedence) {
	advance();
	ParseFn prefixRule = getRule(parser.previous.type)->prefix;
	if (prefixRule == NULL) {
	  	error("Expect expression.");
	  	return;
	}
  
	prefixRule();
	
	while (precedence <= getRule(parser.current.type)->precedence) {
		advance();
		ParseFn infixRule = getRule(parser.previous.type)->infix;
		infixRule();
	}
}

// returns the rule at the given index
// exists solely to handle a declaration cycle in the C code
static ParseRule* getRule(TokenType type) {
	return &rules[type];
}

static void expression() {
	parsePrecedence(PREC_ASSIGNMENT);
}

static void conditional()
{
  	// Compile the then branch.
  	parsePrecedence(PREC_CONDITIONAL);

  	consume(TOKEN_COLON, "Expect ':' after then branch of conditional operator.");

  	// Compile the else branch.
  	parsePrecedence(PREC_ASSIGNMENT);

    // Emit bytecode for conditional operation
    emitByte(OP_CONDITIONAL);
}

// takes source code and generates bytecode to store in a chunk
bool compile(const char* source, Chunk* chunk) {
    initScanner(source);
	compilingChunk = chunk;

	parser.hadError = false;
	parser.panicMode = false;

    advance();
    expression();
    consume(TOKEN_EOF, "Expect end of expression.");
	endCompiler();
	return !parser.hadError;
}