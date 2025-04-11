// include guard
#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

// for key/value pair, key will be a string
typedef struct {
    ObjString* key;
    Value value;
} Entry;

// hash table struct (one big array with buckets to store key/value pairs, kind of like bucket sort in a way)
typedef struct {
  int count;                        // keeps track of # of key/value pairs currently stored in array
  int capacity;                     // keeps track of the allocated size of the array 
  Entry* entries;
} Table;

// hashtable-specific func declarations
void initTable(Table* table);
void freeTable(Table* table);
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash);
void tableRemoveWhite(Table* table);
void markTable(Table* table);


// end include guard
#endif