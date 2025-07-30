#ifndef COMPONENTS_H
#define COMPONENTS_H

#include <stdint.h>

#define COMPONENTS								\
	X(POSITION, position)	\
	X(VELOCITY, velocity)	\
	X(LIFETIME, lifetime)

typedef struct position_t {
	float x;
	float y;
	float z;
} position_t;

typedef struct velocity_t {
	float x;
	float y;
	float z;
} velocity_t;

typedef union lifetime_t {
	float value;
	uint32_t bits;
} lifetime_t;

#endif /* End COMPONENTS_H */
