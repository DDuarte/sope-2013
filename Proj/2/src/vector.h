#ifndef VECTOR_H
#define VECTOR_H

/** @defgroup vector vector
 * @{
 * Functions to manage vector ADT
 */

typedef struct vector_t
{
    void** buffer; ///< Data
    int capacity; ///< Allocated size of elements
    int count; ///< Number of elements
} vector;

void vector_new(vector* v); ///< Create new vector
void vector_free(vector* v); ///< Delete vector
int vector_size(const vector* v); ///< Size of vector
int vector_capacity(const vector* v); ///< Capacity of vector
void vector_push_back(vector* v, void* data); ///< Adds element to back of vector
void vector_insert(vector* v, void* data, int index); ///< Adds element at position index
void* vector_get(const vector* v, int index); ///< Element at position index
void vector_erase(vector* v, int index); ///< Removes element at position index

/**@}*/

#endif // VECTOR_H
