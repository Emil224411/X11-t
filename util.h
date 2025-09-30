#ifndef UTIL_H
#define UTIL_H
//#include "gpu.h"
#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>

#define N 250
#define SIZE ((N+2) * (N+2))
#define IX(x, y) ((x)+(N+2)*(y))

#define ANY_SHADER_TYPE 0
#define  MAX_BUFFER_AMOUNT 256

typedef long long ns_t;

typedef struct {
	bool on_gpu;
	size_t width, height;
	char win_title[256];
	int vsync;

	float diff, visc;
	float dt;
	size_t n;
} Settings;

typedef struct {
	size_t type;
	unsigned int sID, ID;
	char file_name[256];
	char *code;
	bool loaded;
} Shader;

typedef struct { 
	GLuint id;
	size_t binding;
	float data[SIZE]; 
	void *ptr;
	bool mapped;
} ssbo_data;
extern GLuint all_buffer_ids[MAX_BUFFER_AMOUNT];
extern size_t all_buffer_used;

typedef struct {
	ssbo_data dens;
	ssbo_data u;
	ssbo_data v;
} Buffer;

struct buffers {
	Buffer input;
	Buffer output;
	Buffer tmp;
};

extern unsigned int screen_texture;
extern unsigned int quadVAO;
extern unsigned int quadVBO;

void renderQuad(void);
int compile_shader(Shader *s);
char *load_shader_code(const char *path);
int create_c_shader(Shader *s, GLuint ID);
int create_vf_shaders(Shader *v, Shader *f);
GLenum get_shader_type_from(const char *file);
int checkCompileErrors(GLuint ID, GLenum pname);
ssbo_data create_ssbo(size_t binding, GLenum usage);
Shader load_shader_from_name(const char *name, GLenum type);
GLXFBConfig create_fb_conf(Display *d, const int *attribList);
void create_screen_texture(GLuint ID, size_t width, size_t height);
Shader load_and_compile_shader_from(const char *name, GLenum type);
Shader *get_shader_from_name(Shader *look_in, size_t len, const char *look_for);
int load_all_shaders_from(const char *dir, GLenum of_type, Shader *save_to, size_t len);


#endif//UTIL_H
