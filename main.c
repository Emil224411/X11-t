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

#define WIDTH 1002
#define HEIGHT 1002

#define DIFF 0.0001f
#define VISC 0.0001f

typedef long long ns_t;

typedef struct {
	size_t type;
	unsigned int sID, ID;
	char *code;
	size_t code_size;
	bool loaded;
} Shader;

struct ssbo_data { 
	float dt; 
	float dens[SIZE]; 
};

Window w;
Display *d;
GLXContext glc;
Atom wm_delete_window;

Shader sc, sf, sv;
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
GLuint create_ssbo(void *data, size_t sizeof_data, size_t index, GLenum usage);

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
	struct ssbo_data some_date = { 1.0, {0}};
	GLuint ssboid = create_ssbo(&some_date, sizeof(some_date), 1, GL_DYNAMIC_DRAW);


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

		glUseProgram(sc.ID);
		glDispatchCompute(WIDTH, HEIGHT, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(sv.ID);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, screen_texture);
		renderQuad();

		glXSwapBuffers(d, w);

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
	int x = e.x;
	int y = e.y;

	int px = prev_x;
	int py = prev_y;
	int dx = x - px;
	int dy = y - py;
	
	int dt = e.time - prev_time;
	if (dt <= 0) dt = 1;
	
	float speed = sqrtf(dx*dx + dy*dy) / (float)dt;
	float force = speed * 200;
	
	dens_prev[IX(x, y)] += force;
	u_prev[IX(x, y)] += dx * speed * 15.0f;
	v_prev[IX(x, y)] += dy * speed * 15.0f;
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

int init(void)
{
	d = XOpenDisplay(NULL);
	if (!d) { 
		fprintf(stderr,"Error: XOpenDisplay failed\n"); 
		return -1; 
	}
	Window root = DefaultRootWindow(d);
	GLint att[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };

	XVisualInfo *vi = glXChooseVisual(d, 0, att);
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

	glc = glXCreateContext(d, vi, NULL, GL_TRUE);
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
	printf("Info: GL work group count: %d * %d * %d = %ld\n",
											maxX,maxY,maxZ, (long)maxX * maxY * maxZ);
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

GLuint create_ssbo(void *data, size_t sizeof_data, size_t index, GLenum usage) 
{
	GLuint ssbo;
	glGenBuffers(1, &ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof_data, data, usage);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, index, ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	return ssbo;
}

int init_shaders(void) 
{
	char path0[] = "/home/emil/Programming/c/screensaver/compute.comp";
	char path1[] = "/home/emil/Programming/c/screensaver/test.vert";
	char path2[] = "/home/emil/Programming/c/screensaver/test.frag";
	sc = load_shader_code(path0, GL_COMPUTE_SHADER);
	if (!sc.loaded) {
		fprintf(stderr, "Error: load_shader_code failed\n");
		return -1;
	}
	sv = load_shader_code(path1, GL_VERTEX_SHADER);
	if (!sv.loaded) {
		fprintf(stderr, "Error: load_shader_code failed\n");
		free(sc.code);
		return -1;
	}
	sf = load_shader_code(path2, GL_FRAGMENT_SHADER);
	if (!sf.loaded) {
		fprintf(stderr, "Error: load_shader_code failed\n");
		free(sv.code);
		free(sc.code);
		return -1;
	}

	create_c_shader(&sc);
	create_vf_shaders(&sv, &sf);

	return 0;
}
