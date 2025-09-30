#ifndef GPU_H
#define GPU_H
#ifndef SHADER_DIR
#error Please define SHADER_DIR
#endif
#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <time.h>

#include "util.h"

#define SHADER_LEN 14
extern Shader shaders[SHADER_LEN];
extern Shader *sc, *sf, *sv;
extern Shader *text_vert, *text_frag;
extern Shader *add_source_shader, *diffuse_shader;
extern Shader *advect_shader, *set_bnd_shader;
extern Shader *project_shader, *project_s1_shader, *project_s2_shader;
extern Shader *lin_solve_shader;

int init_shaders(void);
void dispatch_jacobi(int n, int b, ssbo_data *x, ssbo_data *x0, ssbo_data *x_new, float a, float c, int iter);
/*
 * should be okay to read and write from the same buffer 
 * since the edges are not being read from only writen to
 */ 
void dispatch_set_bnd(int n, int b, ssbo_data *x);
/*
 * diffuse is okay to do in parallel for density and velosity 
 */
void dispatch_diffuse(int n, int b, ssbo_data *input, ssbo_data *output, ssbo_data *tmp, float diff, float dt);
/*
 * advect is run on either Vx, Vy or density since 
 * they are reling on the output of the previus advection
 */
void  dispatch_advect(int n, int b, ssbo_data *input, ssbo_data *output, ssbo_data *Vx, ssbo_data *Vy, float dt);
/*
 * add_source done in parallel for Vx, Vy and density since
 * they don't read from each other
 */
void dispatch_add_source(int n, Buffer *input, Buffer *output, float dt);
void dispatch_project(int n, ssbo_data *Vx, ssbo_data *Vy, ssbo_data *p, ssbo_data *div, ssbo_data *tmp);

void dispatch_clear(int n);
void rebind(GLuint from, GLuint to, GLint *ids);
void get_currently_bound(GLuint from, GLuint to, GLint *out);
void set_buffer_bind(ssbo_data *x, size_t binding);

#endif//GPU_H
