#include "ecs.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <threads.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include "allocators/arena.h"
#include "allocators/pool.h"
#include "components.h"

static struct
{
	int32_t indices[ENTITY_CAP];
	int32_t size;
} update_list = {0};


static pool_t* component_pools = NULL;
/* pool_t* entity_pool = NULL; */

static arena_t res_arena = {0};
static arena_t arg_arena = {0};

static arena_t* scratch_arenas = NULL;
static int8_t scratch_index = 0;

pthread_attr_t attr = {0};
static float tick_delta;

__attribute__((constructor))
static void init_ecs(void)
{
	component_pools = malloc(NUM_COMPONENTS * sizeof *component_pools);
#define X(ENUM, TYPE) \
	pool_init(component_pools + ENUM, sizeof(TYPE##_t), ENTITY_CAP);
	COMPONENTS
	#undef X
	scratch_arenas = calloc(32, sizeof *scratch_arenas);
	// change thread attribute scheduling
	assert(pthread_attr_init(&attr) == 0 && "failed to initialize POSIX thread attributes!");
	struct sched_param param = {0};
	assert(pthread_attr_getschedparam(&attr, &param) == 0 && "failed to retrive POSIX thread attribute schedule parameter!");
	/* assert(pthread_attr_setschedpolicy(&attr, SCHED_RR) == 0 && "failed to update thread schedule policy!"); */
	const int policy = SCHED_RR;
	assert(pthread_attr_setschedpolicy(&attr, policy) == 0 && "failed to update thread schedule policy!");
	// printf("default posix thread priority: %d\n", param.sched_priority);
	/* ++param.sched_priority; */
	param.sched_priority = sched_get_priority_max(policy);
	// printf("new priority: %d\n", param.sched_priority);
	if(pthread_attr_setschedparam(&attr, &param) == EINVAL)
	{
		fprintf(stderr, "Failed to update posix thread scheduling parameters!\n");
		fprintf(stderr, "value %d 'does not make sense for the current scheduling policy of attr.'\n", param.sched_priority);
		assert(0);
	}
}

inline static void* scratch_checkout(const int32_t size)
{
	return arena_scratch(scratch_arenas + scratch_index++, size);
}

inline static void* scratch_alloc(const int32_t index, int32_t size)
{
	return arena_scratch(scratch_arenas + index, size);
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

// WHY MEMCPY? WHY USE UPDATE_LIST???
int32_t single_thread_tick_alt(ecs_table_t* ecs_table, const float delta)
{
	uint8_t* bitmasks = ecs_table->bitmasks;
	void** components = ecs_table->components;
	if (ecs_table->size > 0)
	{
		const int32_t n = ecs_table->size;
		const size_t sizeof_components = NUM_COMPONENTS * sizeof(void*);
		const uint8_t mask = 1 << FREE_ENTITY;
		for (int32_t i = n - 1; i >= 0; --i)
		{
			if ((bitmasks[i] & mask) == 0x00)
			{
				// bp abuse lmao
				continue;
			}
			else
			{
				const int32_t k = i * NUM_COMPONENTS;
				for (int8_t j = 0; j < NUM_COMPONENTS; ++j)
				{
					pool_free(component_pools + j, components[k + j]);
				}
				const int32_t m = --ecs_table->size;
				if (i < m)
				{
					bitmasks[i] = bitmasks[m];
					memcpy(components + k, components + m * NUM_COMPONENTS, sizeof_components);
				}
			}
		}
	}
	if (ecs_table->size > 0)
	{
		const int32_t n = ecs_table->size;
		const uint8_t pos_mask = (1 << POSITION) | (1 << VELOCITY);
		const uint8_t l_mask = 1 << LIFETIME;
		for (int32_t i = 0; i < n; ++i)
		{
			if ((bitmasks[i] & pos_mask) == pos_mask)
			{
				position_t* p = components[i * NUM_COMPONENTS + POSITION];
				velocity_t v = *(velocity_t*)components[i * NUM_COMPONENTS + VELOCITY];
				p->x += delta * v.x;
				p->y += delta * v.y;
				p->z += delta * v.z;
			}
			if (bitmasks[i] & l_mask)
			{
				lifetime_t* l = components[i * NUM_COMPONENTS + LIFETIME];
				l->value -= delta;
				bitmasks[i] |= (l->bits >> 31) << FREE_ENTITY;
			}
		}
	}
	return ecs_table->size;
}

/***********************/
/* multithreading hell */
/***********************/

typedef struct span_t
{
	union
	{
		float delta;
		void** components;
		uint8_t* bitmasks;
		ecs_table_t* ecs_table;
	};
	int32_t i;
	int32_t n;
	union
	{
		int32_t scratch;
		component_t c;
	};
} span_t;

void set_spans(span_t* spans, const int32_t num_threads, const int32_t n)
{
	const int32_t div = n / num_threads;
	const int32_t mod = n % num_threads;
	spans->i = 0;
	spans->n = div + (mod > 0);
	for (uint8_t i = 1; i < num_threads - 1; ++i) {
		const int32_t k = spans[i - 1].n;
		spans[i].i = k;
		spans[i].n = k + div + (mod > i);
	}
	if (num_threads > 1)
	{
		spans[num_threads - 1].i = spans[num_threads - 2].n;
		spans[num_threads - 1].n = n;
	}
}

typedef struct free_args_t
{
	union
	{
		void** components;
		ecs_table_t* ecs_table;
	};
	component_t c;
} free_args_t;

static int free_components(void* args)
{
	const free_args_t* free_args = args;
	void** components = free_args->components;
	const component_t c = free_args->c;
	for (int32_t i = 0; i < update_list.size; ++i)
	{
		const int32_t j = update_list.indices[i];
		pool_free(component_pools + c, components[j * NUM_COMPONENTS + c]);
	}
	return 0;
}

static int populate_position_update_buffers(void* args)
{
	const span_t* span = args;
	const void** components = span->components;
	const int32_t n = span->n;
	velocity_t* velocities = arg_arena.allocation;
	position_t* positions = res_arena.allocation;
	for (int32_t i = span->i; i < n; ++i)
	{
		const int32_t j = update_list.indices[i];
		const int32_t k = NUM_COMPONENTS * j;
		memcpy(positions + i, components[k + POSITION], sizeof(position_t));
		memcpy(velocities + i, components[k + VELOCITY], sizeof(velocity_t));
	}
	return 0;
}

static int update_positions(void* args)
{
	const span_t* span = args;
	const float delta = span->delta;
	const int32_t n = span->n;
	velocity_t* velocities = arg_arena.allocation;
	position_t* positions = res_arena.allocation;
	for (int32_t i = span->i; i < n; ++i)
	{
		const velocity_t v = velocities[i];
		positions[i].x += delta * v.x;
		positions[i].y += delta * v.y;
		positions[i].z += delta * v.z;
	}
	return 0;
}

static int sync_positions(void* args)
{
	const span_t* span = args;
	const int32_t n = span->n;
	const position_t* positions = res_arena.allocation;
	void** components = span->components;
	for (int32_t i = span->i; i < n; ++i)
	{
		const int32_t j = update_list.indices[i];
		memcpy(components[j * NUM_COMPONENTS + POSITION], positions + i, sizeof(position_t));
	}
	return 0;
}

static int populate_lifetime_update_buffer(void* args)
{
	const span_t* span = args;
	const int32_t n = span->n;
	const void** components = span->components;
	lifetime_t* lifetimes = res_arena.allocation;
	for (int32_t i = span->i; i < n; ++i) {
		const int32_t j = update_list.indices[i];
		memcpy(lifetimes + i, components[j * NUM_COMPONENTS + LIFETIME], sizeof(lifetime_t));
	}
	return 0;
}


static int update_lifetimes(void* args)
{
	const span_t* span = args;
	const int32_t n = span->n;
	const float delta = span->delta;
	lifetime_t* lifetimes = res_arena.allocation;
	uint8_t* free_masks = arg_arena.allocation;
	for (int32_t i = span->i; i < n; ++i)
	{
		lifetimes[i].value -= delta;
		free_masks[i] = (lifetimes[i].bits >> 31) << FREE_ENTITY;
	}
	return 0;
}

static int sync_lifetimes(void* args)
{
	const span_t* span = args;
	const int32_t n = span->n;
	const lifetime_t* lifetimes = res_arena.allocation;
	void** components = span->components;
	for (int32_t i = span->i; i < n; ++i)
	{
		const int32_t j = update_list.indices[i];
		memcpy(components[j * NUM_COMPONENTS + LIFETIME], lifetimes + i, sizeof(lifetime_t));
	}
	return 0;
}

static int sync_free_entity_flags(void* args)
{
	const span_t* span = args;
	const int32_t n = span->n;
	const uint8_t* flags = arg_arena.allocation;
	uint8_t* bitmasks = span->bitmasks;
	for (int32_t i = span->i; i < n; ++i)
	{
		const int32_t j = update_list.indices[i];
		bitmasks[j] |= flags[i];
	}
	return 0;
}

int32_t multi_thread_tick(ecs_table_t* ecs_table, const float delta, const int32_t num_threads)
{
	thrd_t* threads = alloca(num_threads * sizeof *threads);
	int t_res;
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
		if (num_threads >= NUM_COMPONENTS)
		{
			free_args_t* args = alloca(NUM_COMPONENTS * sizeof *args);
			for (int8_t i = 0; i < NUM_COMPONENTS; ++i)
			{
				args[i].components = components;
				args[i].c = i;
				thrd_create(threads + i, free_components, args + i);
			}
			for (int8_t i = 0; i < NUM_COMPONENTS; ++i)
			{
				thrd_join(threads[i], &t_res);
			}
		}
		else
		{
			// lol. lmao even.
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
		span_t* spans = alloca(num_threads * sizeof *spans);
		arena_scratch(&arg_arena, n * sizeof(velocity_t));
		arena_scratch(&res_arena, n * sizeof(position_t));
		set_spans(spans, num_threads, n);
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].components = components;
			thrd_create(threads + i, populate_position_update_buffers, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].delta = delta;
			thrd_create(threads + i, update_positions, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].components = components;
			thrd_create(threads + i, sync_positions, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
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
		arena_scratch(&res_arena, n * sizeof(lifetime_t));
		arena_scratch(&arg_arena, n * sizeof(uint8_t));
		span_t* spans = alloca(num_threads * sizeof *spans);
		set_spans(spans, num_threads, n);
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].components = components;
			thrd_create(threads + i, populate_lifetime_update_buffer, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].delta = delta;
			thrd_create(threads + i, update_lifetimes, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].components = components;
			thrd_create(threads + i, sync_lifetimes, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].bitmasks = bitmasks;
			thrd_create(threads + i, sync_free_entity_flags, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}
	}
	return ecs_table->size;
}

/********************/
/* multi-arena hell */
/********************/

static int populate_position_update_buffers2(void* args)
{
	const span_t* span = args;
	const void** components = span->components;
	const int32_t scratch = span->scratch;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	velocity_t* restrict velocities = scratch_arenas[scratch].allocation;
	position_t* restrict positions = scratch_arenas[scratch + 1].allocation;
	for (int32_t i = 0; i < n - i0; ++i)
	{
		const int32_t j = update_list.indices[i + i0];
		const int32_t k = NUM_COMPONENTS * j;
		memcpy(positions + i, components[k + POSITION], sizeof(position_t));
		memcpy(velocities + i, components[k + VELOCITY], sizeof(velocity_t));
	}
	return 0;
}
static int update_positions2(void* args)
{
	const span_t* span = args;
	const float delta = span->delta;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const int32_t scratch = span->scratch;
	const velocity_t* restrict velocities = scratch_arenas[scratch].allocation;
	position_t* restrict positions = scratch_arenas[scratch + 1].allocation;
	for (int32_t i = 0; i < n - i0; ++i)
	{
		const velocity_t v = velocities[i];
		positions[i].x += delta * v.x;
		positions[i].y += delta * v.y;
		positions[i].z += delta * v.z;
	}
	return 0;
}

static int sync_positions2(void* args)
{
	const span_t* span = args;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const int32_t scratch = span->scratch;
	const position_t* restrict positions = scratch_arenas[scratch + 1].allocation;
	void** components = span->components;
	for (int32_t i = 0; i < n - i0; ++i)
	{
		const int32_t j = update_list.indices[i + i0];
		memcpy(components[j * NUM_COMPONENTS + POSITION], positions + i, sizeof(position_t));
	}
	return 0;
}

static int populate_lifetime_update_buffer2(void* args)
{
	const span_t* span = args;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const int32_t scratch = span->scratch;
	const void** components = span->components;
	lifetime_t* restrict lifetimes = scratch_arenas[scratch].allocation;
	for (int32_t i = 0; i < n - i0; ++i) {
		const int32_t j = update_list.indices[i + i0];
		memcpy(lifetimes + i, components[j * NUM_COMPONENTS + LIFETIME], sizeof(lifetime_t));
	}
	return 0;
}

static int update_lifetimes2(void* args)
{
	const span_t* span = args;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const float delta = span->delta;
	const int32_t scratch = span->scratch;
	lifetime_t* restrict lifetimes = scratch_arenas[scratch].allocation;
	uint8_t* restrict free_masks = scratch_arenas[scratch + 1].allocation;
	for (int32_t i = 0; i < n - i0; ++i)
	{
		lifetimes[i].value -= delta;
		free_masks[i] = (lifetimes[i].bits >> 31) << FREE_ENTITY;
	}
	return 0;
}

static int sync_lifetimes2(void* args)
{
	const span_t* span = args;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const lifetime_t* restrict lifetimes = scratch_arenas[span->scratch].allocation;
	void** components = span->components;
	for (int32_t i = 0; i < n - i0; ++i)
	{
		const int32_t j = update_list.indices[i + i0];
		memcpy(components[j * NUM_COMPONENTS + LIFETIME], lifetimes + i, sizeof(lifetime_t));
	}
	return 0;
}

static int sync_free_entity_flags2(void* args)
{
	const span_t* span = args;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const uint8_t* restrict flags = scratch_arenas[span->scratch + 1].allocation;
	uint8_t* bitmasks = span->bitmasks;
	for (int32_t i = 0; i < n - i0; ++i)
	{
		const int32_t j = update_list.indices[i + i0];
		bitmasks[j] |= flags[i];
	}
	return 0;
}

int32_t multi_thread_tick2(ecs_table_t* ecs_table, const float delta, const int32_t num_threads)
{
	thrd_t* threads = alloca(num_threads * sizeof *threads);
	int t_res;
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
		scratch_index = 0;
		if (num_threads >= NUM_COMPONENTS)
		{
			/* free_args_t* args = alloca(NUM_COMPONENTS * sizeof *args); */
			for (int8_t i = 0; i < NUM_COMPONENTS; ++i)
			{
				free_args_t* args = scratch_checkout(sizeof(free_args_t));
				args->components = components;
				args->c = i;
				thrd_create(threads + i, free_components, args);
			}
			for (int8_t i = 0; i < NUM_COMPONENTS; ++i)
			{
				thrd_join(threads[i], &t_res);
			}
		}
		else
		{
			// lol. lmao even.
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
		scratch_index = 0;
		span_t* spans = alloca(num_threads * sizeof *spans);
		set_spans(spans, num_threads, n);
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].components = components;
			spans[i].scratch = 2 * i;
			const int32_t m = spans[i].n - spans[i].i;
			scratch_checkout(m  * sizeof(velocity_t));
			scratch_checkout(m  * sizeof(position_t));
			thrd_create(threads + i, populate_position_update_buffers2, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].delta = delta;
			thrd_create(threads + i, update_positions2, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].components = components;
			thrd_create(threads + i, sync_positions2, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
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
		scratch_index = 0;
		span_t* spans = alloca(num_threads * sizeof *spans);
		set_spans(spans, num_threads, n);
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].components = components;
			spans[i].scratch = 2 * i;
			const int32_t m = spans[i].n - spans[i].i;
			scratch_checkout(m * sizeof(lifetime_t));
			scratch_checkout(m * sizeof(uint8_t));
			thrd_create(threads + i, populate_lifetime_update_buffer2, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].delta = delta;
			thrd_create(threads + i, update_lifetimes2, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].components = components;
			thrd_create(threads + i, sync_lifetimes2, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].bitmasks = bitmasks;
			thrd_create(threads + i, sync_free_entity_flags2, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}
	}
	return ecs_table->size;
}


/*****************/
/* POSIX THREADS */
/*****************/

static void* free_componentsp(void* args)
{
	const free_args_t* free_args = args;
	void** components = free_args->components;
	const component_t c = free_args->c;
	for (int32_t i = 0; i < update_list.size; ++i)
	{
		const int32_t j = update_list.indices[i];
		pool_free(component_pools + c, components[j * NUM_COMPONENTS + c]);
	}
	return NULL;
}

static void* populate_position_update_buffersp(void* args)
{
	const span_t* span = args;
	const void** components = span->components;
	const int32_t scratch = span->scratch;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	velocity_t* restrict velocities = scratch_arenas[scratch].allocation;
	position_t* restrict positions = scratch_arenas[scratch + 1].allocation;
	for (int32_t i = 0; i < n - i0; ++i)
	{
		const int32_t j = update_list.indices[i + i0];
		const int32_t k = NUM_COMPONENTS * j;
		memcpy(positions + i, components[k + POSITION], sizeof(position_t));
		memcpy(velocities + i, components[k + VELOCITY], sizeof(velocity_t));
	}
	return NULL;
}
static void* update_positionsp(void* args)
{
	const span_t* span = args;
	const float delta = span->delta;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const int32_t scratch = span->scratch;
	const velocity_t* restrict velocities = scratch_arenas[scratch].allocation;
	position_t* restrict positions = scratch_arenas[scratch + 1].allocation;
	for (int32_t i = 0; i < n - i0; ++i)
	{
		const velocity_t v = velocities[i];
		positions[i].x += delta * v.x;
		positions[i].y += delta * v.y;
		positions[i].z += delta * v.z;
	}
	return NULL;
}

static void* sync_positionsp(void* args)
{
	const span_t* span = args;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const int32_t scratch = span->scratch;
	const position_t* restrict positions = scratch_arenas[scratch + 1].allocation;
	void** components = span->components;
	for (int32_t i = 0; i < n - i0; ++i)
	{
		const int32_t j = update_list.indices[i + i0];
		memcpy(components[j * NUM_COMPONENTS + POSITION], positions + i, sizeof(position_t));
	}
	return NULL;
}

static void* populate_lifetime_update_bufferp(void* args)
{
	const span_t* span = args;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const int32_t scratch = span->scratch;
	const void** components = span->components;
	lifetime_t* restrict lifetimes = scratch_arenas[scratch].allocation;
	for (int32_t i = 0; i < n - i0; ++i) {
		const int32_t j = update_list.indices[i + i0];
		memcpy(lifetimes + i, components[j * NUM_COMPONENTS + LIFETIME], sizeof(lifetime_t));
	}
	return NULL;
}

static void* update_lifetimesp(void* args)
{
	const span_t* span = args;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const float delta = span->delta;
	const int32_t scratch = span->scratch;
	lifetime_t* restrict lifetimes = scratch_arenas[scratch].allocation;
	uint8_t* restrict free_masks = scratch_arenas[scratch + 1].allocation;
	for (int32_t i = 0; i < n - i0; ++i)
	{
		lifetimes[i].value -= delta;
		free_masks[i] = (lifetimes[i].bits >> 31) << FREE_ENTITY;
	}
	return NULL;
}

static void* sync_lifetimesp(void* args)
{
	const span_t* span = args;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const lifetime_t* restrict lifetimes = scratch_arenas[span->scratch].allocation;
	void** components = span->components;
	for (int32_t i = 0; i < n - i0; ++i)
	{
		const int32_t j = update_list.indices[i + i0];
		memcpy(components[j * NUM_COMPONENTS + LIFETIME], lifetimes + i, sizeof(lifetime_t));
	}
	return NULL;
}

static void* sync_free_entity_flagsp(void* args)
{
	const span_t* span = args;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const uint8_t* restrict flags = scratch_arenas[span->scratch + 1].allocation;
	uint8_t* bitmasks = span->bitmasks;
	for (int32_t i = 0; i < n - i0; ++i)
	{
		const int32_t j = update_list.indices[i + i0];
		bitmasks[j] |= flags[i];
	}
	return NULL;
}

int32_t multi_pthread_tick(ecs_table_t* ecs_table, const float delta, const int32_t num_threads)
{
	/* thrd_t* threads = alloca(num_threads * sizeof *threads); */
	/* int t_res; */
	pthread_t* threads = alloca(num_threads * sizeof *threads);
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
		scratch_index = 0;
		if (num_threads >= NUM_COMPONENTS)
		{
			/* free_args_t* args = alloca(NUM_COMPONENTS * sizeof *args); */
			for (int8_t i = 0; i < NUM_COMPONENTS; ++i)
			{
				free_args_t* args = scratch_checkout(sizeof(free_args_t));
				args->components = components;
				args->c = i;
				pthread_create(threads + i, &attr, free_componentsp, args);
			}
			for (int8_t i = 0; i < NUM_COMPONENTS; ++i)
			{
				pthread_join(threads[i], NULL);
			}
		}
		else
		{
			// lol. lmao even.
		}
		const size_t sizeof_components = NUM_COMPONENTS * sizeof(void*);
		for (int32_t i = n - 1;  i >= 0; --i)
		{
			const int32_t j = update_list.indices[i];
			const int32_t m = --ecs_table->size;
			if (j < m)
			{
				bitmasks[j] = bitmasks[m];
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
		scratch_index = 0;
		span_t* spans = alloca(num_threads * sizeof *spans);
		set_spans(spans, num_threads, n);
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].components = components;
			spans[i].scratch = 2 * i;
			const int32_t m = spans[i].n - spans[i].i;
			scratch_checkout(m  * sizeof(velocity_t));
			scratch_checkout(m  * sizeof(position_t));
			pthread_create(threads + i, &attr, populate_position_update_buffersp, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			pthread_join(threads[i], NULL);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].delta = delta;
			pthread_create(threads + i, &attr, update_positionsp, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			pthread_join(threads[i], NULL);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].components = components;
			pthread_create(threads + i, &attr, sync_positionsp, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			pthread_join(threads[i], NULL);
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
		scratch_index = 0;
		span_t* spans = alloca(num_threads * sizeof *spans);
		set_spans(spans, num_threads, n);
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].components = components;
			spans[i].scratch = 2 * i;
			const int32_t m = spans[i].n - spans[i].i;
			scratch_checkout(m * sizeof(lifetime_t));
			scratch_checkout(m * sizeof(uint8_t));
			pthread_create(threads + i, &attr, populate_lifetime_update_bufferp, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			pthread_join(threads[i], NULL);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].delta = delta;
			pthread_create(threads + i, &attr, update_lifetimesp, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			pthread_join(threads[i], NULL);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].components = components;
			pthread_create(threads + i, &attr, sync_lifetimesp, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			pthread_join(threads[i], NULL);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].bitmasks = bitmasks;
			pthread_create(threads + i, &attr, sync_free_entity_flagsp, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			pthread_join(threads[i], NULL);
		}
	}
	return ecs_table->size;
}


/***********************/
/* alt multi-threading */
/***********************/

static int thicc_funcc(void* args)
{
	const span_t* span = args;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const int32_t scratch_offset = span->scratch;
	const ecs_table_t* ecs_table = span->ecs_table;
	void** components = ecs_table->components;
	uint8_t* bitmasks = ecs_table->bitmasks;
	const int32_t num = ecs_table->size;
	uint8_t mask = (1 << POSITION) | (1 << VELOCITY);
	int32_t swap = 0;
	position_t* position = scratch_alloc(scratch_offset, num * sizeof(position_t));
	velocity_t* velocity = scratch_alloc(scratch_offset + 1, num * sizeof(velocity_t));
	/* for (int32_t i = i0; i < num; ++i) // THIS IS THE PROBLEM!!! */
	for (int32_t i = i0; i < n; ++i)
	{
		if ((bitmasks[i] & mask) == mask)
		{
			memcpy(position + swap, components[i * NUM_COMPONENTS + POSITION], sizeof(position_t));
			memcpy(velocity + swap, components[i * NUM_COMPONENTS + VELOCITY], sizeof(velocity_t));
			++swap;
		}
	}
	for (int32_t i = 0; i < swap; ++i)
	{
		const velocity_t v = velocity[i];
		position[i].x += tick_delta * v.x;
		position[i].y += tick_delta * v.y;
		position[i].z += tick_delta * v.z;
	}
	swap = 0;
	for (int32_t i = i0; i < n; ++i)
	{
		if ((bitmasks[i] & mask) == mask)
		{
			memcpy(components[i * NUM_COMPONENTS + POSITION], position + swap, sizeof(position_t));
			memcpy(components[i * NUM_COMPONENTS + VELOCITY], velocity + swap, sizeof(velocity_t));
			++swap;
		}
	}
	mask = 1 << LIFETIME;
	swap = 0;
	lifetime_t* lifetime = scratch_alloc(scratch_offset, num * sizeof(lifetime_t));
	for (int32_t i = i0; i < n; ++i)
	{
		if (bitmasks[i] & mask)
		{
			memcpy(lifetime + swap, components[i * NUM_COMPONENTS + LIFETIME], sizeof(lifetime_t));
			++swap;
		}
	}
	for (int32_t i = 0; i < swap; ++i)
	{
		lifetime[i].value -= tick_delta;
	}
	swap = 0;
	for (int32_t i = i0; i < n; ++i)
	{
		if (bitmasks[i] & mask)
		{
			memcpy(components[i * NUM_COMPONENTS + LIFETIME], lifetime + swap, sizeof(lifetime_t));
			bitmasks[i] |= (lifetime[swap].bits >> 31) << FREE_ENTITY;
			++swap;
		}
	}
	return 0;
}

int32_t multi_thread_tick_alt(ecs_table_t* ecs_table, const float delta, const int32_t num_threads)
{
	thrd_t* threads = alloca(num_threads * sizeof *threads);
	int t_res;
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
		if (num_threads >= NUM_COMPONENTS)
		{
			free_args_t* args = alloca(NUM_COMPONENTS * sizeof *args);
			for (int8_t i = 0; i < NUM_COMPONENTS; ++i)
			{
				args[i].components = components;
				args[i].c = i;
				thrd_create(threads + i, free_components, args + i);
			}
			for (int8_t i = 0; i < NUM_COMPONENTS; ++i)
			{
				thrd_join(threads[i], &t_res);
			}
		}
		else
		{
			// lol. lmao even.
		}
		const size_t sizeof_components = NUM_COMPONENTS * sizeof(void*);
		for (int32_t i = n - 1;  i >= 0; --i)
		{
			const int32_t j = update_list.indices[i];
			const int32_t m = --ecs_table->size;
			if (j < m)
			{
				bitmasks[j] = bitmasks[m];
				memcpy(components + j * NUM_COMPONENTS, components + m * NUM_COMPONENTS, sizeof_components);
			}
		}
	}
	if (ecs_table->size > 0)
	{
		tick_delta = delta;
		const int32_t n = ecs_table->size;
		span_t* spans = alloca(num_threads * sizeof *spans);
		set_spans(spans, num_threads, ecs_table->size);
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].ecs_table = ecs_table;
			spans[i].scratch = 2 * i;
			thrd_create(threads + i, thicc_funcc, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}

	}
	return ecs_table->size;
}


/************/
/* ALT HELL */
/************/

static int the_funk(void* args)
{
	const span_t* span = args;
	const int32_t i0 = span->i;
	const int32_t n = span->n;
	const ecs_table_t* ecs_table = span->ecs_table;
	void** components = ecs_table->components;
	uint8_t* bitmasks = ecs_table->bitmasks;
	const int32_t num = ecs_table->size;
	const uint8_t pos_mask = (1 << POSITION) | (1 << VELOCITY);
	const uint8_t life_mask = (1 << LIFETIME);
	for (int32_t i = i0; i < n; ++i)
	{
		if ((bitmasks[i] & pos_mask) == pos_mask)
		{
			position_t* p = components[i * NUM_COMPONENTS + POSITION];
			const velocity_t v = *(velocity_t*)components[i * NUM_COMPONENTS + VELOCITY];
			p->x += tick_delta * v.x;
			p->y += tick_delta * v.y;
			p->z += tick_delta * v.z;
		}
		if (bitmasks[i] & life_mask)
		{
			lifetime_t* l = components[i * NUM_COMPONENTS + LIFETIME];
			l->value -= tick_delta;
			bitmasks[i] |= (l->bits >> 31) << FREE_ENTITY;
		}
	}
	return 0;
}


int32_t multi_thread_tick_other_alt(ecs_table_t* ecs_table, const float delta, const int32_t num_threads)
{
	thrd_t* threads = alloca(num_threads * sizeof *threads);
	int t_res;
	uint8_t* bitmasks = ecs_table->bitmasks;
	void** components = ecs_table->components;
	// yeah this part is singly-threaded idgaf
	if (ecs_table->size > 0)
	{
		const int32_t n = ecs_table->size;
		const size_t sizeof_components = NUM_COMPONENTS * sizeof(void*);
		const uint8_t mask = 1 << FREE_ENTITY;
		for (int32_t i = n - 1; i >= 0; --i)
		{
			if ((bitmasks[i] & mask) == 0x00)
			{
				// bp abuse lmao
				continue;
			}
			else
			{
				const int32_t k = i * NUM_COMPONENTS;
				for (int8_t j = 0; j < NUM_COMPONENTS; ++j)
				{
					pool_free(component_pools + j, components[k + j]);
				}
				const int32_t m = --ecs_table->size;
				if (i < m)
				{
					bitmasks[i] = bitmasks[m];
					memcpy(components + k, components + m * NUM_COMPONENTS, sizeof_components);
				}
			}
		}
	}
	if (ecs_table->size > 0)
	{
		tick_delta = delta;
		const int32_t n = ecs_table->size;
		span_t* spans = alloca(num_threads * sizeof *spans);
		set_spans(spans, num_threads, n);
		for (int8_t i = 0; i < num_threads; ++i)
		{
			spans[i].ecs_table = ecs_table;
			thrd_create(threads + i, the_funk, spans + i);
		}
		for (int8_t i = 0; i < num_threads; ++i)
		{
			thrd_join(threads[i], &t_res);
		}
	}
	return ecs_table->size;
}

/***********************/
/* Just use OpenMP lol */
/***********************/

int32_t openmp_tick(ecs_table_t *ecs_table, const float delta) {
  uint8_t *bitmasks = ecs_table->bitmasks;
  void **components = ecs_table->components;
  if (ecs_table->size > 0) {
    const int32_t n = ecs_table->size;
    const size_t sizeof_components = NUM_COMPONENTS * sizeof(void *);
    const uint8_t mask = 1 << FREE_ENTITY;
    for (int32_t i = n - 1; i >= 0; --i) {
      if ((bitmasks[i] & mask) == 0x00) {
        // bp abuse lmao
        continue;
      } else {
        const int32_t k = i * NUM_COMPONENTS;
        for (int8_t j = 0; j < NUM_COMPONENTS; ++j) {
          pool_free(component_pools + j, components[k + j]);
        }
        const int32_t m = --ecs_table->size;
        if (i < m) {
          bitmasks[i] = bitmasks[m];
          memcpy(components + k, components + m * NUM_COMPONENTS,
                 sizeof_components);
        }
      }
    }
  }
  if (ecs_table->size > 0) {
    const int32_t n = ecs_table->size;
    const uint8_t pos_mask = (1 << POSITION) | (1 << VELOCITY);
    const uint8_t l_mask = 1 << LIFETIME;
    for (int32_t i = 0; i < n; ++i) {
      if ((bitmasks[i] & pos_mask) == pos_mask) {
        position_t *p = components[i * NUM_COMPONENTS + POSITION];
        velocity_t v = *(velocity_t *)components[i * NUM_COMPONENTS + VELOCITY];
        p->x += delta * v.x;
        p->y += delta * v.y;
        p->z += delta * v.z;
      }
      if (bitmasks[i] & l_mask) {
        lifetime_t *l = components[i * NUM_COMPONENTS + LIFETIME];
        l->value -= delta;
        bitmasks[i] |= (l->bits >> 31) << FREE_ENTITY;
      }
    }
  }
  return ecs_table->size;
}
