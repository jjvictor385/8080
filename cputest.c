#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"

#define ORG 0x100

static i8080 cpu;
static _Bool exec;

static void port_out(uint8_t port, uint8_t acc)
{
	(void)acc;

	if (!port) {
		exec = 0;
		return;
	}

	switch (cpu.c) {
	case 2:
		putchar(cpu.e);
		break;
	case 9:
		for (uint8_t const *s = &cpu.memory[cpu.d << 8 | cpu.e];
			*s != '$'; s++)
			putchar(*s);
	}
}

static _Bool load_rom(char const *s)
{
#define error(str) fprintf(stderr, "load rom: %s: %s\n", s, str)

	FILE *fp;

	if (!(fp = fopen(s, "rb"))) {
		error(strerror(errno));
		return 0;
	}

	for (uint8_t *mem = cpu.memory + ORG;;) {
		int ch = fgetc(fp);

		if (ch == EOF)
			break;

		if (mem > cpu.memory + 0xffff) {
			error("rom exceeds memory");
			return 1;
		}

		*mem++ = ch;
	}

	if (ferror(fp)) {
		error(strerror(errno));
		return 0;
	}

#undef error

	return 1;
}

static void run_test(char const *s)
{
	if (!load_rom(s))
		return;

	// initialize state
	cpu.pc.h = ORG >> 8 & 0xff;
	cpu.pc.l = ORG >> 0 & 0xff;
	cpu.hlt = 0;
	cpu.ei = 1;

	exec = 1;

	printf("\x1b[1;32m" __FILE__ "\x1b[0m: running \"%s\"\n", s);

	while (exec)
		i8080_step(&cpu);

	printf("\n\x1b[1;32m" __FILE__ "\x1b[0m: test done.\n\n");
}

int main(void)
{

	// init state
	cpu.memory = malloc(0x10000);
	cpu.port_out = port_out;

	// OUT 0 - exits
	cpu.memory[0] = 0xd3;
	cpu.memory[1] = 0x00;

	// OUT 1 - CP/M syscall
	cpu.memory[5] = 0xd3;
	cpu.memory[6] = 0x01;
	cpu.memory[7] = 0xfb;
	cpu.memory[8] = 0xc9;

	run_test("roms/TST8080.COM");
	run_test("roms/8080PRE.COM");
	run_test("roms/CPUTEST.COM");

	free(cpu.memory);

	return 0;
}
