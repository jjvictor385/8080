#ifndef opcode_h
#define opcode_h

#include <stdint.h>
#include "decl.h"

BEGIN_DECL

typedef struct {
	char const *fmt;
	uint8_t size, cyc;
} opcode_t;

extern opcode_t const OPCODES[];

END_DECL

#endif
