// Include guard
#ifndef clox_scanner_h
#define clox_scanner_h

typedef enum {
    // Single-character tokens.
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN, TOKEN_QUESTION,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE, TOKEN_COLON,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR, TOKEN_PERCENT,
    // One or two character tokens.
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    // Literals.
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,
    // Reserved Keywords.
    TOKEN_AND, TOKEN_CLASS, TOKEN_ELSE, TOKEN_FALSE, TOKEN_DEFAULT,
    TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_NIL, TOKEN_OR,
    TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS, TOKEN_CONTINUE,
    TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE, TOKEN_CASE, TOKEN_SWITCH,
    // TOKEN_ERROR = Invalid token, TOKEN_EOF = Marks end of file (used to terminate scanning)
    TOKEN_ERROR, TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;                 // Specifies token type
    const char* start;              // Points to the first character of the token in the source code 
    int length;                     // The token's length
    int line;                       // Source Code Line # of token (useful for error reporting, ex: Syntax Error on line 23 )
} Token;

// Scanner functions
void initScanner(const char* source);   
Token scanToken();                      

// End include guard
#endif