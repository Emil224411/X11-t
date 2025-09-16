#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <time.h>
#include <math.h>
#include <memory.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <GL/gl.h>
#include <GL/glu.h>

#include "fluid.h"

#define TARGET_FPS 30
#define FRAME_TIME_NS (1000000000 / TARGET_FPS)
#define NS_TO_MS(NS)  (NS / 1000000)
#define MS_TO_S(MS)   (MS / 1000.0)

#define WIDTH (800)
#define HEIGHT (800)
#define SCREEN_WIDTH (WIDTH*1)
#define SCREEN_HEIGHT (HEIGHT*1)
#define SCREEN_WIDTH_RATIO (SCREEN_WIDTH/WIDTH)
#define SCREEN_HEIGHT_RATIO (SCREEN_HEIGHT/HEIGHT)

#define DIFF 0.0002f
#define VISC 0.0002f

typedef long long ns_t;

bool has_pixels_changed = false;
uint32_t *pixels;
uint32_t *window_pixels;

float u[SIZE], v[SIZE], u_prev[SIZE], v_prev[SIZE];
float dens[SIZE], dens_prev[SIZE];

ns_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ns_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static inline void set_pixel(uint32_t *p, size_t pw, 
							 int x, int y, 
							 uint8_t r, uint8_t g, uint8_t b, uint8_t a) 
{
    p[x + pw * y] = a<<24 | r<<16 | g<<8 | b;
}

void set_pixels(uint32_t *p, size_t pw,
				size_t x, size_t y,
				size_t w, size_t h,
				uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	for (int y0 = y; y0 < h + y; y0++) {
		for (int x0 = x; x0 < w + x; x0++) {
			p[x0 + pw * y0] = a << 24 | r << 16 | g << 8 | b;
		}
	}
	//memset(&p[x + pw * y], a << 24|r<<16|g<<8|b, w * h);
	has_pixels_changed = true;
}

void pixels_to_window(uint32_t *p, size_t pw, size_t ph) {
    if (!has_pixels_changed) return;
    int wr = SCREEN_WIDTH  / pw;
    int hr = SCREEN_HEIGHT / ph;
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0;x < SCREEN_WIDTH; x++) {
            window_pixels[x + SCREEN_WIDTH * y] = p[(x / wr) + pw * (y / hr)];
        }
    }
    has_pixels_changed = false;
}

void circle(uint32_t *p, size_t pw,
	size_t x, size_t y, 
	size_t radius, 
	uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	int t1 = radius >> 4;
	int t2 = 0;
	int x0 = radius;
	int y0 = 0;

	while (x0 >= y0) {
		set_pixel(p, pw, x + x0, y + y0, r, g, b, a);
		set_pixel(p, pw, x + x0, y - y0, r, g, b, a);
		set_pixel(p, pw, x - x0, y + y0, r, g, b, a);
		set_pixel(p, pw, x - x0, y - y0, r, g, b, a);

		set_pixel(p, pw, x + y0, y + x0, r, g, b, a);
		set_pixel(p, pw, x + y0, y - x0, r, g, b, a);
		set_pixel(p, pw, x - y0, y + x0, r, g, b, a);
		set_pixel(p, pw, x - y0, y - x0, r, g, b, a);
		y0 = y0 + 1;
		t1 = t1 + y0;
		t2 = t1 - x0;
		if (t2 >= 0) {
			t1 = t2;
			x0 = x0 - 1;
		}
	}
	has_pixels_changed = true;
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

void dens_to_pixels(uint32_t *p, int pw, float *dens, int n) 
{
	float r = (float)pw/n;
    for (int x = 0; x <= n; x++) {
        for (int y = 0; y <= n; y++) {
            int i = (i = (int)(dens[IX(x,y)] * 255)) > 255 ? 255 : (i < 0) ? 0 : i;
            set_pixel(p, pw, x*r,   y*r  , i, i, i, i);
            set_pixel(p, pw, x*r+1, y*r  , i, i, i, i);
            set_pixel(p, pw ,x*r,   y*r+1, i, i, i, i);
            set_pixel(p, pw ,x*r+1, y*r+1, i, i, i, i);
        }
    }
    has_pixels_changed = true;
}

void mouse_moved(XMotionEvent e, int prev_x, int prev_y, int prev_time) {
    int x = e.x / (SCREEN_WIDTH  / N);
    int y = e.y / (SCREEN_HEIGHT / N);

    int px = prev_x / (SCREEN_WIDTH  / N);
    int py = prev_y / (SCREEN_HEIGHT / N);
    int dx = x - px;
    int dy = y - py;

    int dt = e.time - prev_time;
    if (dt <= 0) dt = 1;

    float speed = sqrtf(dx*dx + dy*dy) / (float)dt;
    float force = speed * 350.0f;

    dens_prev[IX(x, y)] += force;
    u_prev[IX(x, y)] += dx * speed * 15.0f;
    v_prev[IX(x, y)] += dy * speed * 15.0f;
}

int main(void) {
    Display *d = XOpenDisplay(NULL);
    if (!d) { 
		fprintf(stderr,"Error: XOpenDisplay failed\n"); 
		return -1; 
	}

    Window w = XCreateSimpleWindow(d, 
								   DefaultRootWindow(d),
								   0, 
								   0, 
								   SCREEN_WIDTH, 
								   SCREEN_HEIGHT, 
								   0, 
								   0, 
								   0xff000000);

    XWindowAttributes wa; 
	XGetWindowAttributes(d,w,&wa);

    window_pixels = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(*window_pixels));
    	   pixels = malloc(WIDTH * HEIGHT * sizeof(*pixels));
    if (!window_pixels || !pixels) { 
		fprintf(stderr,"Error: Memory alloc failed\n"); 
		return -1; 
	}

    XImage *img = XCreateImage(d,
							   wa.visual,
							   wa.depth,
							   ZPixmap,
							   0,
							   (char*)window_pixels,
							   SCREEN_WIDTH,
							   SCREEN_HEIGHT,
							   32,
							   SCREEN_WIDTH*sizeof(*window_pixels));

    GC gc = XCreateGC(d, w, 0, NULL);

    Atom wm_delete_window = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, w, &wm_delete_window, 1);

	Atom wm_type = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
	Atom wm_type_dialog = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	XChangeProperty(d, w, wm_type, XA_ATOM, 32, 
			PropModeReplace, (unsigned char *)&wm_type_dialog, 1);

    XSelectInput(d, w, KeyPressMask
					 | PointerMotionMask
					 | ButtonPressMask
					 | ButtonReleaseMask
					 | ButtonMotionMask );

    XMapWindow(d, w);

    memset(u,         0, sizeof(*u        ) * SIZE); 
	memset(v,         0, sizeof(*v        ) * SIZE);
    memset(u_prev,    0, sizeof(*u_prev   ) * SIZE); 
	memset(v_prev,    0, sizeof(*v_prev   ) * SIZE);
    memset(dens,      0, sizeof(*dens     ) * SIZE); 
	memset(dens_prev, 0, sizeof(*dens_prev) * SIZE);

	set_pixels(pixels, WIDTH, 0, 0, WIDTH, HEIGHT, 0, 0, 0, 0);
	pixels_to_window(pixels, WIDTH, HEIGHT);

    int prev_x = 0, prev_y = 0, prev_mtime = 0;
    ns_t current_t ,prev_t = now_ns();

    bool quit = false;
    while (!quit) {
        ns_t frame_start = now_ns();
        while (XPending(d) > 0) {
            XEvent ev; XNextEvent(d,&ev);
            switch (ev.type) {
				case KeyPress:
					switch (XLookupKeysym(&ev.xkey, 0)) {
						case 'q':
							quit = 1;
							break;
					}
					break;
                case MotionNotify:
                    mouse_moved(ev.xmotion, prev_x, prev_y, prev_mtime);
                    prev_mtime = ev.xmotion.time;
                    prev_x     = ev.xmotion.x; 
					prev_y     = ev.xmotion.y;
                    break;
                case ClientMessage:
                    if ((Atom)ev.xclient.data.l[0] == wm_delete_window)
						quit = true;
                    break;
            }
        }

		current_t = now_ns();
        float dt = (float)(current_t - prev_t) / 1e9f;
        prev_t = current_t;

        vel_step (N, u, v, u_prev, v_prev, VISC, dt);
        dens_step(N, dens, dens_prev, u, v, DIFF, dt);

        dens_to_pixels(pixels, WIDTH, dens, N);

        pixels_to_window(pixels, WIDTH, HEIGHT);
        XPutImage(d, w, gc, img, 0, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

        memset(   u_prev, 0, sizeof(   u_prev));
        memset(   v_prev, 0, sizeof(   v_prev));
        memset(dens_prev, 0, sizeof(dens_prev));

        ns_t frame_end = now_ns();
        ns_t frame_time = frame_end - frame_start;
        if (frame_time < FRAME_TIME_NS) {
            ns_t sleep_ns = FRAME_TIME_NS - frame_time;
            struct timespec ts; 
			ts.tv_sec  = sleep_ns / 1000000000LL; 
			ts.tv_nsec = sleep_ns % 1000000000LL;
            nanosleep(&ts, NULL);
        }
    }
    free(pixels);
    XCloseDisplay(d);
    return 0;
}
