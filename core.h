#ifndef core_h
#define core_h

#include <stdint.h>
#include "decl.h"

BEGIN_DECL

typedef struct {
	uint8_t a, b, c, d, e, h, l;
	uint8_t sf : 1, zf : 1, af : 1, pf : 1, cf : 1;
	struct {
		uint8_t h, l;
	} pc, sp;
	uint8_t cycles;
	uint8_t *memory;
	uint8_t ei : 1, hlt : 1;
	uint8_t (*port_in)(uint8_t);
	void (*port_out)(uint8_t, uint8_t);
} i8080;

void i8080_rst(i8080 *, uint8_t);
void i8080_step(i8080 *);

END_DECL

#endif // core_h
