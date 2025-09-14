#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#define TARGET_FPS 15
#define FRAME_TIME_NS (1000000000 / TARGET_FPS)

#define WIDHT (640/2)
#define HEIGHT (480/2)
#define SCREEN_WIDHT (640*2)
#define SCREEN_HEIGHT (480*2)
#define SCREEN_WIDHT_RATIO (SCREEN_WIDHT/WIDHT)
#define SCREEN_HEIGHT_RATIO (SCREEN_HEIGHT/HEIGHT)

#define MAX_ACCEL 25.0

typedef long long ns_t;

typedef struct {
	int x, y;
	double accel, direction;
} Partical;
Partical particals[WIDHT][HEIGHT];

bool has_pixels_changed = false;
uint32_t *pixels;
uint32_t *window_pixels;

ns_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ns_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}


void to_window_pixels(uint32_t *p, size_t pw, size_t ph) 
{
	if (!has_pixels_changed) return;
	int wr = SCREEN_WIDHT/pw;
	int hr = SCREEN_HEIGHT/ph;
	for (int y0 = 0; y0 < SCREEN_HEIGHT; y0++) {
		for (int x0 = 0; x0 < SCREEN_WIDHT; x0++) {
			window_pixels[x0 + SCREEN_WIDHT * y0] = p[(x0/wr) + pw * (y0/hr)];
		}
	}
	has_pixels_changed = false;
}

void set_pixels(uint32_t *p, size_t pw,
				size_t x, size_t y,
				size_t w, size_t h,
				uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	for (int y0 = y; y0 < h; y0++) {
		for (int x0 = x; x0 < w; x0++) {
			p[x0 + pw * y0] = a << 24 | r << 16 | g << 8 | b;
		}
	}
	has_pixels_changed = true;
}

static inline void set_pixel(uint32_t *p, size_t pw, 
				size_t x, size_t y, 
				uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	p[x + pw * y] = a << 24 | r << 16 | g << 8 | b;
}

void plotLine(uint32_t *p, size_t pw, 
			  int x0, int y0, 
			  int x1, int y1, 
			  uint8_t r, uint8_t g, uint8_t b, uint8_t a) 
{
	int dx = abs(x1 - x0);
	int sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0);
	int sy = y0 < y1 ? 1 : -1;
	int error = dx + dy;
	for (;;) {
		set_pixel(p, pw, (x0 >= 0 ? x0 : 0), (y0 >= 0 ? y0 : 0), r, g, b, a);
		int e2 = 2 * error;
		if (e2 >= dy) {
			if (x0 == x1) break;
			error += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			if (y0 == y1) break;
			error += dx;
			y0 += sy;
		}
	}
	has_pixels_changed = true;
}

void update_particals(void) 
{
	for (int y = 0; y < HEIGHT; y++) {
		for (int x = 0; x < WIDHT; x++) {
			if (particals[x][y].accel != 0) {
				Partical *p = &particals[x][y];
				int x0 = x + p->accel * cos(p->direction);
				int y0 = y + p->accel * sin(p->direction);
			}
		}
	}
}

void mouse_moved(XMotionEvent e, int prev_x, int prev_y, int prev_time) 
{
	int x = e.x/SCREEN_WIDHT_RATIO;
	int y = e.y/SCREEN_HEIGHT_RATIO;
	int px = prev_x/SCREEN_WIDHT_RATIO;
	int py = prev_y/SCREEN_HEIGHT_RATIO;

	int dx = x - px;
	int dy = y - py;

	double dist = sqrt((dx * dx) + (dy * dy));
	double time = e.time - prev_time;
	double speed = (speed = (double)dist/time) > MAX_ACCEL ? MAX_ACCEL : speed;
	double angle = atan2(dy, dx);
	particals[x][y].accel = speed;
	particals[x][y].direction = angle;
}

int main(void) 
{
	for (int y = 0; y < HEIGHT; y++) {
		for (int x = 0; x < WIDHT; x++) {
			particals[x][y].x = x;
			particals[x][y].y = y;
		}
	}
	Display *d = XOpenDisplay(NULL);
	if (d == NULL) {
		fprintf(stderr, "Error: XOpenDisplay failed\n");
		return -1;
	}
	Window w = XCreateSimpleWindow(
			d, 
			DefaultRootWindow(d), 
			0, 0, 
			SCREEN_WIDHT, SCREEN_HEIGHT, 
			0, 0, 
			0xff000000);
	XWindowAttributes wa = {0};
	XGetWindowAttributes(d, w, &wa);

	XImage *img;
	window_pixels = malloc(SCREEN_WIDHT*SCREEN_HEIGHT*sizeof(*window_pixels));
	if (window_pixels == NULL) {
		fprintf(stderr, "Error: failed to allocate memory for window_pixels\n");
		XCloseDisplay(d);
		return -1;
	}
	pixels = malloc(WIDHT*HEIGHT*sizeof(*pixels));
	if (pixels == NULL) {
		fprintf(stderr, "Error: failed to allocate memory for pixels\n");
		free(window_pixels);
		XCloseDisplay(d);
		return -1;
	}
	img = XCreateImage(
			d, 
			wa.visual, 
			wa.depth, 
			ZPixmap, 
			0, 
			(char *) window_pixels, 
			SCREEN_WIDHT, 
			SCREEN_HEIGHT, 
			32, 
			SCREEN_WIDHT * sizeof(*window_pixels));

	GC gc = XCreateGC(d, w, 0, NULL);

	Atom wm_delete_window = XInternAtom(d, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(d, w, &wm_delete_window, 1);

	// makes window float in i3
	Atom wm_type = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
	Atom wm_type_dialog = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	XChangeProperty(d, w, wm_type, XA_ATOM, 32, 
			PropModeReplace, (unsigned char *)&wm_type_dialog, 1);

	long ev_mask = KeyPressMask 
			| PointerMotionMask 
			| ButtonPressMask 
			| ButtonReleaseMask 
			| ButtonMotionMask ;

	XSelectInput(d, w, ev_mask);

	XMapWindow(d, w);
	set_pixels(
			pixels, WIDHT, 
			0, 0, 
			WIDHT, HEIGHT, 
			0, 0, 0, 255);
	to_window_pixels(pixels, WIDHT, HEIGHT);
	int prev_x, prev_y, prev_time = 0;
	bool quit = false;
	while (!quit) {
		ns_t frame_start = now_ns();
		while (XPending(d) > 0) {
			XEvent ev = {0};
			XNextEvent(d, &ev);
			switch (ev.type) {
				case KeyPress:
					switch (XLookupKeysym(&ev.xkey, 0)) {
						case 'q':
							quit = 1;
							break;
					}
					break;
				case MotionNotify:
					mouse_moved(ev.xmotion, prev_x, prev_y, prev_time);
					prev_time = ev.xmotion.time;
					prev_x = ev.xmotion.x;
					prev_y = ev.xmotion.y;
					break;
				case ClientMessage:
					if ((Atom) ev.xclient.data.l[0] == wm_delete_window) {
						quit = 1;
					}
					break;
			}
		}
		set_pixels(pixels, WIDHT, 0, 0, WIDHT, HEIGHT, 0, 0, 0, 255);
		update_particals();
		to_window_pixels(pixels, WIDHT, HEIGHT);
		XPutImage(d, w, gc, img, 0, 0, 0, 0, SCREEN_WIDHT, SCREEN_HEIGHT);

		ns_t frame_end = now_ns();
		ns_t frame_time = frame_end - frame_start;
		if (frame_time < FRAME_TIME_NS) {
			ns_t sleep_ns = FRAME_TIME_NS - frame_time;
			struct timespec ts;
			ts.tv_sec = sleep_ns / 1000000000LL;
			ts.tv_nsec = sleep_ns % 1000000000LL;
			nanosleep(&ts, NULL);
		}
	}
	free(pixels);
	XCloseDisplay(d);
	return 0;
}
