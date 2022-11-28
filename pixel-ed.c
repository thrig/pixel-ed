// pixel-ed - a not very functional SDL pixel editor
//
// there are things to TWEAK

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include <SDL.h>

// the editing grid starts at 0,0 to make the mouse2grid math easier

// where the palette is (beyond the editing grid)
#define PAL_OFFSET_X 16

// spacing for real pixel display (beyond the palette)
// this is, like, impossible to see on a high DPI monitor but that's
// what the modern laptop came with. sigh.
#define REAL_OFFSET_X 16
#define REAL_OFFSET_Y 0

// spacing for semi-large display (below real pixel display)
#define SEMI_OFFSET_X 16
#define SEMI_OFFSET_Y 16

struct bagofholding {
	SDL_Window *win;
	SDL_Renderer *rend;
	size_t editpixels; // size of the edit cells (might be downsized)
	size_t semipixels;
	size_t rows; // y, height
	size_t cols; // x, width
	size_t max_height;
	size_t max_width;
	size_t palettelen;
	int mousedown; // mouse being dragged?
	int curcolor;
	int altcolor;
	int dirty;
	const char *filename;
	SDL_Color **palette;
	int **pixels;
} app;

static void bailout(void);
static void cleanup_sdl(void);
static void dobail(int unused);
static void emit_help(void);
static unsigned long flagtoul(int flag, const char *flagarg, unsigned long min,
                              unsigned long max);
static int mouse2grid(int x, int y, size_t *gridx, size_t *gridy);
static void naptime(long *when, float *remainder);
static void update(void);

int
main(int argc, char *argv[])
{
	// TWEAK fiddle with these as suitable for the bitmap size, etc
	unsigned int rows = 16, cols = 16, width = 1280, height = 960,
	             editpixels = 48, semipixels = 8;

	int ch;
	while ((ch = getopt(argc, argv, "?r:c:p:h:w:s:")) != -1) {
		switch (ch) {
		case 'p': // size of the edit cells to be clicked in
			editpixels = (unsigned int) flagtoul(ch, optarg, 1, 64);
			break;
		case 's': // size of the semi-sized display
			semipixels = (unsigned int) flagtoul(ch, optarg, 1, 32);
			break;
		case 'r': // rows of the bitmap
			rows = (unsigned int) flagtoul(ch, optarg, 2, 512);
			break;
		case 'c': // columns of the bitmap
			cols = (unsigned int) flagtoul(ch, optarg, 2, 512);
			break;
		case 'h': // SDL window height
			height = (unsigned int) flagtoul(ch, optarg, 480, 8192);
			break;
		case 'w': // SDL window width
			width = (unsigned int) flagtoul(ch, optarg, 640, 8192);
			break;
		case '?':
			emit_help();
		}
	}
	argc -= optind;
	argv += optind;

	// setup pixel grid
	app.dirty      = 1; // force a redraw
	app.editpixels = editpixels;
	app.semipixels = semipixels;
	app.rows       = rows;
	app.cols       = cols;
	app.max_height = app.editpixels * app.rows;
	app.max_width  = app.editpixels * app.cols;
	// KLUGE assume height is the limiting factor (it may not be on
	// a vertical monitor; width has the additional complication of
	// needing more space for the palette and etc.)
	if (app.max_height > height) {
		app.editpixels = height / app.rows;
		app.max_height = app.editpixels * app.rows;
		app.max_width  = app.editpixels * app.cols;
	}
	// ImageMagick can "display" these, and I need BMP because SBCL
	// is a wasteland for minor things like SDL_image
	if (*argv && *argv[0] != '\0') app.filename = *argv;

	// allocate pixel grid (actually a grid for palette colors)
	app.pixels = malloc(app.rows * sizeof(int *));
	if (!app.pixels) err(1, "malloc failed");
	app.pixels[0] = calloc(app.rows * app.cols, sizeof(int));
	if (!app.pixels[0]) err(1, "calloc failed");
	for (unsigned int i = 1; i < app.rows; i++)
		app.pixels[i] = app.pixels[0] + i * app.cols;

	// https://lospec.com/palette-list/pico-8
	app.palettelen = 16;
	app.palette    = calloc(app.palettelen, sizeof(SDL_Color *));
	if (!app.palette) err(1, "calloc failed");
	app.palette[0]  = &(SDL_Color){.r = 0x00, .g = 0x00, .b = 0x00, .a = 0};
	app.palette[1]  = &(SDL_Color){.r = 0x1D, .g = 0x2B, .b = 0x53, .a = 0};
	app.palette[2]  = &(SDL_Color){.r = 0x7E, .g = 0x25, .b = 0x53, .a = 0};
	app.palette[3]  = &(SDL_Color){.r = 0x00, .g = 0x87, .b = 0x51, .a = 0};
	app.palette[4]  = &(SDL_Color){.r = 0xAB, .g = 0x52, .b = 0x36, .a = 0};
	app.palette[5]  = &(SDL_Color){.r = 0x5F, .g = 0x57, .b = 0x4F, .a = 0};
	app.palette[6]  = &(SDL_Color){.r = 0xC2, .g = 0xC3, .b = 0xC7, .a = 0};
	app.palette[7]  = &(SDL_Color){.r = 0xFF, .g = 0xF1, .b = 0xE8, .a = 0};
	app.palette[8]  = &(SDL_Color){.r = 0xFF, .g = 0x00, .b = 0x4D, .a = 0};
	app.palette[9]  = &(SDL_Color){.r = 0xFF, .g = 0xA3, .b = 0x00, .a = 0};
	app.palette[10] = &(SDL_Color){.r = 0xFF, .g = 0xEC, .b = 0x27, .a = 0};
	app.palette[11] = &(SDL_Color){.r = 0x00, .g = 0xE4, .b = 0x36, .a = 0};
	app.palette[12] = &(SDL_Color){.r = 0x29, .g = 0xAD, .b = 0xFF, .a = 0};
	app.palette[13] = &(SDL_Color){.r = 0x83, .g = 0x76, .b = 0x9C, .a = 0};
	app.palette[14] = &(SDL_Color){.r = 0xFF, .g = 0x77, .b = 0xA8, .a = 0};
	app.palette[15] = &(SDL_Color){.r = 0xFF, .g = 0xCC, .b = 0xAA, .a = 0};

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		errx(1, "SDL_Init failed: %s", SDL_GetError());
	app.win = SDL_CreateWindow("pixel-ed", SDL_WINDOWPOS_UNDEFINED,
	                           SDL_WINDOWPOS_UNDEFINED, (int) width,
	                           (int) height, 0);
	if (!app.win) errx(1, "SDL_CreateWindow failed: %s", SDL_GetError());
	app.rend = SDL_CreateRenderer(app.win, -1, SDL_RENDERER_ACCELERATED);
	if (!app.rend) errx(1, "SDL_CreateRenderer failed: %s", SDL_GetError());

	atexit(cleanup_sdl);
	// KLUGE no save support (yet?) so we do that at exit
	signal(SIGINT, dobail);
	signal(SIGTERM, dobail);
	signal(SIGHUP, dobail);
	signal(SIGUSR1, dobail);

	float remainder = 0.0; // for naptime()
	long when       = SDL_GetTicks();

#ifdef __OpenBSD__
	// NOTE "drm" may show up as 'pledge "tty", syscall 54' which
	// the "tty" allow may not help with
	if (pledge("cpath drm stdio wpath unveil", NULL) == -1)
		err(1, "pledge failed");
	if (app.filename)
		if (unveil(app.filename, "crw") == -1) err(1, "unveil failed");
#endif

	int activecolor = app.curcolor; // for mouse dragging
	while (1) {
		SDL_Event event;
		SDL_PollEvent(&event);
		// handle input
		switch (event.type) {
		// edit a cell
		case SDL_MOUSEBUTTONDOWN: {
			size_t gridx, gridy;
			if (mouse2grid(event.motion.x, event.motion.y, &gridx,
			               &gridy)) {
				if (app.pixels[gridy][gridx] == app.curcolor)
					activecolor = app.pixels[gridy][gridx] =
					  app.altcolor;
				else
					activecolor = app.pixels[gridy][gridx] =
					  app.curcolor;
				app.dirty = app.mousedown = 1;
			}
			break;
		}
		// copy the most recent mousedown color, if relevant
		case SDL_MOUSEMOTION:
			if (app.mousedown) {
				size_t gridx, gridy;
				if (mouse2grid(event.motion.x, event.motion.y,
				               &gridx, &gridy)) {
					app.pixels[gridy][gridx] = activecolor;
					app.dirty                = 1;
				}
			}
			break;
		case SDL_MOUSEBUTTONUP:
			app.mousedown = 0;
			break;
		case SDL_WINDOWEVENT:
			app.dirty = 1;
			break;
		case SDL_KEYDOWN:
			switch (event.key.keysym.scancode) {
			case SDL_SCANCODE_Q:
				if (SDL_GetModState() & KMOD_SHIFT) bailout();
			default:;
			}
			break;
		case SDL_QUIT:
			bailout();
		}

		if (app.dirty) {
			update();
			app.dirty = 0;
		}
		naptime(&when, &remainder);
	}
	exit(1);
}

static void
bailout(void)
{
	if (app.filename) {
		// mostly cargo culted from the Brogue screenshot code
		SDL_Surface *grid = SDL_CreateRGBSurfaceWithFormat(
		  0, (int) app.cols, (int) app.rows, 32,
		  SDL_PIXELFORMAT_ARGB8888);
		if (grid) {
			SDL_Rect rect;
			rect.x = (int) (app.max_width + PAL_OFFSET_X +
			                app.editpixels * 2 + REAL_OFFSET_X);
			rect.y = REAL_OFFSET_Y;
			rect.h = (int) app.rows;
			rect.w = (int) app.cols;
			SDL_RenderReadPixels(app.rend, &rect,
			                     SDL_PIXELFORMAT_ARGB8888,
			                     grid->pixels, grid->pitch);
			SDL_SaveBMP(grid, app.filename);
			SDL_FreeSurface(grid);
		} else {
			warnx("SDL_CreateRGBSurfaceWithFormat failed: %s",
			      SDL_GetError());
		}
	}
	exit(EXIT_SUCCESS);
}

static void
cleanup_sdl(void)
{
	SDL_DestroyRenderer(app.rend);
	SDL_DestroyWindow(app.win);
	SDL_Quit();
}

static void
dobail(int unused)
{
	bailout();
}

static void
emit_help(void)
{
	fputs("Usage: pixel-ed [-p edit-pixel-size] [-s semi-size]\n"
	      "                [-r rows] [-c columns]\n"
	      "                [-h window-height] [-w window-width]\n"
	      "                [filename.bmp]\n",
	      stderr);
	exit(EX_USAGE);
}

static unsigned long
flagtoul(int flag, const char *flagarg, unsigned long min, unsigned long max)
{
	char *ep;
	unsigned long val;
	if (!flagarg || *flagarg == '\0') errx(1, "flag -%c not set", flag);
	while (isspace(*flagarg))
		flagarg++;
	if (*flagarg != '+' && !isdigit(*flagarg))
		errx(1, "flag -%c must be positive", flag);
	errno = 0;
	val   = strtoul(flagarg, &ep, 0);
	if (flagarg[0] == '\0' || *ep != '\0')
		errx(1, "strtoul failed on -%c %s", flag, flagarg);
	if (errno == ERANGE && val == ULONG_MAX)
		errx(1, "value for -%c not an unsigned long", flag);
	if (min != 0 && val < min)
		errx(1, "value for -%c below min %lu", flag, min);
	if (max != ULONG_MAX && val > max)
		errx(1, "value for -%c above max %lu", flag, max);
	return val;
}

// borrowed from some SDL tutorial somewhere -- lock the frame rate
inline static void
naptime(long *when, float *remainder)
{
	long wait, frametime;
	wait = 16 + (long) *remainder;
	*remainder -= (float) (int) *remainder;
	frametime = SDL_GetTicks() - *when;
	wait -= frametime;
	if (wait < 1) wait = 1;
	SDL_Delay((Uint32) wait);
	*remainder += (float) 0.667;
	*when = SDL_GetTicks();
}

inline static int
mouse2grid(int x, int y, size_t *gridx, size_t *gridy)
{
	if (x < 0 || y < 0) return 0; // never valid

	// is the click in the edit grid, or the palette list?
	if (x < (int) app.max_width && x < (int) app.max_height) {
		size_t newx = (unsigned) x / app.editpixels;
		if (newx >= app.cols) return 0;
		size_t newy = (unsigned) y / app.editpixels;
		if (newy >= app.rows) return 0;
		*gridx = newx;
		*gridy = newy;
		return 1;
	} else if (x > (int) (app.max_width + PAL_OFFSET_X)) {
		size_t newy = (unsigned) y / app.editpixels;
		if (newy < app.palettelen) {
			app.altcolor = app.curcolor;
			app.curcolor = (int) newy;
		}
	}
	return 0;
}

inline static void
update(void)
{
	// TWEAK background color
	SDL_SetRenderDrawColor(app.rend, 192, 192, 192, 0);
	SDL_RenderClear(app.rend);

	// edit grid
	for (size_t y = 0; y < app.rows; y++) {
		for (size_t x = 0; x < app.cols; x++) {
			SDL_Rect rect;
			rect.h = rect.w = (int) app.editpixels;
			rect.x          = (int) (x * app.editpixels);
			rect.y          = (int) (y * app.editpixels);
			int color       = app.pixels[y][x];
			SDL_SetRenderDrawColor(app.rend, app.palette[color]->r,
			                       app.palette[color]->g,
			                       app.palette[color]->b,
			                       app.palette[color]->a);
			SDL_RenderFillRect(app.rend, &rect);

			// literal grid (for filename output)
			SDL_RenderDrawPoint(
			  app.rend,
			  (int) (app.max_width + PAL_OFFSET_X +
			         app.editpixels * 2 + REAL_OFFSET_X + x),
			  (int) (REAL_OFFSET_Y + y));

			// semi-large grid for preview or other needs
			rect.x = (int) (app.max_width + PAL_OFFSET_X +
			                app.editpixels * 2 + SEMI_OFFSET_X +
			                x * app.semipixels);
			rect.y =
			  (int) (app.rows + SEMI_OFFSET_Y + y * app.semipixels);
			rect.h = rect.w = (int) app.semipixels;
			SDL_RenderFillRect(app.rend, &rect);
		}
	}

	// grid line color around the editing cells
	SDL_SetRenderDrawColor(app.rend, 192, 192, 192, 0);
	for (int y = 0, yyy = 0; y <= (int) app.rows;
	     y++, yyy += app.editpixels)
		SDL_RenderDrawLine(app.rend, 0, yyy, (int) app.max_width, yyy);
	for (int x = 0, xxx = 0; x <= (int) app.cols;
	     x++, xxx += app.editpixels)
		SDL_RenderDrawLine(app.rend, xxx, 0, xxx, (int) app.max_height);

	// palette to be clicked on to change the active
	// color. the *2 is so the palette looks
	// different than the edit grid
	SDL_Rect rect;
	rect.x = (int) app.max_width + PAL_OFFSET_X;
	rect.w = (int) app.editpixels * 2;
	rect.h = (int) app.editpixels;
	for (int i = 0, yyy = 0; i < (int) app.palettelen;
	     i++, yyy += app.editpixels) {
		rect.y = yyy;
		SDL_SetRenderDrawColor(app.rend, app.palette[i]->r,
		                       app.palette[i]->g, app.palette[i]->b,
		                       app.palette[i]->a);
		SDL_RenderFillRect(app.rend, &rect);
	}

	SDL_RenderPresent(app.rend);
}
