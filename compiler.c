#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "scanner.h"

#define MAX_CASES 256

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

// global variables used for forloop parsing/compiling
int innermostLoopStart = -1;
int innermostLoopScopeDepth = 0;

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
typedef void (*ParseFn)(bool canAssign);

// Rules for parsing each tokenType
typedef struct {
	ParseFn prefix;						// Pointer to a function for prefix parsing (handles prefix operators like - FOR negation)
	ParseFn infix;						// Pointer to a function for infix parsing (handles infix operators like + FOR addition)
	Precedence precedence;				// Precedence of operator
} ParseRule;

// for local variables
typedef struct {
    Token name;  						// The name of the local variable (stored as a Token)
    int depth;   						// The depth of the block in which the variable was declared
	bool isCaptured;					// whether or not a local variable has been captured by a closure
} Local;

// upvalue refers to a local variable in an enclosing function
// when a closure captures a variable it must be stored in upvalue struct so it can be referenced even when enclosing scope had ended
typedef struct {
	uint8_t index;						// index of the local variable being captured
	bool isLocal;						// whether or not variable is local to immediate closure (true) or is itself an upvalue from a higher level (false)
} Upvalue;

// lets the compiler tell when it’s compiling top-level code versus the body of a function
typedef enum {
	TYPE_FUNCTION,
	TYPE_INITIALIZER,
	TYPE_METHOD,
	TYPE_SCRIPT
} FunctionType;

typedef struct Compiler {
	struct Compiler* enclosing;
	ObjFunction* function;				// function being compiled
	FunctionType type;  				// the type of the function (TYPE_FUNCTION or TYPE_SCRIPT)

	Local locals[UINT8_COUNT];			// array of local structs storing info about local variables
	int localCount;						// tracks how many locals are in scope—how many of those array slots are in use
	Upvalue upvalues[UINT8_COUNT];
	int scopeDepth;						// Tracks the current depth of nested blocks (used for scoping)
} Compiler;

typedef struct ClassCompiler {
	struct ClassCompiler* enclosing;	//  Pointer to the enclosing ClassCompiler (for nested classes)
	bool hasSuperclass;					// whether class has a superclass (used for inheritance)	
} ClassCompiler;
  
Parser parser;

Compiler* current = NULL;
ClassCompiler* currentClass = NULL;


Table stringConstants;

// current chunk = chunk owned by function we are currently compiling
static Chunk* currentChunk() {
  return &current->function->chunk;
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

static bool check(TokenType type) {
	return parser.current.type == type;
}

// for "matching" token type 
static bool match(TokenType type) {
	if (!check(type)) return false;
	advance();
	return true;
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

// emits the bytecode to jump backward in a loop and generates OP_LOOP instruction
static void emitLoop(int loopStart) {
	emitByte(OP_LOOP);
  
	int offset = currentChunk()->count - loopStart + 2;
	if (offset > UINT16_MAX) error("Loop body too large.");
  
	emitByte((offset >> 8) & 0xff);
	emitByte(offset & 0xff);
}

// writes placeholder operand for the jump offset (which will be complete with backpatching function)
static int emitJump(uint8_t instruction) {
	emitByte(instruction);
	emitByte(0xff);
	emitByte(0xff);
	return currentChunk()->count - 2;
}

// to print value we temporarily use OP_RETURN
// Emits a RETURN bytecode instruction, signaling the end of a function
// check the type to decide whether to insert the initializer-specific behavior
// instead of pushing nil onto the stack before returning, we load slot zero, which contains the instance
// also called when compiling a return statement without a value
// correctly handles cases where the user does an early return inside the initializer
static void emitReturn() {
	if (current->type == TYPE_INITIALIZER) {
		emitBytes(OP_GET_LOCAL, 0);
	} else {
		emitByte(OP_NIL);
	}

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

// returns the offset of the emitted instruction in the chunk. After compiling the then branch, pass in the offset to it
static void patchJump(int offset) {
	// -2 to adjust for the bytecode for the jump offset itself.
	int jump = currentChunk()->count - offset - 2;
  
	if (jump > UINT16_MAX) {
	  	error("Too much code to jump over.");
	}
  
	currentChunk()->code[offset] = (jump >> 8) & 0xff;
	currentChunk()->code[offset + 1] = jump & 0xff;
}

// to initialize compiler
// compiler implicitly claims stack slot zero from locals array for the VM’s own internal use
static void initCompiler(Compiler* compiler, FunctionType type) {
	compiler->enclosing = current;
	compiler->function = NULL;
	compiler->type = type;
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	compiler->function = newFunction();
	current = compiler;
	if (type != TYPE_SCRIPT) {
		current->function->name = copyString(parser.previous.start, parser.previous.length);
	}
	
	Local* local = &current->locals[current->localCount++];
	local->depth = 0;
	local->isCaptured = false;
	if (type != TYPE_FUNCTION) {
		local->name.start = "this";
		local->name.length = 4;
	} else {
		local->name.start = "";
		local->name.length = 0;
	}
}

// Finalizes the compilation process and optionally prints the bytecode
// uses our existing “debug” module to print out the chunk’s bytecode.
// when disassembling, check if function has name or not. 
// If it does it is a user-defined function, else, it is implicit function for top-level code
static ObjFunction* endCompiler() {
	emitReturn();
	ObjFunction* function = current->function;

  #ifdef DEBUG_PRINT_CODE
  	if (!parser.hadError) {
		disassembleChunk(currentChunk(), function->name != NULL ? function->name->chars : "<script>");
  	}
  #endif

  current = current->enclosing;
  return function;
}

// to create a scope, just increment it's depth
static void beginScope() {
	current->scopeDepth++;
}

// same concept for ending one
// Removes all local variables that go out of scope
// If a variable is captured by a closure, emit OP_CLOSE_UPVALUE to move it to the heap
// Otherwise, emit OP_POP to discard the variable
static void endScope() {
	current->scopeDepth--;
	
	while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth) {
		if (current->locals[current->localCount - 1].isCaptured) {
			emitByte(OP_CLOSE_UPVALUE);
		} else {
			emitByte(OP_POP);
		}
   		current->localCount--;
 	}
}

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

// Ensures that each identifier is stored only once in the constant table, avoiding duplicates
static uint8_t identifierConstant(Token* name) {
  // See if we already have it.
  ObjString* string = copyString(name->start, name->length);
  Value indexValue;
  if (tableGet(&stringConstants, string, &indexValue)) {
    // We do.
    return (uint8_t)AS_NUMBER(indexValue);
  }

  uint8_t index = makeConstant(OBJ_VAL(string));
  tableSet(&stringConstants, string, NUMBER_VAL((double)index));
  return index;
}

// purpose is in the name
// compare lengths first, then if same, check characters using memcmp()
static bool identifiersEqual(Token* a, Token* b) {
	if (a->length != b->length) return false;
	return memcmp(a->start, b->start, a->length) == 0;
}

// walks the list of locals currently in scope (walk array backwards to find last declared variable with identifier)
// If one has the same name as the identifier token, the identifier must refer to that variable
static int resolveLocal(Compiler* compiler, Token* name) {
	for (int i = compiler->localCount - 1; i >= 0; i--) {
	  	Local* local = &compiler->locals[i];
	  	if (identifiersEqual(name, &local->name)) {
			if (local->depth == -1) {
				error("Can't read local variable in its own initializer.");
			}
			return i;
	  	}
	}
  
	return -1;
}

// Adds a new upvalue to a function's list of upvalues if it doesn’t already exist and returns the index of that upvalue
static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
	int upvalueCount = compiler->function->upvalueCount;
	
	for (int i = 0; i < upvalueCount; i++) {
		Upvalue* upvalue = &compiler->upvalues[i];
		if (upvalue->index == index && upvalue->isLocal == isLocal) {
			return i;
		}
	}

	if (upvalueCount == UINT8_COUNT) {
		error("Too many closure variables in function.");
		return 0;
	}

	compiler->upvalues[upvalueCount].isLocal = isLocal;
	compiler->upvalues[upvalueCount].index = index;
	return compiler->function->upvalueCount++;
}

// Resolves a variable from an enclosing function’s scope and returns an upvalue index for it. It builds up the chain of closures if needed.
static int resolveUpvalue(Compiler* compiler, Token* name) {
	if (compiler->enclosing == NULL) return -1;
  
	int local = resolveLocal(compiler->enclosing, name);
	if (local != -1) {
		compiler->enclosing->locals[local].isCaptured = true;
	  	return addUpvalue(compiler, (uint8_t)local, true);
	}
	
	int upvalue = resolveUpvalue(compiler->enclosing, name);
	if (upvalue != -1) {
	  	return addUpvalue(compiler, (uint8_t)upvalue, false);
	}
  
	return -1;
}

// initializes the next available Local in the compiler’s array of variables
static void addLocal(Token name) {
	if (current->localCount == UINT8_COUNT) {
		error("Too many local variables in function.");
		return;
	}
	Local* local = &current->locals[current->localCount++];
	local->name = name;
	local->depth = -1;
	local->isCaptured = false;
}

// where compiler records the existence of a variable (ONLY for locals since globals are late-bound)
static void declareVariable() {
	if (current->scopeDepth == 0) return;
  
	Token* name = &parser.previous;
	for (int i = current->localCount - 1; i >= 0; i--) {
		Local* local = &current->locals[i];
		if (local->depth != -1 && local->depth < current->scopeDepth) {
		  	break; 
		}
	
		if (identifiersEqual(name, &local->name)) {
		  	error("Already a variable with this name in this scope.");
		}
	}

	addLocal(*name);
}

// parses variable name and returns its constant index
// delcare the variable and exit function if we are in a local scope
static uint8_t parseVariable(const char* errorMessage) {
	consume(TOKEN_IDENTIFIER, errorMessage);

	declareVariable();
	if (current->scopeDepth > 0) return 0;

	return identifierConstant(&parser.previous);
}

static void markInitialized() {
	if (current->scopeDepth == 0) return;
	current->locals[current->localCount - 1].depth = current->scopeDepth;
}

// creates bytecode to define a global variable
static void defineVariable(uint8_t global) {
	if (current->scopeDepth > 0) {
		markInitialized();
		return;
	}
	
	emitBytes(OP_DEFINE_GLOBAL, global);
}

// compile arguments for function (arguments != parameters)
static uint8_t argumentList() {
	uint8_t argCount = 0;
	if (!check(TOKEN_RIGHT_PAREN)) {
	  do {
			expression();
			if (argCount == 255) {
				error("Can't have more than 255 arguments.");
			}
			argCount++;
	  } while (match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
	return argCount;
}

// using short-circuit method to lessen resources
static void and_(bool canAssign) {
	int endJump = emitJump(OP_JUMP_IF_FALSE);			// jump if left operand is false
  
	emitByte(OP_POP);									// discard left operand if true
	parsePrecedence(PREC_AND);							// parse right operand
  
	patchJump(endJump);									// patch jump to skip right side if left was false
}

// handles binary operator parsing
static void binary(bool canAssign) {
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
		case TOKEN_MINUS:         emitBytes(OP_NEGATE, OP_ADD); break;
	  	case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
	  	case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
		case TOKEN_PERCENT:		  emitByte(OP_MODULUS); break;
	  	default: return; // Unreachable.
	}
}

// when parsing through a call to a function
static void call(bool canAssign) {
	uint8_t argCount = argumentList();
	emitBytes(OP_CALL, argCount);
}

// for parsing through after a dot for instances
// load token’s lexeme (property name) into the constant table as a string so that the name is available at runtime
static void dot(bool canAssign) {
	consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
	uint8_t name = identifierConstant(&parser.previous);
  
	if (canAssign && match(TOKEN_EQUAL)) {
		expression();
		emitBytes(OP_SET_PROPERTY, name);
	} else if (match(TOKEN_LEFT_PAREN)) {
		uint8_t argCount = argumentList();
		emitBytes(OP_INVOKE, name);
		emitByte(argCount);
	} else {
	  	emitBytes(OP_GET_PROPERTY, name);
	}
}

// compiles literal tokens like nil, true, false into bytecode instructions
static void literal(bool canAssign) {
	switch (parser.previous.type) {
	  	case TOKEN_FALSE: emitByte(OP_FALSE); break;
	  	case TOKEN_NIL: emitByte(OP_NIL); break;
	  	case TOKEN_TRUE: emitByte(OP_TRUE); break;
	  	default: return; // Unreachable.
	}
}

// handles expressions inside of parentheses
static void grouping(bool canAssign) {
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

// handles # literals parsing
static void number(bool canAssign) {
	double value = strtod(parser.previous.start, NULL);
	emitConstant(NUMBER_VAL(value));
}

// also uses short-circuiting technique
static void or_(bool canAssign) {
	int elseJump = emitJump(OP_JUMP_IF_FALSE); 			// jump if left is false
	int endJump = emitJump(OP_JUMP);           			// if true, skip right side

	patchJump(elseJump); 								// jump here if false
	emitByte(OP_POP);    								// discard left if false
	
	parsePrecedence(PREC_OR); 							// parse right side
	patchJump(endJump);									// jump here if left was true
}

// when parser hits string token, call this function from pratt-parse table
// the + 1 and - 2 parts trim the leading and trailing quotation marks
// it then creates a string object, wraps it in a Value, and stuffs it into the constant table
static void string(bool canAssign) {
	emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

// determines whether var is global or local, then based on next token being '=', determines value assignment
static void namedVariable(Token name, bool canAssign) {
	uint8_t getOp, setOp;
	int arg = resolveLocal(current, &name);
	if (arg != -1) {
	  	getOp = OP_GET_LOCAL;
	 	setOp = OP_SET_LOCAL;
	} else if ((arg = resolveUpvalue(current, &name)) != -1) {
		getOp = OP_GET_UPVALUE;
		setOp = OP_SET_UPVALUE;
	} else {
	  	arg = identifierConstant(&name);
	  	getOp = OP_GET_GLOBAL;
	  	setOp = OP_SET_GLOBAL;
	}

	if (canAssign && match(TOKEN_EQUAL)) {
		expression();
		emitBytes(setOp, (uint8_t)arg);
	} else {
		emitBytes(getOp, (uint8_t)arg);
	}
}

// handles variable access or assignment when the compiler encounters a variable name in the source code
// if canAssign True = this variable is allowed to appear on the left side of an assignment (=)
// if canAssign False = this is a read-only reference (like inside an expression)
// thing wrapper function for namedVariable
static void variable(bool canAssign) {
	namedVariable(parser.previous, canAssign);
}

// This function creates a fake or synthetic Token for internal use (compiler needs a stand-in identifier)
static Token syntheticToken(const char* text) {
	Token token;
	token.start = text;
	token.length = (int)strlen(text);
	return token;
}

// when parser encounters a "super" keyword
static void super_(bool canAssign) {
	if (currentClass == NULL) {
		error("Can't use 'super' outside of a class.");
	} else if (!currentClass->hasSuperclass) {
		error("Can't use 'super' in a class with no superclass.");
	}
	
	consume(TOKEN_DOT, "Expect '.' after 'super'.");
	consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
	uint8_t name = identifierConstant(&parser.previous);
	
	namedVariable(syntheticToken("this"), false);
	if (match(TOKEN_LEFT_PAREN)) {
		uint8_t argCount = argumentList();
		namedVariable(syntheticToken("super"), false);
		emitBytes(OP_SUPER_INVOKE, name);
		emitByte(argCount);
	} else {
		namedVariable(syntheticToken("super"), false);
		emitBytes(OP_GET_SUPER, name);
	}
}

// when parser encounters a "this" keyword in prefix position
// using the this keyword outside of a class is forbidden
static void this_(bool canAssign) {
	if (currentClass == NULL) {
		error("Can't use 'this' outside of a class.");
		return;
	}
	
	variable(false);
} 

// handles unary operator parsing
static void unary(bool canAssign) {
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
	[TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL},
	[TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
	[TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, 
	[TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
	[TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
	[TOKEN_DOT]           = {NULL,     dot,    PREC_CALL},
	[TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
	[TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
	[TOKEN_PERCENT]       = {NULL,     binary, PREC_TERM},
	[TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
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
	[TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
	[TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
	[TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
	[TOKEN_AND]           = {NULL,     and_,   PREC_AND},
	[TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
	[TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
	[TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
	[TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
	[TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
	[TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
	[TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
	[TOKEN_OR]            = {NULL,     or_,    PREC_OR},
	[TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
	[TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
	[TOKEN_SUPER]         = {super_,   NULL,   PREC_NONE},
	[TOKEN_THIS]          = {this_,    NULL,   PREC_NONE},
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
  
	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefixRule(canAssign);
	
	while (precedence <= getRule(parser.current.type)->precedence) {
		advance();
		ParseFn infixRule = getRule(parser.previous.type)->infix;
		infixRule(canAssign);
	}
	
	if (canAssign && match(TOKEN_EQUAL)) {
		error("Invalid assignment target.");
	}
}

// returns the rule at the given index
// exists solely to handle a declaration cycle in the C code
static ParseRule* getRule(TokenType type) {
	return &rules[type];
}

// starts parsing expressions
static void expression() {
	parsePrecedence(PREC_ASSIGNMENT);
}

// parses block of code (ex: {...block of code...})
static void block() {
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
	  	declaration();
	}
  
	consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type) {
    // Create a new compiler for the function being compiled.
    Compiler compiler;
    initCompiler(&compiler, type);

    // Begin a new scope for the function body.
    beginScope();

    // Parse the parameter list inside parentheses ().
    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) { // If there are parameters, parse them.
        do {
            // Increment the function's arity (number of parameters).
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }

            // Parse the parameter name and add it as a local variable.
            uint8_t constant = parseVariable("Expect parameter name.");
            defineVariable(constant);
        } while (match(TOKEN_COMMA)); // Continue parsing parameters separated by commas.
    }

    // Ensure the parameter list is properly closed with a `)`.
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

    // Parse the function body enclosed in `{}`.
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    // Finalize the compilation of the function and emit it as a constant.
    // Emit the OP_CLOSURE instruction to create a closure for the function.
    // For each upvalue, emit whether it is local (1) or an upvalue from a higher scope (0),
    // followed by its index in the stack or upvalue array.
    ObjFunction* function = endCompiler();
	emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));
	
	for (int i = 0; i < function->upvalueCount; i++) {
		emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
		emitByte(compiler.upvalues[i].index);
	}
}

// compiler adds the method name token’s lexeme to the constant table, getting back a table index
// emit an OP_METHOD instruction with that index as the operand, which is the name
static void method() {
	consume(TOKEN_IDENTIFIER, "Expect method name.");
	uint8_t constant = identifierConstant(&parser.previous);
	
	FunctionType type = TYPE_METHOD;
	if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0) {
	  	type = TYPE_INITIALIZER;
	}
  
	function(type);
	emitBytes(OP_METHOD, constant);
}

// compiles a class declaration
// adds class name as string to constant table for runtime use
// declares class name as a variable in current scope and defines it so that it can be used within class body (static & factory methods)
// Emits bytecode instruction to create a class object with the given name
// support for superclasses/inheritance
// initialize classCompiler, and assume it is not a subclass
// if we see a superclass clause, we know we are compiling a subclass
static void classDeclaration() {
	consume(TOKEN_IDENTIFIER, "Expect class name.");
	Token className = parser.previous;
	uint8_t nameConstant = identifierConstant(&parser.previous);
	declareVariable();
  
	emitBytes(OP_CLASS, nameConstant);
	defineVariable(nameConstant);

	ClassCompiler classCompiler;
	classCompiler.hasSuperclass = false;
	classCompiler.enclosing = currentClass;
	currentClass = &classCompiler;

	if (match(TOKEN_LESS)) {
		consume(TOKEN_IDENTIFIER, "Expect superclass name.");
		variable(false);
		
		if (identifiersEqual(&className, &parser.previous)) {
			error("A class can't inherit from itself.");
		}

		beginScope();
		addLocal(syntheticToken("super"));
		defineVariable(0);	
	  
		namedVariable(className, false);
		emitByte(OP_INHERIT);
		classCompiler.hasSuperclass = true;
	}
	
	namedVariable(className, false);
	consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		method();
	}
	consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
	emitByte(OP_POP);
	
	if (classCompiler.hasSuperclass) {
		endScope();
	}

	currentClass = currentClass->enclosing;
}

// compiles function declaration
static void funDeclaration() {
	uint8_t global = parseVariable("Expect function name.");
	markInitialized();
	function(TYPE_FUNCTION);
	defineVariable(global);
}

// if var token is matched, jump to this function (parses and compiles var declaration)
// if there is an initializer like '=' parse expression, if not, value within var is set to nil
static void varDeclaration() {
	uint8_t global = parseVariable("Expect variable name.");
  
	if (match(TOKEN_EQUAL)) {
	  	expression();
	} else {
	  	emitByte(OP_NIL);
	}
	consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
  
	defineVariable(global);
}

// Ex: a + b; (it is popped off stack since we usually want to discard expressions like this)
// more cares has to be taken into account for variables
static void expressionStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
	emitByte(OP_POP);
}

// for parsing for loops, continue keyword support added
static void forStatement() {
	beginScope();
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
	if (match(TOKEN_SEMICOLON)) {
		// No initializer.
	} else if (match(TOKEN_VAR)) {
		varDeclaration();
	} else {
		expressionStatement();
	}

	int surroundingLoopStart = innermostLoopStart; 
	int surroundingLoopScopeDepth = innermostLoopScopeDepth; 
	innermostLoopStart = currentChunk()->count; 
	innermostLoopScopeDepth = current->scopeDepth;   
  
	int loopStart = currentChunk()->count;
	int exitJump = -1;
	if (!match(TOKEN_SEMICOLON)) {
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");
	
		// Jump out of the loop if the condition is false.
		exitJump = emitJump(OP_JUMP_IF_FALSE);
		emitByte(OP_POP); // Condition.
	}
  
	if (!match(TOKEN_RIGHT_PAREN)) {
		int bodyJump = emitJump(OP_JUMP);
		int incrementStart = currentChunk()->count;
		expression();
		emitByte(OP_POP);
		consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
	
		emitLoop(loopStart);
		loopStart = incrementStart;

		emitLoop(innermostLoopStart); 
		innermostLoopStart = incrementStart; 
		patchJump(bodyJump);
	}
  
	statement();
	
	emitLoop(innermostLoopStart); 

	if (exitJump != -1) {
		patchJump(exitJump);
		emitByte(OP_POP); // Condition.
	}

	innermostLoopStart = surroundingLoopStart; 
	innermostLoopScopeDepth = surroundingLoopScopeDepth; 
	
	endScope();
}

// handles continue keyword during parsing
// checks to see if inside loop, then pops all local variables inside current iteration, then jumps to top of loop using innermostLoopStart 
static void continueStatement() {
	if (innermostLoopStart == -1) {
	  	error("Can't use 'continue' outside of a loop.");
	}
  
	consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
  
	// Discard any locals created inside the loop.
	for (int i = current->localCount - 1;
		i >= 0 && current->locals[i].depth > innermostLoopScopeDepth;
		i--) {
	  	emitByte(OP_POP);
	}
  
	// Jump to top of current innermost loop.
	emitLoop(innermostLoopStart);
}

// for parsing/compiling an if statement
static void ifStatement() {
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition."); 
  
	int thenJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);
	statement();
  
	int elseJump = emitJump(OP_JUMP);

	patchJump(thenJump);
	emitByte(OP_POP);

	if (match(TOKEN_ELSE)) statement();
	patchJump(elseJump);
}

// for parsing switch statement (might be changed later on to be more pythonic depending on efficiency)
static void switchStatement() {
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'switch'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after value.");
	consume(TOKEN_LEFT_BRACE, "Expect '{' before switch cases.");
  
	int state = 0; // 0: before all cases, 1: before default, 2: after default.
	int caseEnds[MAX_CASES];
	int caseCount = 0;
	int previousCaseSkip = -1;
  
	while (!match(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
	  if (match(TOKEN_CASE) || match(TOKEN_DEFAULT)) {
		TokenType caseType = parser.previous.type;
  
		if (state == 2) {
		  error("Can't have another case or default after the default case.");
		}
  
		if (state == 1) {
		  // At the end of the previous case, jump over the others.
		  caseEnds[caseCount++] = emitJump(OP_JUMP);
  
		  // Patch its condition to jump to the next case (this one).
		  patchJump(previousCaseSkip);
		  emitByte(OP_POP);
		}
  
		if (caseType == TOKEN_CASE) {
		  state = 1;
  
		  // See if the case is equal to the value.
		  emitByte(OP_DUP);
		  expression();
  
		  consume(TOKEN_COLON, "Expect ':' after case value.");
  
		  emitByte(OP_EQUAL);
		  previousCaseSkip = emitJump(OP_JUMP_IF_FALSE);
  
		  // Pop the comparison result.
		  emitByte(OP_POP);
		} else {
		  state = 2;
		  consume(TOKEN_COLON, "Expect ':' after default.");
		  previousCaseSkip = -1;
		}
	  } else {
		// Otherwise, it's a statement inside the current case.
		if (state == 0) {
		  error("Can't have statements before any case.");
		}
		statement();
	  }
	}
  
	// If we ended without a default case, patch its condition jump.
	if (state == 1) {
	  patchJump(previousCaseSkip);
	  emitByte(OP_POP);
	}
  
	// Patch all the case jumps to the end.
	for (int i = 0; i < caseCount; i++) {
	  patchJump(caseEnds[i]);
	}
  
	emitByte(OP_POP); // The switch value.
}

// parses through expression expecting a semicolon token, and emits the print bytecode instruction
static void printStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(OP_PRINT);
}

// compiles a return statement (result of an expression)
static void returnStatement() {
	if (current->type == TYPE_SCRIPT) {
		error("Can't return from top-level code.");
	}	

	if (match(TOKEN_SEMICOLON)) {
	  	emitReturn();
	} else {
		if (current->type == TYPE_INITIALIZER) {
			error("Can't return a value from an initializer.");
		}
	  
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
		emitByte(OP_RETURN);
	}
}

// Records start of loop, Parses condition, jump out if false, Parse body, then jump back to top
// Patches the exit jump, and pops the condition result afterwards
static void whileStatement() {
	int loopStart = currentChunk()->count;
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
  
	int exitJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);
	statement();
	emitLoop(loopStart);
  
	patchJump(exitJump);
	emitByte(OP_POP);
}

// called when parser in panic mode (after there was an error), to keep on parsing properly (avoids cascading compile errors) 
static void synchronize() {
	parser.panicMode = false;
  
	while (parser.current.type != TOKEN_EOF) {
	  	if (parser.previous.type == TOKEN_SEMICOLON) return;
	  	switch (parser.current.type) {
			case TOKEN_CLASS:
			case TOKEN_FUN:
			case TOKEN_VAR:
			case TOKEN_FOR:
			case TOKEN_IF:
			case TOKEN_WHILE:
			case TOKEN_PRINT:
			case TOKEN_RETURN:
		  		return;
  
			default:
		  		; // Do nothing.
	  	}
  
	  	advance();
	}
}

// for compiling single declaration
// if panic mode is on, then implement synchronization
// panic mode error recovery to minimize the number of cascaded compile errors that it reports,
// compiler exits panic mode when it reaches a synchronization point
static void declaration() {
	if (match(TOKEN_CLASS)) {
		classDeclaration();
	} else if (match(TOKEN_FUN)) {
		funDeclaration();
	} else if (match(TOKEN_VAR)) {
		varDeclaration();
	} else {
		statement();
	}

	if (parser.panicMode) synchronize();
}

// if you do not see a print keyword, it must be an expression statement
static void statement() {
	if (match(TOKEN_PRINT)) {
	  	printStatement();
	} else if (match(TOKEN_CONTINUE)) {
		continueStatement();
	} else if (match(TOKEN_SWITCH)) {
		switchStatement();
	} else if (match(TOKEN_FOR)) {
		forStatement();
	} else if (match(TOKEN_IF)) {
		ifStatement();
	} else if (match(TOKEN_RETURN)) {
		returnStatement();
	} else if (match(TOKEN_WHILE)) {
		whileStatement();
	} else if (match(TOKEN_LEFT_BRACE)) {
		beginScope();
		block();
		endScope();
	} else {
		expressionStatement();
	}
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
// keep compiling declarations until hitting end of source file
ObjFunction* compile(const char* source) {
    initScanner(source);
	Compiler compiler;
	initCompiler(&compiler, TYPE_SCRIPT);


	parser.hadError = false;
	parser.panicMode = false;
	initTable(&stringConstants);

    advance();
    
  	while (!match(TOKEN_EOF)) {
    	declaration();
  	}

	freeTable(&stringConstants);
	ObjFunction* function = endCompiler();
	return parser.hadError ? NULL : function;
}

// for marking roots that compiler makes (compiler itself periodically grabs memory from the heap for literals and the constant table)
void markCompilerRoots() {
	Compiler* compiler = current;
	while (compiler != NULL) {
		markObject((Obj*)compiler->function);
		compiler = compiler->enclosing;
	}
}