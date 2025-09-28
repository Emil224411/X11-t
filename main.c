#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <memory.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include "util.h"
#include "gpu.h"
#include "cpu.h"

/* 
 *
 * TODO
 * implement non sqaure aspect ratio 
 * implement i think it was called xinput2 but better touch controls
 * (would be cool to work with raw data from touchpad but i dont got time for all that or...)
 *
 * implement Red-Black Gauss-Seidel may help with the current set_bnd problems????
 * i dont actually think there is a problem with set_bnd when running at 250x250 it
 * seems to behave fine so maby just a problem when running at 1000x1000? but idk
 *
 * test for best workgroupe/localgroup size
 *
 * add text rendering :/ (for ui stuff)
 * add ui elements for controling stuff
 *
 * when loading shaders check for uniforms and save their names and locations
 * and create a function for setting uniforms
 *
 * IMPORTANT!!!!
 * (it still works without, so i havent done it yet)
 * create a shotdown function for freeing memory etc.
 * related to shutdown function clean up memory
 * currently there is alot of allocated memory that never gets freed
 */

//not really in use no more but keeping it around just in case
#define TARGET_FPS 30
#define FRAME_TIME_NS (1000000000 / TARGET_FPS)

typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
typedef int (*PFNGLXSWAPINTERVALSGI)(int interval);
static glXCreateContextAttribsARBProc glXCreateContextAttribsARB = 0;
static PFNGLXSWAPINTERVALSGI glXSwapIntervalSGI = 0;


struct buffers all_b = { 
	{ {0}, {0}, {0} }, 
	{ {0}, {0}, {0} }, 
	{ {0}, {0}, {0} } 
};

Settings s= { 
			true, 		//width
			1000, 		//width
			1000, 		//height
			"", 		//win_title
			0, 			//vsync
			0.00001f, 	//diff
			0.00002f, 	//visc
			0.01f, 		//dt
			1000, 		//n(n can/should not be change yet)
};

Window w;
Display *d;
GLXContext glc;
Atom wm_delete_window;

int prev_x = 0, prev_y = 0, prev_mtime = 0;

float dens[SIZE], u[SIZE], v[SIZE];
float dens_prev[SIZE], u_prev[SIZE], v_prev[SIZE];

int init(void);
ns_t now_ns(void);
bool handle_Xevents(XEvent ev);
bool handle_KeyPress(XKeyEvent ke);
void mouse_moved(XMotionEvent e, int prev_x, int prev_y, int prev_time);

void step_on_gpu(void)
{
	dispatch_add_source(s.n, &all_b.input, &all_b.output, s.dt);

	dispatch_diffuse(s.n, 0, &all_b.output.dens, &all_b.input.dens, &all_b.tmp.dens, s.diff, s.dt);
	dispatch_diffuse(s.n, 1, &all_b.output.u, &all_b.input.u, &all_b.tmp.u, s.visc, s.dt);
	dispatch_diffuse(s.n, 2, &all_b.output.v, &all_b.input.v, &all_b.tmp.v, s.visc, s.dt);

	dispatch_project(s.n, &all_b.input.u, &all_b.input.v, &all_b.output.u, &all_b.output.v, &all_b.tmp.dens);

	dispatch_advect(s.n, 1, &all_b.output.u, &all_b.input.u, &all_b.input.u, &all_b.input.v, s.dt);
	dispatch_advect(s.n, 2, &all_b.output.v, &all_b.input.v, &all_b.input.u, &all_b.input.v, s.dt);
	
	dispatch_project(s.n, &all_b.output.u, &all_b.output.v, &all_b.input.u, &all_b.input.v, &all_b.tmp.u);

	dispatch_advect(s.n, 0, &all_b.output.dens, &all_b.input.dens, &all_b.output.u, &all_b.output.v, s.dt);

}

// switch_to_gpu and switch_to_cpu should be rewriten they look like shit
void switch_to_gpu(void) 
{
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.input.u.id);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
					0, 
					sizeof(u_prev),
					u_prev);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.input.v.id);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
					0, 
					sizeof(v_prev),
					v_prev);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.input.dens.id);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
					0, 
					sizeof(dens_prev),
					dens_prev);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.output.u.id);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
					0, 
					sizeof(u),
					u);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.output.v.id);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
					0, 
					sizeof(v),
					v);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.output.dens.id);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
					0, 
					sizeof(dens),
					dens);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void switch_to_cpu(void) 
{
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.input.u.id);
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(u_prev), u_prev);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.input.v.id);
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(v_prev), v_prev);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.input.dens.id);
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(dens_prev), dens_prev);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.output.u.id);
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(u), u);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.output.v.id);
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(v), v);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.output.dens.id);
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(dens), dens);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void step_cpu(void)
{
	add_source(s.n, u, u_prev, s.dt); 
	add_source(s.n, v, v_prev, s.dt);
    add_source(s.n, dens, dens_prev, s.dt);

	diffuse(s.n, 0, dens_prev, dens, s.diff, s.dt);
	diffuse(s.n, 1, u_prev, u, s.visc, s.dt);
	diffuse(s.n, 2, v_prev, v, s.visc, s.dt);

	project(s.n, u_prev, v_prev, u, v);

	advect(s.n, 1, u, u_prev, u_prev, v_prev, s.dt); 
	advect(s.n, 2, v, v_prev, u_prev, v_prev, s.dt);
		
	project(s.n, u, v, u_prev, v_prev);
		
	advect (s.n, 0, dens, dens_prev, u, v, s.dt);
}
void dispatch_visuualize(void)
{
	glUseProgram(sc->ID);
	if (!s.on_gpu) {
		//should probly make this a function but eh....
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.input.dens.id);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
						0, 
						sizeof(all_b.input.dens.data),
						dens_prev);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.input.u.id);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
						0, 
						sizeof(all_b.input.u.data),
						u_prev);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.input.v.id);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
						0, 
						sizeof(all_b.input.v.data),
						v_prev);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.output.dens.id);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
						0, 
						sizeof(all_b.output.dens.data),
						dens);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.output.u.id);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
						0, 
						sizeof(all_b.output.u.data),
						u);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.output.v.id);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
						0, 
						sizeof(all_b.output.v.data),
						v);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	}
	glUniform1i(glGetUniformLocation(sc->ID, "n"), s.n);
	// v should have its own setting but vsync isnt being used rn sooo...
	glUniform1i(glGetUniformLocation(sc->ID, "v"), s.vsync);
	glDispatchCompute((s.n+2 + 15)/16, (s.n+2 + 15)/16, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	memset(u_prev, 0, sizeof(u_prev));
	memset(v_prev, 0, sizeof(v_prev));
	memset(dens_prev, 0, sizeof(dens_prev));
}

int main(void) 
{
	if (init()) {
		fprintf(stderr, "Error: init failed\n");
		return -1;
	}
	if (init_shaders()) {
		fprintf(stderr, "Error: init_shaders failed\n");
		glXMakeCurrent(d, None, NULL);
		glXDestroyContext(d, glc);
		XDestroyWindow(d, w);
		XCloseDisplay(d);
		return -1;
	}
	create_vf_shaders(sv, sf);
	create_screen_texture(sv->ID, s.width, s.height);

	glUseProgram(sc->ID);
	// add error checks and handling
	all_b.input.dens = create_ssbo(1, GL_DYNAMIC_DRAW);
	all_b.input.u = create_ssbo(2, GL_DYNAMIC_DRAW);
	all_b.input.v = create_ssbo(3, GL_DYNAMIC_DRAW);
	all_b.output.dens = create_ssbo(4, GL_DYNAMIC_DRAW);
	all_b.output.u = create_ssbo(5, GL_DYNAMIC_DRAW);
	all_b.output.v = create_ssbo(6, GL_DYNAMIC_DRAW);
	all_b.tmp.dens = create_ssbo(7, GL_DYNAMIC_DRAW);
	all_b.tmp.u = create_ssbo(8, GL_DYNAMIC_DRAW);
	all_b.tmp.v = create_ssbo(9, GL_DYNAMIC_DRAW);
	
	int frames = 0;
	double elapsed = 0.0, now = 0.0, last = 0.0;

	XEvent ev; 
	bool quit = false;
	while (!quit) {
		//ns_t frame_start = now_ns();
		while (XPending(d) > 0) {
			XNextEvent(d,&ev);
			quit = handle_Xevents(ev);
		}

		if (s.on_gpu) {
			// refator shit
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.input.u.id);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
							0, 
							sizeof(u_prev),
							u_prev);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.input.v.id);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
							0, 
							sizeof(v_prev),
							v_prev);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_b.input.dens.id);
			glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
							0, 
							sizeof(dens_prev),
							dens_prev);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			
			step_on_gpu();
		} else step_cpu();
		dispatch_visuualize();

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(sv->ID);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, screen_texture);
		glUniform1f(glGetUniformLocation(sv->ID, "scale"), s.width/s.n);
		renderQuad();

		glXSwapBuffers(d, w);

		//ns_t frame_end = now = now_ns();
		now = now_ns()/1e9f;

		frames++;
		elapsed = now-last;
		if (elapsed >= 1.0) {
			char title[256];
			snprintf(title, sizeof(title), "Fluid sim on %s. fps: %f, grid: %ld*%ld = %ld", 
					(s.on_gpu ? "GPU" : "CPU"), frames/elapsed, s.n, s.n, s.n*s.n);
			XStoreName(d, w, title);
			frames = 0;
			last = now;
		}
		/*ns_t frame_time = frame_end - frame_start;
		if (frame_time < FRAME_TIME_s.nS) {
			ns_t sleep_ns = FRAME_TIME_s.nS - frame_time;
			struct timespec ts; 
			ts.tv_sec  = sleep_ns / 1000000000LL; 
			ts.tv_nsec = sleep_ns % 1000000000LL;
			nanosleep(&ts, s.nULL);
		}
		*/
	}
	// free more stuff!!!!!!!1
	free(sc->code);
	free(sv->code);
	free(sf->code);
	glXMakeCurrent(d, None, NULL);
	glXDestroyContext(d, glc);
	XDestroyWindow(d, w);
	XCloseDisplay(d);
	return 0;
}

void mouse_moved(XMotionEvent e, int prev_x, int prev_y, int prev_time) 
{
	int x = e.x / (s.width/ s.n);
	int y = e.y / (s.height / s.n);
	if (x > s.n+2 || x < 0 || y > s.n+2 || y < 0) return;

	int px = prev_x / (s.width / s.n);
	int py = prev_y / (s.height / s.n);
	float dx = x - px;
	float dy = y - py;
	
	int dt = e.time - prev_time;
	if (dt <= 0) dt = 1;
	
	float speed = sqrtf(dx*dx + dy*dy) / s.dt;
	float force = speed * 2.0;

	if (e.state & Button3Mask) 
		dens_prev[IX(x, y)] += force;
	else if (e.state & Button1Mask) {
		u_prev[IX(x, y)] += dx * speed * 1.0f;
		v_prev[IX(x, y)] += dy * speed * 1.0f;
	}
}

bool handle_KeyPress(XKeyEvent ke) 
{
	bool should_quit = false;
	switch (XLookupKeysym(&ke, 0)) {
		case 'q':
			should_quit = true;
			break;
		case 'a':
			s.diff += 0.000001f;
			printf("diff\t= %f\n", s.diff);
			break;
		case 'd':
			if (s.diff - 0.000001f > 0) s.diff -= 0.000001f;
			printf("diff\t= %f\n", s.diff);
			break;
		case 'w':
			s.visc += 0.000001f;
			printf("visc\t= %f\n", s.visc);
			break;
		case 's':
			if (s.visc - 0.000001f > 0) s.visc-= 0.000001f;
			printf("visc\t= %f\n", s.visc);
			break;
		case 'e':
			s.dt += 0.0001f;
			printf("dt\t= %f\n", s.dt);
			break;
		case 'r':
			s.dt -= 0.0001f;
			printf("dt\t= %f\n", s.dt);
			break;
		case 'v':
			s.vsync = s.vsync > 0 ? 0 : 1;
		break;
		case 'c':
			if (s.on_gpu) {
				switch_to_cpu();
				s.on_gpu = false;
				printf("on cpu\n");
			} else {
				switch_to_gpu();
				s.on_gpu = true;
				printf("on gpu\n");
			}
		break;
		case 'o':
			if (s.on_gpu) dispatch_clear(s.n);
			else {
				memset(dens, 0, sizeof(dens));
				memset(u, 0, sizeof(u));
				memset(v, 0, sizeof(v));
			}
		break;
	}
	return should_quit;
}

bool handle_Xevents(XEvent ev)
{
	bool should_quit = false;
	switch (ev.type) {
		case KeyPress:
			should_quit = handle_KeyPress(ev.xkey);
			break;
		case MotionNotify:
			mouse_moved(ev.xmotion, prev_x, prev_y, prev_mtime);
			prev_mtime = ev.xmotion.time;
			prev_x     = ev.xmotion.x; 
			prev_y     = ev.xmotion.y;
			break;
		case ClientMessage:
			if ((Atom)ev.xclient.data.l[0] == wm_delete_window)
				should_quit = true;
		break;
	}
	return should_quit;
}

// maby refator?????
int init(void)
{
	d = XOpenDisplay(NULL);
	if (!d) { 
		fprintf(stderr,"Error: XOpenDisplay failed\n"); 
		return -1; 
	}
	Window root = DefaultRootWindow(d);
	int fb_att[] = {
		GLX_X_RENDERABLE, True,
		GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
		GLX_RENDER_TYPE, GLX_RGBA_BIT,
		GLX_DOUBLEBUFFER, True,
		GLX_RED_SIZE, 8,
		GLX_GREEN_SIZE, 8,
		GLX_BLUE_SIZE, 8,
		GLX_DEPTH_SIZE, 24,
		None
	};
	GLXFBConfig fbc = create_fb_conf(d, fb_att);

	XVisualInfo *vi = glXGetVisualFromFBConfig(d, fbc);
	if (vi == NULL) {
		fprintf(stderr, "Error: glXChooseVisual failed\n");
		XCloseDisplay(d);
		return -1;
	}

	Colormap cmap = XCreateColormap(d, root, vi->visual, AllocNone);
	XSetWindowAttributes swa;
	swa.colormap = cmap;
	swa.event_mask =  KeyPressMask
					| PointerMotionMask
					| ButtonPressMask
					| ButtonReleaseMask
					| ButtonMotionMask ;

	w = XCreateWindow(d, 
					  root, 
					  0, 
					  0, 
					  s.width, 
					  s.height, 
					  0, 
					  vi->depth, 
					  InputOutput, 
					  vi->visual, 
					  CWColormap | CWEventMask, 
					  &swa);

	wm_delete_window = XInternAtom(d, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(d, w, &wm_delete_window, 1);

	Atom wm_type = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
	Atom wm_type_dialog = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	XChangeProperty(d, w, wm_type, XA_ATOM, 32, 
				PropModeReplace, (unsigned char *)&wm_type_dialog, 1);

	XMapWindow(d, w);

	glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)glXGetProcAddress((const GLubyte *)"glXCreateContextAttribsARB");
	glXSwapIntervalSGI = (PFNGLXSWAPINTERVALSGI)glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalSGI");

	int catt[] = {
		GLX_CONTEXT_MAJOR_VERSION_ARB, 4,
		GLX_CONTEXT_MINOR_VERSION_ARB, 6,
		GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
		GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_DEBUG_BIT_ARB,
		None
	};
	glc = glXCreateContextAttribsARB(d, fbc, NULL, True, catt);
	glXMakeCurrent(d, w, glc);
	glEnable(GL_DEPTH_TEST);
	if (glXSwapIntervalSGI) {
		glXSwapIntervalSGI(1);
	}
	
	int glew_error = glewInit();
	if (glew_error) {
		fprintf(stderr, "Error: glewInit failed returned: %d, %s\n", glew_error, glewGetErrorString(glew_error));
		glXMakeCurrent(d, None, NULL);
		glXDestroyContext(d, glc);
		XDestroyWindow(d, w);
		XCloseDisplay(d);
		return -1;
	}
	printf("Info: GL   VER:\t %s\n", glGetString(GL_VERSION));
	printf("Info: GLSL VER:\t %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	int maxX, maxY, maxZ;
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxX);
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &maxY);
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &maxZ);
	printf("Info: GL work group count: %d * %d * %d = %lu\n",
											maxX,maxY,maxZ, (unsigned long)maxX * maxY * maxZ);
	return 0;
}

