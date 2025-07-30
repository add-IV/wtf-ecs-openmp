#include "ecs.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include "allocators/arena.h"
#include "allocators/pool.h"

static struct
{
	int32_t indices[ENTITY_CAP];
	int32_t size;
} update_list = {0};


static pool_t* component_pools = NULL;
/* pool_t* entity_pool = NULL; */

static arena_t res_arena = {0};
static arena_t arg_arena = {0};

__attribute__((constructor))
static void init_ecs(void)
{
	component_pools = malloc(NUM_COMPONENTS * sizeof *component_pools);
#define X(ENUM, TYPE) \
	pool_init(component_pools + ENUM, sizeof(TYPE##_t), ENTITY_CAP);
	COMPONENTS
	#undef X
}

inline static void mark_update(int32_t index) {
	update_list.indices[update_list.size++] = index;
}

// NOTE: just set ecs_table size to zero. No need to do anything else.
void ecs_free_all(void)
{
	// free all the pools
	for (int32_t i = 0; i < NUM_COMPONENTS; ++i)
	{
		pool_free_all(component_pools + i);
	}
}

int32_t ecs_activate_entity(ecs_table_t* ecs_table)
{
	if (ecs_table->size < ENTITY_CAP)
	{
		const int32_t i = ecs_table->size++;
		// NOTE: don't bother setting all the components to zero.  Just set the bitmask to zero :)
		ecs_table->bitmasks[i] = 0;
		return i;
	}
	else
	{
		fprintf(stderr, "TOO MANY ENTITIES!\n");
		assert(0);
		return -1;
	}
}

void ecs_add_component(ecs_table_t* ecs_table, const int32_t id, const component_t component)
{
	ecs_table->components[NUM_COMPONENTS * id + component] = pool_calloc(component_pools + component);
	ecs_table->bitmasks[id] |= 1 << component;
}


#define X(ENUM, NAME) void ecs_set_##NAME(ecs_table_t* ecs_table, const int32_t id, const NAME##_t* value) \
{ \
	memcpy(ecs_table->components[NUM_COMPONENTS * id + ENUM], value, sizeof(NAME##_t)); \
}
COMPONENTS
#undef X

#define FREE_ENTITY NUM_COMPONENTS
int32_t single_thread_tick(ecs_table_t* ecs_table, const float delta)
{
	/* entity_t* entities = ecs_table->entities; */
	uint8_t* bitmasks = ecs_table->bitmasks;
	void** components = ecs_table->components;
	uint8_t mask = 1 << FREE_ENTITY;
	update_list.size = 0;
	for (int32_t i = 0; i < ecs_table->size; ++i)
	{
		if (bitmasks[i] & mask)
		{
			mark_update(i);
		}
	}
	if (update_list.size > 0)
	{
		const int32_t n = update_list.size;
		for (int32_t i = 0; i < NUM_COMPONENTS; ++i)
		{
			for (int32_t j = 0; j < n; ++j)
			{
				const int32_t k = update_list.indices[j] * NUM_COMPONENTS;
				pool_free(component_pools + i, components[k + i]);
			}
		}
		const size_t sizeof_components = NUM_COMPONENTS * sizeof(void*);
		for (int32_t i = n - 1;  i >= 0; --i)
		{
			const int32_t j = update_list.indices[i];
			const int32_t m = --ecs_table->size;
			if (j < m)
			{
				bitmasks[j] = bitmasks[m];
				/* for (int32_t k = 0; k < NUM_COMPONENTS; ++k) */
				/* { */
				/* 	components[j * NUM_COMPONENTS + k] = components[m * NUM_COMPONENTS + k]; */
				/* } */
				memcpy(components + j * NUM_COMPONENTS, components + m * NUM_COMPONENTS, sizeof_components);
			}
		}
	}
	mask = (1 << POSITION) | (1 << VELOCITY);
	update_list.size = 0;
	for (int32_t i = 0; i < ecs_table->size; ++i)
	{
		if ((bitmasks[i] & mask) == mask)
		{
			mark_update(i);
		}
	}
	if (update_list.size > 0)
	{
		const int32_t n = update_list.size;
		position_t* positions = arena_scratch(&res_arena, n * sizeof *positions);
		velocity_t* velocities = arena_scratch(&arg_arena, n * sizeof *velocities);
		// populate
		for (int32_t i = 0; i < n; ++i)
		{
			const uint32_t j = update_list.indices[i];
			const uint32_t k = j * NUM_COMPONENTS;
			memcpy(positions + i, components[k + POSITION], sizeof(position_t));
			memcpy(velocities + i, components[k + VELOCITY], sizeof(velocity_t));
		}
		// update
		for (int32_t i = 0; i < n; ++i)
		{
			const velocity_t v = velocities[i];
			positions[i].x += delta * v.x;
			positions[i].y += delta * v.y;
			positions[i].z += delta * v.z;
			/* printf("p%i: (%f,%f,%f)\n", i, positions[i].x, positions[i].y, positions[i].z); */
		}
		//copy to components
		for (int32_t i = 0; i < n; ++i)
		{
			const uint32_t j = update_list.indices[i];
			const uint32_t k = j * NUM_COMPONENTS;
			memcpy(components[k + POSITION], positions + i, sizeof(position_t));
		}
	}
	mask = 1 << LIFETIME;
	update_list.size = 0;
	for (int32_t i = 0; i < ecs_table->size; ++i)
	{
		if (bitmasks[i] & mask)
		{
			mark_update(i);
		}
	}
	if (update_list.size > 0)
	{
		const int32_t n = update_list.size;
		lifetime_t* lifetimes = arena_scratch(&arg_arena, n * sizeof *lifetimes);
		// populate
		for (int32_t i = 0; i < n; ++i)
		{
			const uint32_t j = update_list.indices[i];
			const uint32_t k = j * NUM_COMPONENTS;
			memcpy(lifetimes + i, components[k + LIFETIME], sizeof(lifetime_t));
		}
		// update
		for (int32_t i = 0; i < n; ++i)
		{
			lifetimes[i].value -= delta;
		}
		// copy to components & bitmask memels
		for (int32_t i = 0; i < n; ++i)
		{
			const uint32_t j = update_list.indices[i];
			const uint32_t k = j * NUM_COMPONENTS;
			memcpy(components[k + LIFETIME], lifetimes + i, sizeof(lifetime_t));
			/* printf("time: %f, bits: %x\n", lifetimes[i].value, lifetimes[i].bits); */
			/* printf("bitshift0: %x\n", lifetimes[i].bits >> 31); */
			/* printf("bitshift1: %x\n", (lifetimes[i].bits >> 31) << FREE_ENTITY); */
			bitmasks[j] |= (lifetimes[i].bits >> 31) << FREE_ENTITY;
			/* printf("res: %x\n", bitmasks[j] & (1 << FREE_ENTITY)); */
		}
	}
	return ecs_table->size;
}
