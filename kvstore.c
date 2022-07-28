#include "private.h"

#include <string.h>
#include <assert.h>

void i_kvstore_init(kvstore *store)
{
    memset(store, 0, sizeof(kvstore));
    // Need one entry for terminator, so may as well allocate a few.
    store->entries = irealloc(store->entries, sizeof(im_kv) * 5);
    // Always a terminating entry at the end.
    store->entries[store->num_entries].key = NULL;
    store->entries[store->num_entries].value = NULL;
}

void i_kvstore_cleanup(kvstore *store)
{
    ifree(store->entries);
    ifree(store->buf);
}


bool i_kvstore_add(kvstore *store, const char* key, const char* value)
{
    size_t key_size = strlen(key) + 1;   // Include the nul.
    size_t val_size = strlen(value) + 1;

    size_t new_cap = store->buf_size + key_size + val_size;
    if (new_cap > store->buf_cap) {
        // Need to grow the string buffer.
        char* oldbuf = store->buf;
        store->buf = irealloc(store->buf, new_cap);
        if (!store->buf) {
            // Out of memory. Ditch everything and bail.
            store->entries = 0;
            return false;
        }
        store->buf_cap = new_cap;

        // relocated?
        if (store->buf != oldbuf) {
            // Update the string pointers for the new buffer location. 
            ptrdiff_t delta = store->buf - oldbuf;
            size_t i;
            for (i = 0; i < store->num_entries; ++i) {
                store->entries[i].key += delta;
                store->entries[i].value += delta;
            }
        }
    }
    assert(new_cap <= store->buf_cap);

    // Allocate a new entry (plus an extra null entry after the end).
    store->entries = irealloc(store->entries, sizeof(im_kv) * (store->num_entries+2));

    // Add the strings to the buffer.
    memcpy(store->buf + store->buf_size, key, key_size);
    store->entries[store->num_entries].key = store->buf + store->buf_size;
    store->buf_size += key_size;

    memcpy(store->buf + store->buf_size, value, val_size);
    store->entries[store->num_entries].value = store->buf + store->buf_size;
    store->buf_size += val_size;

    ++store->num_entries;
    // And the terminating entry (not part of the count).
    store->entries[store->num_entries].key = NULL;
    store->entries[store->num_entries].value = NULL;

    return true;
}




