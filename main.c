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

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include "fluid.h"

#define TARGET_FPS 60
#define FRAME_TIME_NS (1000000000 / TARGET_FPS)

#define WIDTH 1000
#define HEIGHT 1000

#define DIFF 0.00005f
#define VISC 0.0001f

typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
static glXCreateContextAttribsARBProc glXCreateContextAttribsARB = 0;

typedef long long ns_t;

typedef struct {
	size_t type;
	unsigned int sID, ID;
	char *code;
	size_t code_size;
	bool loaded;
} Shader;


struct ssbo_data { 
	float dens[SIZE]; 
	float u[SIZE]; 
	float v[SIZE]; 
};

//GLuint in_buf_id, out_buf_id, write_buf_id;
struct buffer_id {
	GLuint id;
	size_t binding;
};
struct buffers {
	struct buffer_id read, write, tmp;
	struct ssbo_data *read_data;
	struct ssbo_data *write_data;
	struct ssbo_data *tmp_data;
};
struct ssbo_data read_buf = {0};
struct ssbo_data tmp_buf = {0};
struct ssbo_data write_buf = {0};
struct buffers all_buffers = { 
	.tmp_data = &tmp_buf,
	.write_data = &write_buf, 
	.read_data = &read_buf 
};

Window w;
Display *d;
GLXContext glc;
Atom wm_delete_window;

GLuint readbuffer = 1;
GLuint writebuffer = 2;
GLuint tmpbuffer = 3;
Shader sc, sf, sv;
Shader add_source_shader, diffuse_shader;
Shader advect_shader, set_bnd_shader;
Shader project_shader, project_s1_shader, project_s2_shader;
unsigned int screen_texture;

int prev_x = 0, prev_y = 0, prev_mtime = 0;

float dens[SIZE], dens_prev[SIZE];
float u[SIZE], v[SIZE], u_prev[SIZE], v_prev[SIZE];

int init(void);
ns_t now_ns(void);
void renderQuad(void);
int init_shaders(void);
void compile_shader(Shader *s);
bool handle_Xevents(XEvent ev);
void create_c_shader(Shader *s);
bool handle_KeyPress(XKeyEvent ke);
void create_screen_texture(GLuint ID);
void create_vf_shaders(Shader *v, Shader *f);
void checkCompileErrors(GLuint shader, char *type);
Shader load_shader_code(const char *path, GLenum type);
void mouse_moved(XMotionEvent e, int prev_x, int prev_y, int prev_time);
struct buffer_id create_ssbo(void *data, size_t sizeof_data, size_t binding, GLenum usage);


void swap_buffer(struct buffer_id *x, struct buffer_id *x0)
{
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, x->id);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, x0->binding, x->id);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, x0->id);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, x->binding, x0->id);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	size_t tmp = x->binding;
	x->binding = x0->binding;
	x0->binding = tmp;
}

void dispatch_set_bnd(int which, int b) 
{
	glUseProgram(set_bnd_shader.ID);
	glUniform1i(glGetUniformLocation(set_bnd_shader.ID, "which"), which);
	glUniform1i(glGetUniformLocation(set_bnd_shader.ID, "d0"), b);
	glDispatchCompute(N+2, 1, 1);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
}
void dispatch_diffuse(float dt)
{
	glUseProgram(diffuse_shader.ID);

	glUniform1f(glGetUniformLocation(diffuse_shader.ID, "dt"), dt);
	glUniform1f(glGetUniformLocation(diffuse_shader.ID, "diff"), DIFF);
	glUniform1f(glGetUniformLocation(diffuse_shader.ID, "visc"), VISC);
	for (int i = 0; i < 30; i++) {
		glDispatchCompute(N, N, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		/*
		read = input_buffer
		write = output_buf

		diffuse(N, 0, tmp, write_x, read_current_x, diff, dt);
		diffuse(N, 1, tmp, write_u, read_current_u0, visc, dt);
		diffuse(N, 2, tmp, write_v, read_current_v0, visc, dt);

		tmp[IX(i, j)] = (read_current_x0[IX(i, j)] + a * 
						(write[IX(i-1, j  )] + write[IX(i+1, j  )] + 
						 write[IX(i  , j-1)] + write[IX(i  , j+1)])) / (1+4*a);
		set_bnd(n, b, x);
		swap(write with tmp)
		*/
		swap_buffer(&all_buffers.write, &all_buffers.tmp);
		dispatch_set_bnd(0, 1);
		dispatch_set_bnd(1, 2);
		dispatch_set_bnd(2, 0);
	}
}
void dispatch_add_source(float dt)
{
	glUseProgram(add_source_shader.ID);
	glUniform1f(glGetUniformLocation(add_source_shader.ID, "dt"), dt);
	glDispatchCompute(N, N, 1);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);

	swap_buffer(&all_buffers.read, &all_buffers.write);
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
void dispatch_project(void)
{
	glUseProgram(project_s1_shader.ID);
	glDispatchCompute(N, N, 1);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
	dispatch_set_bnd(0, 0);
	dispatch_set_bnd(1, 0);

	glUseProgram(project_shader.ID);
	for (int i = 0; i < 30; i++) {
		glDispatchCompute(N, N, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		swap_buffer(&all_buffers.tmp, &all_buffers.write);
		dispatch_set_bnd(0, 0);
	}

	glUseProgram(project_s2_shader.ID);
	glDispatchCompute(N, N, 1);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
	dispatch_set_bnd(0, 1);
	dispatch_set_bnd(1, 2);
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
	create_screen_texture(sv.ID);

	glUseProgram(sc.ID);
	all_buffers.read = create_ssbo(&read_buf, sizeof(read_buf), 1, GL_DYNAMIC_DRAW);
	all_buffers.write = create_ssbo(&write_buf, sizeof(write_buf), 2, GL_DYNAMIC_DRAW);
	all_buffers.tmp = create_ssbo(&tmp_buf, sizeof(tmp_buf), 3, GL_DYNAMIC_DRAW);


	ns_t current_t , prev_t = now_ns();
	
	XEvent ev; 
	bool quit = false;
	while (!quit) {
		ns_t frame_start = now_ns();
		while (XPending(d) > 0) {
			XNextEvent(d,&ev);
			quit = handle_Xevents(ev);
		}

		current_t = now_ns();
		float dt = (float)(current_t - prev_t) / 1e9f;
		prev_t = current_t;

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, all_buffers.write.id);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 
						0, 
						sizeof(*all_buffers.write_data), 
						all_buffers.write_data);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		dispatch_add_source(dt);
		dispatch_diffuse(dt);
		dispatch_project();
		dispatch_advect(dt, 0, 1);
		dispatch_advect(dt, 1, 2);
		dispatch_project();

		dispatch_advect(dt, 2, 0);

		//vel_step(N, input_buf.u, input_buf.v, output_buf.u, output_buf.v, VISC, dt);
		//dens_step(N, input_buf.dens, output_buf.dens, input_buf.u, input_buf.v, DIFF, dt);
		
		glUseProgram(sc.ID);


		glDispatchCompute(N+2, N+2, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(sv.ID);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, screen_texture);
		renderQuad();

		glXSwapBuffers(d, w);
		memset(all_buffers.write_data->dens, 
			   0, 
			   sizeof(all_buffers.write_data->dens)
			   );
		memset(all_buffers.write_data->u, 
			   0, 
			   sizeof(all_buffers.write_data->u)
			   );
		memset(all_buffers.write_data->v, 
			   0, 
			   sizeof(all_buffers.write_data->v)
			   );

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

	free(add_source_shader.code);
	free(advect_shader.code);
	free(diffuse_shader.code);
	free(set_bnd_shader.code);
	free(project_shader.code);
	free(project_s1_shader.code);
	free(project_s2_shader.code);
	free(sc.code);
	free(sv.code);
	free(sf.code);
	glXMakeCurrent(d, None, NULL);
	glXDestroyContext(d, glc);
	XDestroyWindow(d, w);
	XCloseDisplay(d);
	return 0;
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

ns_t now_ns(void) 
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ns_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void checkCompileErrors(GLuint shader, char *type) 
{
	GLint success = 0;
	GLchar infoLog[1024] = "";
	if (strcmp(type, "PROGRAM") == 0) {
		glGetProgramiv(shader, GL_LINK_STATUS, &success);
		if (!success) {
			glGetProgramInfoLog(shader, 1024, NULL, infoLog);
			fprintf(stderr, "Error: Program linking err: \n%s\n", infoLog);
		}
	} else {
		glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
		if (success == GL_FALSE) {
			glGetShaderInfoLog(shader, 1024, NULL, infoLog);
			fprintf(stderr, "Error: Shader Compile err: \n%s\n", infoLog);
		}
	}
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

	all_buffers.write_data->dens[IX(x, y)] += force;
	all_buffers.write_data->u[IX(x, y)] += dx * speed * 15.0f;
	all_buffers.write_data->v[IX(x, y)] += dy * speed * 15.0f;
	//u[IX(x, (N+2-y))] = 1.0;
}

Shader load_shader_code(const char *path, GLenum type)
{
	Shader return_shader = { .type = type, .loaded = false };
	FILE *shader_file;

	shader_file = fopen(path, "r");
	if (shader_file == NULL) {
		fprintf(stderr, "Error: load_shader_code failed to open shader file at %s\n", path);
		return return_shader;
	}

	fseek(shader_file, 0, SEEK_END);
	size_t size = ftell(shader_file);
	fseek(shader_file, 0, SEEK_SET);

	return_shader.code_size = size;
	return_shader.code = calloc(size, sizeof(char));
	if (return_shader.code == NULL) {
		fprintf(stderr, "Error: load_shader_code failed to calloc return_shader.code, size %ld\n", size);
		fclose(shader_file);
		return return_shader;
	}

	fread(return_shader.code, 1, size, shader_file);

	return_shader.loaded = true;

	fclose(shader_file);
	return return_shader;
}

void create_c_shader(Shader *s) 
{
	compile_shader(s);
	checkCompileErrors(s->sID, "COMPUTE");

	s->ID = glCreateProgram();
	glAttachShader(s->ID, s->sID);
	glLinkProgram(s->ID);
	checkCompileErrors(s->ID, "PROGRAM");
	glDeleteShader(s->sID);
}

void create_vf_shaders(Shader *v, Shader *f)
{
	compile_shader(v);
	checkCompileErrors(v->sID, "VERTEX");
	compile_shader(f);
	checkCompileErrors(f->sID, "FRAGMENT");

	unsigned int ID = f->ID = v->ID = glCreateProgram();
	glAttachShader(ID, f->sID);
	glAttachShader(ID, v->sID);
	glLinkProgram(ID);
	checkCompileErrors(ID, "PROGRAM");

	glDeleteShader(f->sID);
	glDeleteShader(v->sID);
}

void compile_shader(Shader *s)
{
	s->sID = glCreateShader(s->type);
	glShaderSource(s->sID, 1, (const char *const*)&s->code, NULL);
	glCompileShader(s->sID);
}
GLXFBConfig create_fb_conf(const int *attribList)
{
	int fb_c;
	GLXFBConfig *fbc = glXChooseFBConfig(d, DefaultScreen(d), attribList, &fb_c);

	int best = -1, worst = -1, best_sample = -1;
	int best_samples = -1, worst_samples = 999;
	for (int i = 0; i < fb_c; i++) {
		XVisualInfo *vi = glXGetVisualFromFBConfig(d, fbc[i]);
		if (vi) {
			int sample_buffers, samples;
			glXGetFBConfigAttrib(d, fbc[i], GLX_SAMPLE_BUFFERS, &sample_buffers);
			glXGetFBConfigAttrib(d, fbc[i], GLX_SAMPLES, &samples);

			if (best < 0 || sample_buffers) {
				best = samples;
				best_sample = i;
			}
			if (sample_buffers && samples > best_samples) {
				best_samples = samples;
				best_sample = i;
			}
			if (worst < 0 || !sample_buffers) {
				worst = i;
			}
		}
		XFree(vi);
	}
	GLXFBConfig bfb = fbc[best_sample >= 0 ? best_sample : best];
	XFree(fbc);
	return bfb;
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
	GLXFBConfig fbc = create_fb_conf(fb_att);

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
	glc = glXCreateContextAttribsARB(d, fbc, NULL, True, catt);  //glXCreateContext(d, vi, NULL, GL_TRUE);
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

unsigned int quadVAO = 0;
unsigned int quadVBO;
void renderQuad(void)
{
	if (quadVAO == 0) {
		float quadVertices[] = {
			-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
			-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
		 	 1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
		 	 1.0f, -1.0f, 0.0f, 1.0f, 0.0f
		};

		glGenVertexArrays(1, &quadVAO);
		glGenBuffers(1, &quadVBO);

		glBindVertexArray(quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 
							  3, 
							  GL_FLOAT, 
							  GL_FALSE, 
							  5 * sizeof(float), 
							  (void*)0);

		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 
							  2, 
							  GL_FLOAT, 
							  GL_FALSE, 
							  5 * sizeof(float), 
							  (void*)(3*sizeof(float)));
	}
	glBindVertexArray(quadVAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}

void create_screen_texture(GLuint ID)
{
	glUseProgram(ID);
	glUniform1i(glGetUniformLocation(ID, "tex"), 0);

	glGenTextures(1, &screen_texture);
	glBindTexture(GL_TEXTURE_2D, screen_texture);
	glActiveTexture(GL_TEXTURE0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, WIDTH, HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);

	glBindImageTexture(0, screen_texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
}

struct buffer_id create_ssbo(void *data, size_t sizeof_data, size_t binding, GLenum usage) 
{
	struct buffer_id ssbo = { .binding = binding };
	glGenBuffers(1, &ssbo.id);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo.id);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof_data, data, usage);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ssbo.binding, ssbo.id);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	return ssbo;
}

// TODO refactor this it sucks
int init_shaders(void) 
{
	char path0[] = "/home/emil/Programming/c/screensaver/fromWork/compute.comp";
	char path_ass[] = "/home/emil/Programming/c/screensaver/fromWork/add_source.comp";
	char path_ds[] = "/home/emil/Programming/c/screensaver/fromWork/diffuse.comp";
	char path_av[] = "/home/emil/Programming/c/screensaver/fromWork/advect.comp";
	char path_sb[] = "/home/emil/Programming/c/screensaver/fromWork/set_bnd.comp";
	char path_p[] = "/home/emil/Programming/c/screensaver/fromWork/project.comp";
	char path_p1[] = "/home/emil/Programming/c/screensaver/fromWork/project_step_1.comp";
	char path_p2[] = "/home/emil/Programming/c/screensaver/fromWork/project_step_2.comp";
	char path1[] = "/home/emil/Programming/c/screensaver/fromWork/test.vert";
	char path2[] = "/home/emil/Programming/c/screensaver/fromWork/test.frag";
	diffuse_shader = load_shader_code(path_ds, GL_COMPUTE_SHADER);
	if (!diffuse_shader.loaded) {
		fprintf(stderr, "Error: load_shader_code failed\n");
		return -1;
	}
	add_source_shader = load_shader_code(path_ass, GL_COMPUTE_SHADER);
	if (!add_source_shader.loaded) {
		fprintf(stderr, "Error: load_shader_code failed\n");
		free(diffuse_shader.code);
		return -1;
	}
	set_bnd_shader = load_shader_code(path_sb, GL_COMPUTE_SHADER);
	if (!set_bnd_shader.loaded) {
		fprintf(stderr, "Error: load_shader_code failed\n");
		free(diffuse_shader.code);
		free(add_source_shader.code);
		return -1;
	}
	advect_shader = load_shader_code(path_av, GL_COMPUTE_SHADER);
	if (!advect_shader.loaded) {
		fprintf(stderr, "Error: load_shader_code failed\n");
		free(diffuse_shader.code);
		free(add_source_shader.code);
		free(set_bnd_shader.code);
		return -1;
	}
	sc = load_shader_code(path0, GL_COMPUTE_SHADER);
	if (!sc.loaded) {
		fprintf(stderr, "Error: load_shader_code failed\n");
		free(diffuse_shader.code);
		free(add_source_shader.code);
		free(set_bnd_shader.code);
		free(advect_shader.code);
		return -1;
	}
	sv = load_shader_code(path1, GL_VERTEX_SHADER);
	if (!sv.loaded) {
		fprintf(stderr, "Error: load_shader_code failed\n");
		free(sc.code);
		free(diffuse_shader.code);
		free(add_source_shader.code);
		free(set_bnd_shader.code);
		free(advect_shader.code);
		free(sc.code);
		return -1;
	}
	sf = load_shader_code(path2, GL_FRAGMENT_SHADER);
	if (!sf.loaded) {
		fprintf(stderr, "Error: load_shader_code failed\n");
		free(sv.code);
		free(sc.code);
		free(diffuse_shader.code);
		free(add_source_shader.code);
		free(set_bnd_shader.code);
		free(advect_shader.code);
		return -1;
	}
	project_shader = load_shader_code(path_p, GL_COMPUTE_SHADER);
	if (!project_shader.loaded) {
		fprintf(stderr, "Error: load_shader_code failed\n");
		free(sv.code);
		free(sf.code);
		free(sc.code);
		free(diffuse_shader.code);
		free(add_source_shader.code);
		free(set_bnd_shader.code);
		free(advect_shader.code);
		return -1;
	}
	project_s1_shader = load_shader_code(path_p1, GL_COMPUTE_SHADER);
	if (!project_s1_shader.loaded) {
		fprintf(stderr, "Error: load_shader_code failed\n");
		free(sv.code);
		free(sf.code);
		free(sc.code);
		free(diffuse_shader.code);
		free(add_source_shader.code);
		free(set_bnd_shader.code);
		free(advect_shader.code);
		free(project_shader.code);
		return -1;
	}
	project_s2_shader = load_shader_code(path_p2, GL_COMPUTE_SHADER);
	if (!project_s2_shader.loaded) {
		fprintf(stderr, "Error: load_shader_code failed\n");
		free(sv.code);
		free(sf.code);
		free(sc.code);
		free(diffuse_shader.code);
		free(add_source_shader.code);
		free(set_bnd_shader.code);
		free(advect_shader.code);
		free(project_shader.code);
		free(project_s1_shader.code);
		return -1;
	}

	printf("sc\n");
	create_c_shader(&sc);
	printf("diffuse\n");
	create_c_shader(&diffuse_shader);
	printf("add_source\n");
	create_c_shader(&add_source_shader);
	printf("set_bnd\n");
	create_c_shader(&set_bnd_shader);
	printf("advect\n");
	create_c_shader(&advect_shader);
	printf("project\n");
	create_c_shader(&project_shader);
	printf("project_step_1\n");
	create_c_shader(&project_s1_shader);
	printf("project_step_2\n");
	create_c_shader(&project_s2_shader);
	printf("sv & sf\n");
	create_vf_shaders(&sv, &sf);

	return 0;
}
