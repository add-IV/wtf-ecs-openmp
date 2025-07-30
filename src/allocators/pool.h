#ifndef ALLOCATORS_H
#define ALLOCATORS_H

#include <stdint.h>

typedef struct pool_t
{
	uint8_t* allocation;
	int32_t head; // allocation relative address
	int32_t alloc_size; // treated as NULL
	int32_t chunk_size;
	int32_t chunk_cap;
} pool_t;


void pool_init(pool_t* pool, int32_t chunk_size, int32_t chunk_cap);
void pool_free(pool_t* pool, void* ptr);
void* pool_calloc(pool_t* pool);
void pool_free_all(pool_t* pool);

#endif /* End ALLOCATORS_H */
