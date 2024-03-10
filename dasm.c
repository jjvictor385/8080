#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "opcode.h"

int main(int argc, char **argv) {

#define error(...) { \
	fprintf(stderr, "%s: ", *argv); \
	fprintf(stderr, __VA_ARGS__); \
}

	if (argc == 1) {
		error("missing rom!\n");
		return 1;
	}

	FILE *fp;

	if (!(fp = fopen(argv[1], "rb"))) {
		error("can't open %s: %s\n", argv[1], strerror(errno));
		return 1;
	}

	for (uint16_t pc = 0;;) {
		int op = fgetc(fp), arg[2];		

		if (op == EOF)
			break;

		opcode_t const *opcode = &OPCODES[op];

		if (
			(opcode->size > 1 && (arg[0] = fgetc(fp)) == EOF) ||
			(opcode->size > 2 && (arg[1] = fgetc(fp)) == EOF)
		)
			break;

		printf("%04x: %02x ", pc, op);

		pc += opcode->size;

		if (opcode->size > 1) {
			printf("%02x ", arg[0]);

			uint16_t value = arg[0];

			if (opcode->size > 2) {
				printf("%02x : ", arg[1]);
				value |= arg[1] << 8;
			} else
				printf("   : ");

			printf(opcode->fmt, value);
		} else
			printf("      : %s", opcode->fmt);

		putchar('\n');
	}

	if (ferror(fp)) {
		error("can't read %s: %s\n", argv[1], strerror(errno));
		return 1;
	}

	fclose(fp);

#undef error

	return 0;
}
