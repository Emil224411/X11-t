#ifndef PROG_DIR
#error Please define PROG_DIR
#endif
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
#include <errno.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include "fluid.h"
#include "util.h"

/*
 *
 * TODO
 * Clean up
 * USE A CONST TIME STEP!!
 * make DIFF, VISC and DT variable, maby add ui elements for control
 * think about moving init and related functions to a new file
 * move gpu functions to new file
 * create a shotdown function for freeing memory etc
 *
 * also make gpu work
 *
 */

#define TARGET_FPS 30
#define FRAME_TIME_NS (1000000000 / TARGET_FPS)
#define ANY_SHADER_TYPE 0

#define WIDTH 1000
#define HEIGHT 1000

#define U 0
#define DENS 1
#define V 2
#define DIFF 0.00001f
#define VISC 0.0001f
//#define DT 0.0001f

typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
static glXCreateContextAttribsARBProc glXCreateContextAttribsARB = 0;


Shader shaders[10];
struct buffers all_buffers = { 
	{ {0}, {0}, {0} }, 
	{ {0}, {0}, {0} }, 
	{ {0}, {0}, {0} } 
};

Window w;
Display *d;
GLXContext glc;
Atom wm_delete_window;

Shader sc, sf, sv;
Shader add_source_shader, diffuse_shader;
Shader advect_shader, set_bnd_shader;
Shader project_shader, project_s1_shader, project_s2_shader;
Shader lin_solve_shader;

int prev_x = 0, prev_y = 0, prev_mtime = 0;

float dens[SIZE], dens_prev[SIZE];
float u[SIZE], v[SIZE], u_prev[SIZE], v_prev[SIZE];

int init(void);
ns_t now_ns(void);
int init_shaders(void);
bool handle_Xevents(XEvent ev);
bool handle_KeyPress(XKeyEvent ke);
void mouse_moved(XMotionEvent e, int prev_x, int prev_y, int prev_time);

void set_buffer_bind(ssbo_data *x, size_t binding)
{
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, x->id);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, x->id);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	x->binding = binding;
}

void swap_buffer(ssbo_data *x0, ssbo_data *x1)
{
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, x0->id);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, x1->binding, x0->id);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, x1->id);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, x0->binding, x1->id);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	size_t tmp  = x0->binding;
	x0->binding = x1->binding;
	x1->binding = tmp;
}

void dispatch_set_bnd(int which, int b) 
{
	//printf("set_bnd: input binding = %ld, output binding = %ld\n", all_buffers.input.binding, all_buffers.output.binding);
	glUseProgram(set_bnd_shader.ID);
	glUniform1i(glGetUniformLocation(set_bnd_shader.ID, "which"), which);
	glUniform1i(glGetUniformLocation(set_bnd_shader.ID, "d0"), b);
	glDispatchCompute(N+2, 1, 1);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
	//swap_buffer(&all_buffers.output, &all_buffers.input);
}

void dispatch_add_source(float dt)
{
	glUseProgram(add_source_shader.ID);
	glUniform1f(glGetUniformLocation(add_source_shader.ID, "dt"), dt);
	glDispatchCompute(N, N, 1);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);

}
void dispatch_advect(float dt, int which, int b) 
{
	glUseProgram(advect_shader.ID);
	glUniform1f(glGetUniformLocation(advect_shader.ID, "dt"), dt);
	glUniform1i(glGetUniformLocation(advect_shader.ID, "b"), which);

	glDispatchCompute(N, N, 1);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);

	dispatch_set_bnd(which, b);

}
void dispatch_lin_solve(int b, float a, float a0, unsigned int which, int iter)
{
	glUseProgram(lin_solve_shader.ID);
	glUniform1f(glGetUniformLocation(lin_solve_shader.ID, "a"), a);
	glUniform1f(glGetUniformLocation(lin_solve_shader.ID, "a0"), a0);
	glUniform1ui(glGetUniformLocation(lin_solve_shader.ID, "which"), which);

	ssbo_data *tmp, *out;
	if (which == U) {
		tmp = &all_buffers.tmp.u;
		out = &all_buffers.output.u;
	} else if (which == DENS) {
		tmp = &all_buffers.tmp.dens;
		out = &all_buffers.output.dens;
	} else if (which == V) {
		tmp = &all_buffers.tmp.v;
		out = &all_buffers.output.v;
	}
	for (int i = 0; i < iter; i++) {
		glUseProgram(lin_solve_shader.ID);
		glDispatchCompute(N, N, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		swap_buffer(out, tmp);
		//dispatch_set_bnd(which, b);
	}

}
void dispatch_diffuse(float dt, bool w)
{
	float a = dt * DIFF * N * N;
	if (w) dispatch_lin_solve(0, a, 1.0+4.0*a, DENS, 30);
	else {
		a = dt * VISC * N * N;
		dispatch_lin_solve(1, a, 1+4*a, U, 30);
		dispatch_lin_solve(2, a, 1+4*a, V, 30);
	}
}
void dispatch_project(void)
{
	glUseProgram(project_s1_shader.ID);
	glDispatchCompute(N, N, 1);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
	swap_buffer(&all_buffers.input.u, &all_buffers.output.u);
	swap_buffer(&all_buffers.input.v, &all_buffers.output.v);

	dispatch_set_bnd(0, 0);
	dispatch_set_bnd(1, 0);

	dispatch_lin_solve(0, 1, 4, U, 30);
	swap_buffer(&all_buffers.input.u, &all_buffers.output.u);
	swap_buffer(&all_buffers.input.v, &all_buffers.output.v);

	glUseProgram(project_s2_shader.ID);
	glDispatchCompute(N, N, 1);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
	swap_buffer(&all_buffers.input.u, &all_buffers.output.u);
	swap_buffer(&all_buffers.input.v, &all_buffers.output.v);

	dispatch_set_bnd(0, 1);
	dispatch_set_bnd(1, 2);
}

int main(void) 
{
	printf("size = %ld\n", sizeof(float)*SIZE);
	if (init()) {
		fprintf(stderr, "Error: init failed\n");
		return -1;
	}
	/*
	if (init_shaders()) {
		fprintf(stderr, "Error: init_shaders failed\n");
		glXMakeCurrent(d, None, NULL);
		glXDestroyContext(d, glc);
		XDestroyWindow(d, w);
		XCloseDisplay(d);
		return -1;
	}
	 * remember to handle errors from load_all_shaders_from
	 */
	//load_all_shaders_from(PROG_DIR, ANY_SHADER_TYPE, shaders, sizeof(shaders)/sizeof(Shader));

	sc = load_shader_from_name("compute.comp", 0);
	sv = load_shader_from_name("test.vert", 0);
	sf = load_shader_from_name("test.frag", 0);
	create_c_shader(&sc, 0);
	create_vf_shaders(&sv, &sf);
	create_screen_texture(sv.ID, WIDTH, HEIGHT);

	glUseProgram(sc.ID);
	all_buffers.input.dens = create_ssbo(1, GL_DYNAMIC_DRAW);
	all_buffers.input.u = create_ssbo(2, GL_DYNAMIC_DRAW);
	all_buffers.input.v = create_ssbo(3, GL_DYNAMIC_DRAW);
	all_buffers.output.dens = create_ssbo(4, GL_DYNAMIC_DRAW);
	all_buffers.output.u = create_ssbo(5, GL_DYNAMIC_DRAW);
	all_buffers.output.v = create_ssbo(6, GL_DYNAMIC_DRAW);
	all_buffers.tmp.dens = create_ssbo(7, GL_DYNAMIC_DRAW);
	all_buffers.tmp.u = create_ssbo(8, GL_DYNAMIC_DRAW);
	all_buffers.tmp.v = create_ssbo(9, GL_DYNAMIC_DRAW);

	ns_t current_t , prev_t = now_ns();
	
	XEvent ev; 
	bool quit = false;
	all_buffers.input.dens.data[IX((N+2)/2, (N+2)/2)] = 10.0;
	int frames = 0;
	double elapsed = 0.0, now = 0.0, last = 0.0;
	while (!quit) {
		ns_t frame_start = now_ns();
		while (XPending(d) > 0) {
			XNextEvent(d,&ev);
			quit = handle_Xevents(ev);
		}

		current_t = now_ns();
		float dt = (float)(current_t - prev_t) / 1e9f;
		prev_t = current_t;
		now = now_ns()/1e9f;
		elapsed = now-last;
		frames++;
		if (elapsed >= 1.0) {
			char title[256];
			snprintf(title, sizeof(title), "fps: %f", frames/elapsed);
			XStoreName(d, w, title);
			frames = 0;
			last = now;
		}

		/*
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_buffers.input.dens.id);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
						0, 
						sizeof(all_buffers.input.dens.data),
						all_buffers.input.dens.data);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_buffers.input.u.id);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
						0, 
						sizeof(all_buffers.input.u.data),
						all_buffers.input.u.data);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_buffers.input.v.id);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
						0, 
						sizeof(all_buffers.input.v.data),
						all_buffers.input.v.data);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		dispatch_add_source(dt);
		dispatch_diffuse(dt, 1);
		dispatch_diffuse(dt, 0);
		dispatch_project();
		dispatch_advect(dt, 0, 1);
		dispatch_advect(dt, 1, 2);
		dispatch_project();

		dispatch_advect(dt, 2, 0);
		*/

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_buffers.input.dens.id);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
						0, 
						sizeof(all_buffers.input.dens.data),
						dens);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_buffers.input.u.id);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
						0, 
						sizeof(all_buffers.input.u.data),
						u);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_buffers.input.v.id);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
						0, 
						sizeof(all_buffers.input.v.data),
						v);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		vel_step(N, u, 
					v, 
					u_prev, 
					v_prev, 
					VISC, dt);
		dens_step(N, dens, 
					 dens_prev, 
					 u, 
					 v, 
					 DIFF, dt);
		
		glUseProgram(sc.ID);

		glDispatchCompute(N+2, N+2, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		memset(u_prev, 0, sizeof(u_prev));
		memset(v_prev, 0, sizeof(v_prev));
		memset(dens_prev, 0, sizeof(dens_prev));

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(sv.ID);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, screen_texture);
		renderQuad();

		glXSwapBuffers(d, w);
		//memset(all_buffers.input.dens.data, 0, sizeof(all_buffers.input.dens.data));
		//memset(all_buffers.input.u.data, 0, sizeof(all_buffers.input.u.data));
		//memset(all_buffers.input.v.data, 0, sizeof(all_buffers.input.v.data));

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
	//TODO remember to free when loaded in
	//free(add_source_shader.code);
	//free(advect_shader.code);
	//free(diffuse_shader.code);
	//free(set_bnd_shader.code);
	//free(project_shader.code);
	//free(project_s1_shader.code);
	//free(project_s2_shader.code);
	free(sc.code);
	free(sv.code);
	free(sf.code);
	glXMakeCurrent(d, None, NULL);
	glXDestroyContext(d, glc);
	XDestroyWindow(d, w);
	XCloseDisplay(d);
	return 0;
}

void mouse_moved(XMotionEvent e, int prev_x, int prev_y, int prev_time) 
{
	int x = e.x / (WIDTH / N);
	int y = e.y / (HEIGHT / N);

	int px = prev_x / (WIDTH / N);
	int py = prev_y / (HEIGHT / N);
	int dx = x - px;
	int dy = y - py;
	
	int dt = e.time - prev_time;
	if (dt <= 0) dt = 1;
	
	float speed = sqrtf(dx*dx + dy*dy) / (float)dt;
	float force = speed * 200;

	dens_prev[IX(x, y)] += force;
	u_prev[IX(x, y)] += dx * speed * 40.0f;
	v_prev[IX(x, y)] += dy * speed * 40.0f;
	//dens_prev[IX((N+2)/2, (N+2)/2)] += 100.0;
	//all_buffers.write_data->dens[IX(50, (N+2-50))] += 100.0;
	//all_buffers.input.u.data[IX(x, (N+2-y))] += 1.0;
	//all_buffers.input.v.data[IX(x, (N+2-y))] += 1.0;
	//all_buffers.input.dens.data[IX(x, (N+2-y))] += force;
	//printf("(x, y) = (%d, %d)\n", x, y);
}

bool handle_KeyPress(XKeyEvent ke) 
{
	bool should_quit = false;
	switch (XLookupKeysym(&ke, 0)) {
		case 'q':
			should_quit = true;
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
					  WIDTH, 
					  HEIGHT, 
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

int init_shaders(void) 
{

	return 0;
}
