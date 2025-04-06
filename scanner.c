#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
    const char* start;          // Beginning of current token
    const char* current;        // Current position in source code
    int line;                   // track current line #
} Scanner;

Scanner scanner;                

// Initializes scanner, feed a reference to source code into it
void initScanner(const char* source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

//  Returns true if c is a letter (a-z, A-Z, or _)
static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// Returns true if c is a # from 0-9
static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

// If the current character in token reaches null byte (end of input)
static bool isAtEnd() {
    return *scanner.current == '\0';
}

// Moves to next character, returns prev one
static char advance() {
    scanner.current++;
    return scanner.current[-1];
}

// Peek at current char in token without consuming
static char peek() {
    return *scanner.current;
}

// Look at next char
static char peekNext() {
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}

// To differentiate < to <=, ! to !=, etc.
static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;
    scanner.current++;
    return true;
}

static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

static Token errorToken(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner.line;
    return token;
}

static void skipWhitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                scanner.line++;
                advance();
                break;
            case '/':
                if (peekNext() == '/') { // Single-line comment
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else if (peekNext() == '*') { // Multi-line comment
                    advance(); // Consume '*'
                    while (!isAtEnd()) {
                        if (peek() == '*' && peekNext() == '/') {
                            advance(); advance(); // Consume "*/"
                            break;
                        }
                        if (peek() == '\n') scanner.line++; // Handle newlines
                        advance();
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
      }
    }
}

static TokenType checkKeyword(int start, int length,
    const char* rest, TokenType type) {
  if (scanner.current - scanner.start == start + length &&
        memcmp(scanner.start + start, rest, length) == 0) {                     // memcmp compares blocks of memory up to a byte length to see if equal
        return type;
    }

  return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
    switch (scanner.start[0]) {
        case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'c':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'l': return checkKeyword(2, 3, "ass", TOKEN_CLASS);
                    case 'a': return checkKeyword(2, 2, "se", TOKEN_CASE);
                    case 'o': return checkKeyword(2, 6, "ntinue", TOKEN_CONTINUE);
                }
            }
            break;
        case 'd': return checkKeyword(1, 6, "efault", TOKEN_DEFAULT);

        case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
                }
            } 
            break;
        case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
        case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
        case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
        case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 's':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'u':
                        return checkKeyword(2, 2, "per", TOKEN_SUPER);
                    case 'w':
                        return checkKeyword(2, 4, "itch", TOKEN_SWITCH);
                }
            }
            break;
        case 't':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
                    case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                 }
            }
            break;
        case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }

    return TOKEN_IDENTIFIER;
}

static Token identifier() {
    while (isAlpha(peek()) || isDigit(peek())) advance();
    return makeToken(identifierType());
}

static Token number() {
    while (isDigit(peek())) advance();
  
    // Look for a fractional part.
    if (peek() == '.' && isDigit(peekNext())) {
        // Consume the ".".
        advance();
  
        while (isDigit(peek())) advance();
    }
  
    return makeToken(TOKEN_NUMBER);
}

static Token string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++;
        if (peek() == '\\' && peekNext() == '"') {
            advance(); // Skip the backslash
        }
        advance();
    }
  
    if (isAtEnd()) return errorToken("Unterminated string.");
  
    // The closing quote.
    advance();
    return makeToken(TOKEN_STRING);
}

// Tokenizing Operators & Symbols (differentiates single & double char operators using match())
Token scanToken() {
    skipWhitespace();
    scanner.start = scanner.current;
  
    if (isAtEnd()) return makeToken(TOKEN_EOF);
    
    char c = advance();
    if (isAlpha(c)) return identifier();
    if (isDigit(c)) return number();

    switch (c) {
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case ':': return makeToken(TOKEN_COLON);
        case '?': return makeToken(TOKEN_QUESTION);
        case ',': return makeToken(TOKEN_COMMA);
        case '.': return makeToken(TOKEN_DOT);
        case '-': return makeToken(TOKEN_MINUS);
        case '+': return makeToken(TOKEN_PLUS);
        case '/': return makeToken(TOKEN_SLASH);
        case '*': return makeToken(TOKEN_STAR);
        case '%': return makeToken(TOKEN_PERCENT);
        // Handle two-character operators
        case '!': return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=': return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<': return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>': return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"': return string();
    }
  
    return errorToken("Unexpected character.");
}