#include <stdio.h>
#include <assert.h>
#include "opcode.h"
#include "core.h"

static uint8_t *i8080_rp(i8080 *state, uint8_t rp) {
	assert(rp <= 3);
	return (uint8_t *[]) {
		&state->b,
		&state->d,
		&state->h,
		&state->sp.h
	}[rp];
}

#define R16(h, l) ((h) << 8 | (l))

static uint8_t *i8080_reg(i8080 *state, uint8_t reg) {
	assert(reg <= 7);
	return (uint8_t *[]) {
		&state->b,
		&state->c,
		&state->d,
		&state->e,
		&state->h,
		&state->l,
		&state->memory[R16(state->h, state->l)],
		&state->a
	}[reg];
}

static inline void i8080_zsp(i8080 *state, uint8_t v) {
	state->zf = !v;
	state->sf = v >> 7;
	state->pf = !__builtin_parity(v);
}

static inline void i8080_inx(uint8_t *rp, uint16_t v) {
	uint16_t value = R16(rp[0], rp[1]) + v;

	rp[0] = value >> 8;
	rp[1] = value & 0xff;
}

#define HALF_ADD(x, y) ((x ^ (x + y) ^ y) >> 4 & 1)
#define HALF_SUB(x, y) (~(x ^ (x - y) ^ y) >> 4 & 1)

static inline void i8080_inr(i8080 *state, uint8_t *reg, uint8_t v) {
	state->af = HALF_ADD(*reg, v);
	i8080_zsp(state, *reg += v);
}

static inline void i8080_dcr(i8080 *state, uint8_t *reg, uint8_t v) {
	state->af = HALF_SUB(*reg, v);
	i8080_zsp(state, *reg -= v);
}

static inline void i8080_add(i8080 *state, uint8_t reg) {
	state->cf = state->a + reg > 0xff;
	i8080_inr(state, &state->a, reg);
}

static inline void i8080_sub(i8080 *state, uint8_t reg) {
	state->cf = state->a - reg < 0;
	i8080_dcr(state, &state->a, reg);
}

static inline void i8080_rot(i8080 *state, uint8_t c) {
	uint8_t tmp;

	switch (c) {
	case 0:
		// RLC
		state->cf = state->a >> 7;
		state->a <<= 1;
		state->a |= state->cf;
		break;
	case 1:
		// RRC
		state->cf = state->a & 1;
		state->a >>= 1;
		state->a |= state->cf << 7;
		break;
	case 2:
		// RAL
		tmp = state->cf;
		state->cf = state->a >> 7;
		state->a <<= 1;
		state->a |= tmp;
		break;
	case 3:
		// RAR
		tmp = state->cf;
		state->cf = state->a & 1;
		state->a >>= 1;
		state->a |= tmp << 7;
		break;
	default:
		assert(0);
	}
}

static inline void i8080_dad(i8080 *state, uint8_t *rp) {
	uint16_t hl = R16(state->h, state->l), rg = R16(rp[0], rp[1]);

	state->cf = hl + rg > 0xffff;
	hl += rg;
	state->h = hl >> 8;
	state->l = hl & 0xff;
}

// direct addressing
static inline void i8080_daddr(i8080 *state, uint16_t addr, uint8_t op) {
	switch (op) {
	case 0:
		// SHLD
		state->memory[addr + 1] = state->h;
		state->memory[addr] = state->l;
		break;
	case 1:
		// LHLD
		state->h = state->memory[addr + 1];
		state->l = state->memory[addr];
		break;
	case 2:
		// STA
		state->memory[addr] = state->a;
		break;
	case 3:
		// LDA
		state->a = state->memory[addr];
		break;
	default:
		assert(0);
	}
}

static inline void i8080_cmp(i8080 *state, uint8_t byte) {
	state->cf = state->a - byte < 0;
	state->af = HALF_SUB(state->a, byte);
	i8080_zsp(state, state->a - byte);
}

static inline void i8080_logical(i8080 *state, uint8_t op, uint8_t reg) {
	state->af = 0;
	switch (op) {
	case 0x4:
		// ANA
		state->af = (state->a | reg) >> 3 & 1;
		state->a &= reg;
		break;
	case 0x5:
		// XRA
		state->a ^= reg;
		break;
	case 0x6:
		// ORA
		state->a |= reg;
		break;
	default:
		assert(0);
	}
	// either ANA, XRA and ORA reset carry
	state->cf = 0;
	i8080_zsp(state, state->a);
}

static inline _Bool i8080_branch_cond(i8080 *state, uint8_t opcode) {
	// CALL, JMP and RET
	if (opcode == 0xcd || opcode == 0xc3 || opcode == 0xc9)
		return 1;

	switch (opcode >> 3 & 7) {
	case 0x0:
		return !state->zf;
	case 0x1:
		return state->zf;
	case 0x2:
		return !state->cf;
	case 0x3:
		return state->cf;
	case 0x4:
		return !state->pf;
	case 0x5:
		return state->pf;
	case 0x6:
		return !state->sf;
	case 0x7:
		return state->sf;
	}

	// not reached
	return 0;
}

// used in stack operations
#define SET_RP(reg, value) (reg.h = value >> 8 & 0xff, reg.l = value & 0xff)
#define GET_RP(reg) (reg.h << 8 | reg.l)

static inline void i8080_call(i8080 *state, uint8_t opcode, uint16_t addr) {
	uint16_t sp = GET_RP(state->sp);

	if (!i8080_branch_cond(state, opcode))
		return;

	if (opcode != 0xcd)
		state->cycles += 6;

	state->memory[--sp] = state->pc.h;
	state->memory[--sp] = state->pc.l;

	SET_RP(state->sp, sp);
	SET_RP(state->pc, addr);
}

static inline void i8080_ret(i8080 *state, uint8_t opcode) {
	uint16_t sp = GET_RP(state->sp);

	if (!i8080_branch_cond(state, opcode))
		return;

	if (opcode != 0xc9)
		state->cycles += 6;

	state->pc.l = state->memory[sp++];
	state->pc.h = state->memory[sp++];

	SET_RP(state->sp, sp);
}

static inline void i8080_push(i8080 *state, uint8_t rp) {
	assert(rp <= 3);

	uint16_t sp = GET_RP(state->sp);
	uint8_t h, l;

	if (rp == 3) {
		h = state->a;
		l = (
			state->sf << 7 |
			state->zf << 6 |
			state->af << 4 |
			state->pf << 2 |
			state->cf << 0 |
			0x2
		);
	} else {
		uint8_t *tmp = i8080_rp(state, rp);
		h = *tmp++;
		l = *tmp;
	}

	state->memory[--sp] = h;
	state->memory[--sp] = l;

	SET_RP(state->sp, sp);
}

static inline void i8080_imm(i8080 *state, uint8_t op, uint8_t byte) {
	switch (op) {
	case 0x0:
		i8080_add(state, byte);
		break;
	case 0x1:
		i8080_add(state, byte + state->cf);
		break;
	case 0x2:
		i8080_sub(state, byte);
		break;
	case 0x3:
		i8080_sub(state, byte + state->cf);
		break;
	case 0x4:
	case 0x5:
	case 0x6:
		i8080_logical(state, op, byte);
		break;
	case 0x7:
		i8080_cmp(state, byte);
		break;
	default:
		assert(0);
	}
}

static inline void i8080_pop(i8080 *state, uint8_t rp) {
	assert(rp <= 3);

	uint16_t sp = GET_RP(state->sp);
	uint8_t tmp, *h, *l = &tmp;

	if (rp == 3) {
		h = &state->a;
	} else {
		h = i8080_rp(state, rp);
		l = h + 1;
	}

	*l = state->memory[sp++];
	*h = state->memory[sp++];

	if (rp == 3) {
		state->sf = tmp >> 7 & 1;
		state->zf = tmp >> 6 & 1;
		state->af = tmp >> 4 & 1;
		state->pf = tmp >> 2 & 1;
		state->cf = tmp >> 0 & 1;
	}

	SET_RP(state->sp, sp);
}

void i8080_rst(i8080 *state, uint8_t arg) {
	// already handling an interrupt
	if (!state->ei)
		return;

	state->ei = 0;
	state->hlt = 0;
	i8080_call(state, 0xcd, arg);
}

void i8080_step(i8080 *state) {

	// when cpu is halted, only a interrupt can resume operation
	if (state->hlt) {
#ifdef ENABLE_DEBUG
		printf("%04x: halted!\n", GET_RP(state->pc));
#endif
		return;
	}

	uint16_t pc = GET_RP(state->pc);
	uint8_t opcode = state->memory[pc], *tmp;

	opcode_t const *inst = &OPCODES[opcode];

	// increment PC
	state->pc.h = (pc + inst->size) >> 8;
	state->pc.l = (pc + inst->size) & 0xff;
	state->cycles = inst->cyc;

	switch (opcode) {
	case 0x00:
		break;
	case 0x01:
	case 0x11:
	case 0x21:
	case 0x31:
		// LXI
		tmp = i8080_rp(state, opcode >> 4 & 3);
		tmp[0] = state->memory[pc + 2];
		tmp[1] = state->memory[pc + 1];
		break;
	case 0x02:
	case 0x12:
		// STAX
		tmp = i8080_rp(state, opcode >> 4 & 1);
		state->memory[R16(tmp[0], tmp[1])] = state->a;
		break;
	case 0x03:
	case 0x13:
	case 0x23:
	case 0x33:
		// INX
		i8080_inx(i8080_rp(state, opcode >> 4 & 3), 1);
		break;
	case 0x04:
	case 0x0c:
	case 0x14:
	case 0x1c:
	case 0x24:
	case 0x2c:
	case 0x34:
	case 0x3c:
		// INR
		i8080_inr(state, i8080_reg(state, opcode >> 3 & 7), 1);
		break;
	case 0x05:
	case 0x0d:
	case 0x15:
	case 0x1d:
	case 0x25:
	case 0x2d:
	case 0x35:
	case 0x3d:
		// DCR
		i8080_dcr(state, i8080_reg(state, opcode >> 3 & 7), 1);
		break;
	case 0x06:
	case 0x0e:
	case 0x16:
	case 0x1e:
	case 0x26:
	case 0x2e:
	case 0x36:
	case 0x3e:
		// MVI
		*i8080_reg(state, opcode >> 3 & 7) = state->memory[pc + 1];
		break;
	case 0x07:
	case 0x0f:
	case 0x17:
	case 0x1f:
		// RLC | RRC | RAL | RAR
		i8080_rot(state, opcode >> 3 & 3);
		break;
	case 0x09:
	case 0x19:
	case 0x29:
	case 0x39:
		// DAD
		i8080_dad(state, i8080_rp(state, opcode >> 4 & 3));
		break;
	case 0x0a:
	case 0x1a:
		// LDAX
		tmp = i8080_rp(state, opcode >> 4 & 1);
		state->a = state->memory[R16(tmp[0], tmp[1])];
		break;
	case 0x0b:
	case 0x1b:
	case 0x2b:
	case 0x3b:
		// DCX
		i8080_inx(i8080_rp(state, opcode >> 4 & 3), 0xffff);
		break;
	case 0x22:
	case 0x2a:
	case 0x32:
	case 0x3a:
		// SHLD | LHLD | LDA | STA
		tmp = &state->memory[pc + 1];
		i8080_daddr(state, R16(tmp[1], tmp[0]), opcode >> 3 & 3);
		break;
	case 0x37:
	case 0x3f:
		// CMC | STC
		if (opcode >> 3 & 1)
			state->cf ^= 1;
		else
			state->cf = 1;
		break;
	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x44:
	case 0x45:
	case 0x46:
	case 0x47:
	case 0x48:
	case 0x49:
	case 0x4a:
	case 0x4b:
	case 0x4c:
	case 0x4d:
	case 0x4e:
	case 0x4f:
	case 0x50:
	case 0x51:
	case 0x52:
	case 0x53:
	case 0x54:
	case 0x55:
	case 0x56:
	case 0x57:
	case 0x58:
	case 0x59:
	case 0x5a:
	case 0x5b:
	case 0x5c:
	case 0x5d:
	case 0x5e:
	case 0x5f:
	case 0x60:
	case 0x61:
	case 0x62:
	case 0x63:
	case 0x64:
	case 0x65:
	case 0x66:
	case 0x67:
	case 0x68:
	case 0x69:
	case 0x6a:
	case 0x6b:
	case 0x6c:
	case 0x6d:
	case 0x6e:
	case 0x6f:
	case 0x70:
	case 0x71:
	case 0x72:
	case 0x73:
	case 0x74:
	case 0x75:
	case 0x77:
	case 0x78:
	case 0x79:
	case 0x7a:
	case 0x7b:
	case 0x7c:
	case 0x7d:
	case 0x7e:
	case 0x7f:
		// MOV
		*i8080_reg(state, opcode >> 3 & 7) = *i8080_reg(state, opcode & 7);
		break;
	case 0x76:
		// HLT
		state->hlt = 1;
		break;
	case 0x80:
	case 0x81:
	case 0x82:
	case 0x83:
	case 0x84:
	case 0x85:
	case 0x86:
	case 0x87:
		// ADD
		i8080_add(state, *i8080_reg(state, opcode & 7));
		break;
	case 0x88:
	case 0x89:
	case 0x8a:
	case 0x8b:
	case 0x8c:
	case 0x8d:
	case 0x8e:
	case 0x8f:
		// ADC
		i8080_add(state, *i8080_reg(state, opcode & 7) + state->cf);
		break;
	case 0x90:
	case 0x91:
	case 0x92:
	case 0x93:
	case 0x94:
	case 0x95:
	case 0x96:
	case 0x97:
		// SUB
		i8080_sub(state, *i8080_reg(state, opcode & 7));
		break;
	case 0x98:
	case 0x99:
	case 0x9a:
	case 0x9b:
	case 0x9c:
	case 0x9d:
	case 0x9e:
	case 0x9f:
		// SBB
		i8080_sub(state, *i8080_reg(state, opcode & 7) + state->cf);
		break;
	case 0xa0:
	case 0xa1:
	case 0xa2:
	case 0xa3:
	case 0xa4:
	case 0xa5:
	case 0xa6:
	case 0xa7:
	case 0xa8:
	case 0xa9:
	case 0xaa:
	case 0xab:
	case 0xac:
	case 0xad:
	case 0xae:
	case 0xaf:
	case 0xb0:
	case 0xb1:
	case 0xb2:
	case 0xb3:
	case 0xb4:
	case 0xb5:
	case 0xb6:
	case 0xb7:
		// ANA | XRA | ORA
		i8080_logical(state, opcode >> 3 & 7, *i8080_reg(state, opcode & 7));
		break;
	case 0xb8:
	case 0xb9:
	case 0xba:
	case 0xbb:
	case 0xbc:
	case 0xbd:
	case 0xbe:
	case 0xbf:
		// CMP
		i8080_cmp(state, *i8080_reg(state, opcode & 7));
		break;
	case 0xc0:
	case 0xc8:
	case 0xc9:
	case 0xd0:
	case 0xd8:
	case 0xe0:
	case 0xe8:
	case 0xf0:
	case 0xf8:
		// R(COND)
		i8080_ret(state, opcode);
		break;
	case 0xc1:
	case 0xd1:
	case 0xe1:
	case 0xf1:
		// POP
		i8080_pop(state, opcode >> 4 & 3);
		break;
	case 0xc2:
	case 0xc3:
	case 0xca:
	case 0xd2:
	case 0xda:
	case 0xe2:
	case 0xea:
	case 0xf2:
	case 0xfa:
		// JMP
		if (!i8080_branch_cond(state, opcode))
			break;
		state->pc.h = state->memory[pc + 2];
		state->pc.l = state->memory[pc + 1];
		break;
	case 0xc4:
	case 0xcc:
	case 0xcd:
	case 0xd4:
	case 0xdc:
	case 0xe4:
	case 0xec:
	case 0xf4:
	case 0xfc:
		// CALL
		i8080_call(state, opcode, R16(state->memory[pc + 2],
			state->memory[pc + 1]));
		break;
	case 0xc5:
	case 0xd5:
	case 0xe5:
	case 0xf5:
		// PUSH
		i8080_push(state, opcode >> 4 & 3);
		break;
	case 0xc6:
	case 0xce:
	case 0xd6:
	case 0xde:
	case 0xe6:
	case 0xee:
	case 0xf6:
	case 0xfe:
		i8080_imm(state, opcode >> 3 & 7, state->memory[pc + 1]);
		break;
	case 0xc7:
	case 0xcf:
	case 0xd7:
	case 0xdf:
	case 0xe7:
	case 0xef:
	case 0xf7:
	case 0xff:
		// RST
		i8080_rst(state, opcode & 0x38);
		break;
	case 0x2f:
		// CMA
		state->a = ~state->a;
		break;
	case 0x27:
		// DAA - used with BCD arithmetic
		if ((state->a & 0xf) > 9 || state->af) {
			state->af = (state->a & 0xf) + 6 > 0xf;
			state->a += 6;
		}
		if (state->a >> 4 > 9 || state->cf) {
			state->cf = (state->a >> 4) + 6 > 0xf;
			state->a = ((state->a >> 4) + 6) << 4 | (state->a & 0xf);
		}
		i8080_zsp(state, state->a);
		break;
	case 0xeb:
		// XCHG
		// A XOR B XOR B = A
		state->h ^= state->d;
		state->d ^= state->h;
		state->h ^= state->d;

		state->l ^= state->e;
		state->e ^= state->l;
		state->l ^= state->e;
		break;
	case 0xe3:
		// XTHL
		tmp = &state->memory[GET_RP(state->sp)];
		state->h ^= tmp[1];
		tmp[1] ^= state->h;
		state->h ^= tmp[1];

		state->l ^= tmp[0];
		tmp[0] ^= state->l;
		state->l ^= tmp[0];
		break;
	case 0xf9:
		// SPHL
		state->sp.h = state->h;
		state->sp.l = state->l;
		break;
	case 0xe9:
		// PCHL
		state->pc.h = state->h;
		state->pc.l = state->l;
		break;
	case 0xfb:
	case 0xf3:
		// EI
		state->ei = opcode >> 3 & 1;
		break;
	case 0xdb:
	case 0xd3:
		if (opcode >> 3 & 1) {
			if (state->port_in)
				state->a = state->port_in(state->memory[pc + 1]);
		} else if (state->port_out)
			state->port_out(state->memory[pc + 1], state->a);
		break;
	default:
		printf("unimplemented instruction: %02x (pc %04x)\n", opcode, pc);
	}

#ifdef ENABLE_DEBUG
	int output_len = 0;

	printf("%04x: (%02x) ", pc, opcode);

	if (inst->size > 1) {
		uint16_t param = state->memory[pc + 1];

		if (inst->size > 2)
			param |= state->memory[pc + 2] << 8;

		output_len += printf(inst->fmt, param);
	} else
		output_len += printf(inst->fmt);

	printf(
		"%*s: a=%02x,bc=%04x,de=%04x,hl=%04x,"
		"sp=%04x | c=%d,p=%d,a=%d,z=%d,s=%d "
		"| M=%02x\n",
		17 - output_len, "",
		state->a,
		R16(state->b, state->c),
		R16(state->d, state->e),
		R16(state->h, state->l),
		R16(state->sp.h, state->sp.l),
		state->cf,
		state->pf,
		state->af,
		state->zf,
		state->sf,
		state->memory[R16(state->h, state->l)]
	);
#endif

}
