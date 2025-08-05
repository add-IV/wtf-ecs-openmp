#ifndef ECS_H
#define ECS_H

#include <stdint.h>
#include "components.h"

// #define ENTITY_CAP 1048456
#define ENTITY_CAP 65536
/* #define ENTITY_CAP 1024 */

    typedef enum __attribute__((packed)) component_t {
#define X(A,...) A,
      COMPONENTS
#undef X
          NUM_COMPONENTS
    } component_t;

/* typedef struct entity_t entity_t; */

typedef struct ecs_table_t
{
	void** components;
	uint8_t* bitmasks;
	int32_t size;
} ecs_table_t;

void ecs_free_all(void);

int32_t ecs_activate_entity(ecs_table_t* ecs_table);

void ecs_add_component(ecs_table_t* ecs_table, const int32_t id, const component_t component);

#define X(_, NAME) void ecs_set_##NAME(ecs_table_t* ecs_table, const int32_t id, const NAME##_t* value);
COMPONENTS
#undef X

int32_t single_thread_tick(ecs_table_t* ecs_table, const float delta);

int32_t single_thread_tick_alt(ecs_table_t* ecs_table, const float delta);

int32_t multi_thread_tick(ecs_table_t* ecs_table, const float delta, const int32_t num_threads);

int32_t multi_thread_tick2(ecs_table_t* ecs_table, const float delta, const int32_t num_threads);

int32_t multi_pthread_tick(ecs_table_t* ecs_table, const float delta, const int32_t num_threads);

int32_t multi_thread_tick_alt(ecs_table_t* ecs_table, const float delta, const int32_t num_threads);

int32_t multi_thread_tick_other_alt(ecs_table_t* ecs_table, const float delta, const int32_t num_threads);

#endif /* End ECS_H */
