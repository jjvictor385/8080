#include <stdio.h>
#include <SDL2/SDL.h>
#include "core.h"

#define ROM "roms/invaders"

#define SDL_TEST(expr, s) do { \
	if (!(expr)) { \
		SDL_LogCritical(0, s " failed: %s", SDL_GetError()); \
		return 1; \
	} \
} while (0)

// special shift register
static uint16_t shift_data = 0;
static uint8_t shift_amount = 0;
static uint8_t inp1 = 0x88, inp2 = 0;

static void port_out(uint8_t port, uint8_t acc) {
	switch (port) {
	case 0x2:
		shift_amount = acc & 3;
		break;
	case 0x4:
		shift_data >>= 8;
		shift_data |= acc << 8;
		break;
	}
}

static uint8_t port_in(uint8_t port) {
	switch (port) {
	case 0x1:
		return inp1;
	case 0x2:
		return inp2;
	case 0x3:
		return shift_data >> (8 - shift_amount) & 0xff;
	}
	return 0;
}

static void render_copy(
	SDL_Renderer *render,
	SDL_Surface *surface
) {
	SDL_Texture *tex = SDL_CreateTextureFromSurface(
		render,
		surface
	);

	SDL_RenderCopy(render, tex, NULL, NULL);
	SDL_DestroyTexture(tex);
}

#undef main
int main() {

	i8080 state = {
		.ei = 1,
		.hlt = 0,
		.pc = {0, 0},
		.memory = (uint8_t [0x10000]) {0},
		.port_in = port_in,
		.port_out = port_out
	};

	FILE *fp = fopen(ROM, "rb");

	if (!fp) {
		perror("can't open " ROM);
		return 1;
	}

	fread(state.memory, 1, 0x2000, fp);

	if (ferror(fp)) {
		perror("can't read " ROM);
		return 1;
	}

	SDL_Window *window;
	SDL_Renderer *render;

#define W 224
#define H 256

	SDL_TEST(!SDL_Init(SDL_INIT_VIDEO), "init video");
	SDL_TEST(window = SDL_CreateWindow(
		"Space Invaders",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		W * 2, H * 2,
		0
	), "create window");
	SDL_TEST(render = SDL_CreateRenderer(
		window,
		-1,
		0
	), "create renderer");

	SDL_RenderSetLogicalSize(render, W, H);

	SDL_Surface *fb = SDL_CreateRGBSurfaceWithFormat(
		0,
		W, H,
		1,
		SDL_PIXELFORMAT_INDEX1LSB
	);

	SDL_TEST(fb, "can't create framebuffer");

	SDL_Color colors[] = {
		{0, 0, 0, 0},
		{255, 255, 255, 0}
	};

	SDL_SetPaletteColors(fb->format->palette, colors, 0, 2);

	for (uint8_t vblank = 0;;) {
		SDL_Event e;

		while (SDL_PollEvent(&e))
			if (e.type == SDL_QUIT)
				return 0;

		Uint8 const *kb = SDL_GetKeyboardState(NULL);

#define SET_BIT(t, n, v) (t = (t & ~(1 << (n))) | ((v) << (n)))

		SET_BIT(inp1, 0, kb[SDL_SCANCODE_LCTRL]);
		SET_BIT(inp1, 2, kb[SDL_SCANCODE_1]);
		SET_BIT(inp1, 4, kb[SDL_SCANCODE_A]);
		SET_BIT(inp1, 5, kb[SDL_SCANCODE_LEFT]);
		SET_BIT(inp1, 6, kb[SDL_SCANCODE_RIGHT]);
		SET_BIT(inp2, 2, kb[SDL_SCANCODE_9]);

#define VB 8

		if (kb[SDL_SCANCODE_5])
			state.pc.h = 0, state.pc.l = 0;

		if (vblank++ == VB / 2 || vblank == VB) {
			i8080_rst(&state, vblank == VB ? 0x10 : 0x8);

			if (vblank == VB)
				vblank = 0;
		}

		for (unsigned i = 1000; i--; )
			i8080_step(&state);

		SDL_LockSurface(fb);

		uint8_t *vram = &state.memory[0x2400];

		for (unsigned c = H, i = 0; i < W; vram++) {
			uint8_t
				*p = (uint8_t *)fb->pixels + i / 8,
				*c0 = &p[(c - 1) * fb->pitch],
				*c1 = &p[(c - 2) * fb->pitch],
				*c2 = &p[(c - 3) * fb->pitch],
				*c3 = &p[(c - 4) * fb->pitch],
				*c4 = &p[(c - 5) * fb->pitch],
				*c5 = &p[(c - 6) * fb->pitch],
				*c6 = &p[(c - 7) * fb->pitch],
				*c7 = &p[(c - 8) * fb->pitch];

			SET_BIT(*c0, i % 8, *vram >> 0 & 1);
			SET_BIT(*c1, i % 8, *vram >> 1 & 1);
			SET_BIT(*c2, i % 8, *vram >> 2 & 1);
			SET_BIT(*c3, i % 8, *vram >> 3 & 1);
			SET_BIT(*c4, i % 8, *vram >> 4 & 1);
			SET_BIT(*c5, i % 8, *vram >> 5 & 1);
			SET_BIT(*c6, i % 8, *vram >> 6 & 1);
			SET_BIT(*c7, i % 8, *vram >> 7 & 1);

			c -= 8;

			if (!c) {
				c = H;
				i++;
			}
		}

		SDL_UnlockSurface(fb);

		render_copy(render, fb);

		SDL_RenderPresent(render);
	}

	return 0;
}
