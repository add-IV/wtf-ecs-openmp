#include "pool.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

typedef struct pool_node_t
{
	int32_t next;
} pool_node_t;

void pool_init(pool_t* pool, const int32_t chunk_size, const int32_t chunk_cap)
{
	const size_t alloc_size = chunk_size * chunk_cap;
	assert(chunk_size >= sizeof(pool_node_t) && "Chunks too small!");
	assert(alloc_size >= sizeof(pool_node_t) && "Allocation too small!");
	pool->allocation = malloc(alloc_size + sizeof(pool_node_t));
	pool->chunk_size = chunk_size;
	pool->chunk_cap = chunk_cap;
	pool->alloc_size = alloc_size;
	pool_free_all(pool);
}

/* // UPDATE: just assume ptr is from the pool */
void pool_free(pool_t* pool, void* ptr)
{
	const uint8_t* p = ptr;
	const int32_t k = pool->allocation <= p & p < pool->allocation + pool->alloc_size;
	const pool_node_t* head = pool->allocation + pool->head;
	pool_node_t* node = ptr;
	node->next = pool->head;
	pool->head = k * (p - pool->allocation) + (1 - k) * pool->head;
}


void* pool_calloc(pool_t* pool)
{
	assert(pool->head < pool->alloc_size && "Pool has no available memory!");
	pool_node_t* node = pool->allocation + pool->head;
	pool->head = node->next;
	memset(node, 0x00, pool->chunk_size);
	return node;
}

void pool_free_all(pool_t* pool)
{
	const int32_t size = pool->chunk_size; // hoist pls
	const uint8_t* alloc = pool->allocation;
	pool_node_t* node;
	for (uint32_t i = 0; i < pool->chunk_cap; ++i)
	{
		node = alloc + i * size;
		node->next = (i + 1) * size; // alloc_size treated as NULL
	}
	// one additional node that just points back to itself.  Used in pool_free for branchless memes.
	node = alloc + size * pool->chunk_cap;
	node->next = (uint8_t*)node - alloc;
	pool->head = 0;
}
