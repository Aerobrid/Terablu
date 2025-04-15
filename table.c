#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

// hash-table's load factor
#define TABLE_MAX_LOAD 0.75

// a hash table initially starts with no entries (0 capacity and a NULL array)
void initTable(Table* table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

// for freeing hash table (remember we are implementing it as a dynamic array)
void freeTable(Table* table) {
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

// takes key and the array of buckets, and figures out which bucket entry is in
// will be used to look up existing entries in the hash table and to decide where to insert new ones 
// optimization: bitmasking calculation for index instead of using modulus operator (% is really slow executing)
static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
    uint32_t index = key->hash & (capacity - 1);
    Entry* tombstone = NULL;

    for (;;) {
        Entry* entry = &entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                // Empty entry.
                return tombstone != NULL ? tombstone : entry;
            } else {
                // We found a tombstone.
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) {
            // We found the key.
            return entry;
        }

        index = (index + 1) & (capacity - 1);
    }
}

bool tableGet(Table* table, ObjString* key, Value* value) {
    if (table->count == 0) return false;                                    // If the table is empty, return false.

    Entry* entry = findEntry(table->entries, table->capacity, key);         // Locate the entry for the key.
    if (entry->key == NULL) return false;                                   // If the key is not found, return false.

    *value = entry->value;                                                  // If the key is found, store the value in the provided pointer.
    return true;                                                            // Indicate success.
}

// creates a bucket array with "capacity" entries
// initializes every bucket to be empty and stores array in hash-table main struct
// to resize bucket array, allocate new array and re-insert all non-empty buckets into it
static void adjustCapacity(Table* table, int capacity) {
    Entry* entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key == NULL) continue;

        Entry* dest = findEntry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }
  
    FREE_ARRAY(Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

// adds the given key/value pair to the given hash table, allocate entry array if haven't already
// we grow array when its 75% full (0.75 load factor)
bool tableSet(Table* table, ObjString* key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }
    
    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL;
    if (isNewKey && IS_NIL(entry->value)) table->count++;
  
    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableDelete(Table* table, ObjString* key) {
    if (table->count == 0) return false;
  
    // Find the entry.
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;
  
    // Place a tombstone in the entry.
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

// walks through bucket array, adds entry to destination hash-table whenever finding non-empty bucket
void tableAddAll(Table* from, Table* to) {
    for (int i = 0; i < from->capacity; i++) {
        Entry* entry = &from->entries[i];
        if (entry->key != NULL) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

// Instead of creating a new ObjString for every string, we check if an identical string already exists in the hash table
// If it does, we reuse the existing ObjString to save memory and avoid duplicate strings
// optimization: bitmasking calculation for index instead of using modulus operator (% is really slow executing)
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash) {
    if (table->count == 0) return NULL;

    uint32_t index = hash & (table->capacity - 1);
    for (;;) {
        Entry* entry = &table->entries[index];
        if (entry->key == NULL) {
            // Stop if an empty non-tombstone entry is found
            if (IS_NIL(entry->value)) return NULL;
        } else if (entry->key->length == length && entry->key->hash == hash && memcmp(entry->key->chars, chars, length) == 0) {
            return entry->key;
        }

        index = (index + 1) & (table->capacity - 1);
    }
}

// during sweep phase of GC, this deletes any table entries whose keys are no longer marked (unreachable)
void tableRemoveWhite(Table* table) {
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->obj.isMarked) {
            tableDelete(table, entry->key);
        }
    }
}

// walks the table array and ensures that all string keys and associated values are marked 
void markTable(Table* table) {
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        markObject((Obj*)entry->key);
        markValue(entry->value);
    }
}