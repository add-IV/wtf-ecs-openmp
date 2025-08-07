#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/times.h>
#include <unistd.h>
#endif
#include "ecs.h"

#define SINGLE
#define ALT_SINGLE
#define MULTITHREAD
#define MULTITHREAD2
#define POSIXTHREADS
#define ALT_THREAD
#define OTHER_ALT_THREAD
#define OpenMP

/* #define N 100000 */
#define N 10000

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
	float sum;
	// clock setup
	#ifdef _WIN32
	LARGE_INTEGER clock_freq;
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceFrequency(&clock_freq);
	#else
	uintmax_t start;
	uintmax_t end;
	unsigned long clock_freq = sysconf(_SC_CLK_TCK);
	#endif

	#ifdef SINGLE
	num_active = 0;
	sum = 0;
	#ifdef _WIN32
	QueryPerformanceCounter(&start);
	#else
	start = times(NULL);
	#endif
	// singlethread
	for (int32_t i = 0; i < N; ++i)
	{
		sum += delta;
		for (; sum > spawn_freq && num_active < num_total; sum -= spawn_freq)
		{
			spawn_projectile(&ecs_table, &position0, &velocity0, lifetime0);
			++num_active;
		}
		num_active = single_thread_tick(&ecs_table, delta);
	}
	#ifdef _WIN32
	QueryPerformanceCounter(&end);
	printf("singly-threaded: %fs\n", (double)(end.QuadPart - start.QuadPart)  / clock_freq.QuadPart);
	#else
	end = times(NULL);
	printf("singly-threaded: %fs\n", (double)(end - start) / clock_freq);
	#endif
	printf("ecs_table.size: %d\n", ecs_table.size);
	fflush(stdout);
	ecs_table.size = 0;
	ecs_free_all();
	#endif

	#ifdef ALT_SINGLE
	num_active = 0;
	sum = 0;
	#ifdef _WIN32
	QueryPerformanceCounter(&start);
	#else
	start = times(NULL);
	#endif
	// singlethread
	for (int32_t i = 0; i < N; ++i)
	{
		sum += delta;
		for (; sum > spawn_freq && num_active < num_total; sum -= spawn_freq)
		{
			spawn_projectile(&ecs_table, &position0, &velocity0, lifetime0);
			++num_active;
		}
		num_active = single_thread_tick_alt(&ecs_table, delta);
	}
	#ifdef _WIN32
	QueryPerformanceCounter(&end);
	printf("alt singly-threaded: %fs\n", (double)(end.QuadPart - start.QuadPart)  / clock_freq.QuadPart);
	#else
	end = times(NULL);
	printf("alt singly-threaded: %fs\n", (double)(end - start) / clock_freq);
	#endif
	printf("ecs_table.size: %d\n", ecs_table.size);
	fflush(stdout);
	ecs_table.size = 0;
	ecs_free_all();
	#endif

	const int num_threads = 8; // yeah I hardcode values.  Cry about it >:^)
	printf("threads: %d\n", num_threads);

	#ifdef MULTITHREAD
	// multithread
	num_active = 0;
	sum = 0;
	#ifdef _WIN32
	QueryPerformanceCounter(&start);
	#else
	start = times(NULL);
	#endif
	for (int32_t i = 0; i < N; ++i)
	{
		sum += delta;
		for (; sum > spawn_freq && num_active < num_total; sum -= spawn_freq)
		{
			spawn_projectile(&ecs_table, &position0, &velocity0, lifetime0);
			++num_active;
		}
		num_active = multi_thread_tick(&ecs_table, delta, num_threads);
	}
	#ifdef _WIN32
	QueryPerformanceCounter(&end);
	printf("multi-threaded1: %fs\n", (double)(end.QuadPart - start.QuadPart)  / clock_freq.QuadPart);
	#else
	end = times(NULL);
	printf("multi-threaded1: %fs\n", (double)(end - start) / clock_freq);
	#endif
	printf("ecs_table.size: %d\n", ecs_table.size);
	fflush(stdout);
	ecs_table.size = 0;
	ecs_free_all();
	#endif

	#ifdef MULTITHREAD2
	// multithread2
	num_active = 0;
	sum = 0;
	#ifdef _WIN32
	QueryPerformanceCounter(&start);
	#else
	start = times(NULL);
	#endif
	for (int32_t i = 0; i < N; ++i)
	{
		sum += delta;
		for (; sum > spawn_freq && num_active < num_total; sum -= spawn_freq)
		{
			spawn_projectile(&ecs_table, &position0, &velocity0, lifetime0);
			++num_active;
		}
		num_active = multi_thread_tick2(&ecs_table, delta, num_threads);
	}
	#ifdef _WIN32
	QueryPerformanceCounter(&end);
	printf("multi-threaded2: %fs\n", (double)(end.QuadPart - start.QuadPart)  / clock_freq.QuadPart);
	#else
	end = times(NULL);
	printf("multi-threaded2: %fs\n", (double)(end - start) / clock_freq);
	#endif
	printf("ecs_table.size: %d\n", ecs_table.size);
	fflush(stdout);
	ecs_table.size = 0;
	ecs_free_all();
	#endif

	#ifdef POSIXTHREADS
	// posix threads
	num_active = 0;
	sum = 0;
	#ifdef _WIN32
	QueryPerformanceCounter(&start);
	#else
	start = times(NULL);
	#endif
	for (int32_t i = 0; i < N; ++i)
	{
		sum += delta;
		for (; sum > spawn_freq && num_active < num_total; sum -= spawn_freq)
		{
			spawn_projectile(&ecs_table, &position0, &velocity0, lifetime0);
			++num_active;
		}
		num_active = multi_pthread_tick(&ecs_table, delta, num_threads);
	}
	#ifdef _WIN32
	QueryPerformanceCounter(&end);
	printf("POSIX multi-threaded: %fs\n", (double)(end.QuadPart - start.QuadPart)  / clock_freq.QuadPart);
	#else
	end = times(NULL);
	printf("POSIX multi-threaded: %fs\n", (double)(end - start) / clock_freq);
	#endif
	printf("ecs_table.size: %d\n", ecs_table.size);
	ecs_table.size = 0;
	ecs_free_all();
	#endif

	#ifdef ALT_THREAD
	// alt thread
	num_active = 0;
	sum = 0;
	#ifdef _WIN32
	QueryPerformanceCounter(&start);
	#else
	start = times(NULL);
	#endif
	for (int32_t i = 0; i < N; ++i)
	{
		sum += delta;
		for (; sum > spawn_freq && num_active < num_total; sum -= spawn_freq)
		{
			spawn_projectile(&ecs_table, &position0, &velocity0, lifetime0);
			++num_active;
		}
		num_active = multi_thread_tick_alt(&ecs_table, delta, num_threads);
	}
	#ifdef _WIN32
	QueryPerformanceCounter(&end);
	printf("alt multi-threaded: %fs\n", (double)(end.QuadPart - start.QuadPart)  / clock_freq.QuadPart);
	#else
	end = times(NULL);
	printf("alt multi-threaded: %fs\n", (double)(end - start) / clock_freq);
	#endif
	printf("ecs_table.size: %d\n", ecs_table.size);
	fflush(stdout);
	ecs_table.size = 0;
	ecs_free_all();
	#endif

	#ifdef OTHER_ALT_THREAD
	// alt thread
	num_active = 0;
	sum = 0;
	#ifdef _WIN32
	QueryPerformanceCounter(&start);
	#else
	start = times(NULL);
	#endif
	for (int32_t i = 0; i < N; ++i)
	{
		sum += delta;
		for (; sum > spawn_freq && num_active < num_total; sum -= spawn_freq)
		{
			spawn_projectile(&ecs_table, &position0, &velocity0, lifetime0);
			++num_active;
		}
		num_active = multi_thread_tick_other_alt(&ecs_table, delta, num_threads);
	}
	#ifdef _WIN32
	QueryPerformanceCounter(&end);
	printf("other alt multi-threaded: %fs\n", (double)(end.QuadPart - start.QuadPart)  / clock_freq.QuadPart);
	#else
	end = times(NULL);
	printf("other alt multi-threaded: %fs\n", (double)(end - start) / clock_freq);
	#endif
	printf("ecs_table.size: %d\n", ecs_table.size);
	fflush(stdout);
	ecs_table.size = 0;
	ecs_free_all();
	#endif

	#ifdef OpenMP
	// OpenMP
	num_active = 0;
	sum = 0;
	#ifdef _WIN32
	QueryPerformanceCounter(&start);
	#else
	start = times(NULL);
	#endif
	for (int32_t i = 0; i < N; ++i)
	{
		sum += delta;
		for (; sum > spawn_freq && num_active < num_total; sum -= spawn_freq)
		{
			spawn_projectile(&ecs_table, &position0, &velocity0, lifetime0);
			++num_active;
		}
            num_active = openmp_tick(&ecs_table, delta);
        }
	#ifdef _WIN32
	QueryPerformanceCounter(&end);
	printf("openmp: %fs\n", (double)(end.QuadPart - start.QuadPart)  / clock_freq.QuadPart);
	#else
	end = times(NULL);
	printf("other alt multi-threaded: %fs\n", (double)(end - start) / clock_freq);
	#endif
	printf("ecs_table.size: %d\n", ecs_table.size);
	fflush(stdout);
	ecs_table.size = 0;
	ecs_free_all();
	#endif


	return 0;
}
