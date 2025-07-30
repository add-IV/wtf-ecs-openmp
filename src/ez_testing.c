#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <time.h>
#include "ecs.h"

#define N 100000

static int32_t num_total = ENTITY_CAP;
/* static int32_t num_total = 1000; */
static int32_t num_active = 0;
static const float delta = 0.001f; // 100hz

void spawn_projectile(ecs_table_t* ecs_table, const position_t* position, const velocity_t* velocity, const float lifetime)
{
	const int32_t id = ecs_activate_entity(ecs_table);
	ecs_add_component(ecs_table, id, POSITION);
	ecs_add_component(ecs_table, id, VELOCITY);
	ecs_add_component(ecs_table, id, LIFETIME);
	ecs_set_position(ecs_table, id, position);
	ecs_set_velocity(ecs_table, id, velocity);
	ecs_set_lifetime(ecs_table, id, &lifetime);
}


int main(int argc, char** argv)
{
	// ecs table setup
	ecs_table_t ecs_table = {0};
	const int32_t size = NUM_COMPONENTS * sizeof(void**) + sizeof(int8_t);
	ecs_table.components = malloc(ENTITY_CAP * size);
	ecs_table.bitmasks = ecs_table.components + NUM_COMPONENTS * ENTITY_CAP;
	// spawn config
	const position_t position0 = {0};
	const velocity_t velocity0 =
	{
		.x = 10.0f
	};
	const float lifetime0 = 3.0f;
	const float spawn_freq = lifetime0 / (float)num_total;
	printf("freq: %f\n", spawn_freq);
	float sum = 0.0f;
	// clock setup
	#ifdef _WIN32
	LARGE_INTEGER clock_freq;
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceFrequency(&clock_freq);
	QueryPerformanceCounter(&start);
	#else
	struct tms t;
	unsigned long clock_freq;
	uintmax_t start;
	uintmax_t end;
	clock_freq = sysconf(_SC_CLK_TCK);
	start = times(&t);
	#endif
	// singlethread
	for (int32_t i = 0; i < N; ++i)
	{
		sum += delta;
		for (; sum > spawn_freq && num_active < num_total; sum -= spawn_freq)
		{
			fflush(stdout);
			spawn_projectile(&ecs_table, &position0, &velocity0, lifetime0);
			++num_active;
		}
		num_active = single_thread_tick(&ecs_table, delta);
	}
	#ifdef _WIN32
	QueryPerformanceCounter(&end);
	printf("single threaded: %fs\n", (double)(end.QuadPart - start.QuadPart)  / clock_freq.QuadPart);
	#else
	end = times(&t);
	printf("single threaded: %fs\n", (double)(end - start) / clock_freq);
	#endif
	void** components = ecs_table.components;
	printf("ecs_table.size: %d\n", ecs_table.size);
	/* for (int32_t i = 0; i < ecs_table.size; ++i) */
	/* { */
	/* 	const position_t* p = components[i * NUM_COMPONENTS + POSITION]; */
	/* 	printf("entity%d: (%f, %f, %f)\n", i, p->x, p->y, p->z); */
	/* } */
	ecs_table.size = 0;
	ecs_free_all();
	// multithread
	const int num_threads = 8; // yeah I hardcode values.  Cry about it >:^)
	printf("num_threads %d\n", num_threads);
	/* for (int32_t i = 0; i < N; ++i) */
	/* { */
	/* } */
	return 0;
}
