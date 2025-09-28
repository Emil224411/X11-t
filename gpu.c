#include "gpu.h"
#include "util.h"

Shader shaders[SHADER_LEN];
Shader *sc, *scc, *sf, *sv;
Shader *add_source_shader, *diffuse_shader;
Shader *advect_shader, *set_bnd_shader;
Shader *project_shader, *project_s1_shader, *project_s2_shader;
Shader *lin_solve_shader;

int init_shaders(void) 
{
	int found = load_all_shaders_from(SHADER_DIR, ANY_SHADER_TYPE, shaders, SHADER_LEN);
	if (found <= 0) {
		fprintf(stderr, "Error: init_shaders: load_all_shaders_from(%s) found <= 0\n", SHADER_DIR);
		return -1;
	}
	for (int i = 0; i < found; i++) {
		if (create_c_shader(&shaders[i], 0)) {
			fprintf(stderr, "Error: init_shaders: create_c_shader failed\n");
			return -1;
		}
	}
	sc = get_shader_from_name(shaders, found, "compute");
	if (sc == NULL) goto err_n_out;
	scc = get_shader_from_name(shaders, found, "clear");
	if (scc == NULL) goto err_n_out;
	sv = get_shader_from_name(shaders, found, "test.vert");
	if (sv == NULL) goto err_n_out;
	sf = get_shader_from_name(shaders, found, "test.frag");
	if (sf == NULL) goto err_n_out;
	add_source_shader = get_shader_from_name(shaders, found, "add_source");
	if (add_source_shader == NULL) goto err_n_out;
	advect_shader = get_shader_from_name(shaders, found, "advect");
	if (advect_shader == NULL) goto err_n_out;
	set_bnd_shader = get_shader_from_name(shaders, found, "set_bnd");
	if (set_bnd_shader == NULL) goto err_n_out;
	project_s1_shader = get_shader_from_name(shaders, found, "project_step_1");
	if (project_s1_shader == NULL) goto err_n_out;
	project_s2_shader = get_shader_from_name(shaders, found, "project_step_2");
	if (project_s2_shader == NULL) goto err_n_out;
	lin_solve_shader = get_shader_from_name(shaders, found, "lin_solve");
	if (lin_solve_shader == NULL) goto err_n_out;

	return 0;
err_n_out:
	fprintf(stderr, "Error: init_shaders get_shader_from_name failed\n");
	return -1;
}

void set_buffer_bind(ssbo_data *x, size_t binding)
{
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, x->id);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, x->id);
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

void dispatch_set_bnd(int n, int b, ssbo_data *x)
{
	GLint tmp;
	get_currently_bound(1, 1, &tmp);

	glUseProgram(set_bnd_shader->ID);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, x->id);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, x->id);

	glUniform1i(glGetUniformLocation(set_bnd_shader->ID, "b"), b);
	glUniform1i(glGetUniformLocation(set_bnd_shader->ID, "n"), n);
	glDispatchCompute((n+2 + 63)/64, 1, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	rebind(1, 1, &tmp);
}

void dispatch_add_source(int n, Buffer *input, Buffer *output, float dt)
{
	GLint prev_bindings[6];
	get_currently_bound(1, 6, prev_bindings);

	glUseProgram(add_source_shader->ID);
	set_buffer_bind(&input->dens, 1);
	set_buffer_bind(&input->u, 2);
	set_buffer_bind(&input->v, 3);
	set_buffer_bind(&output->dens, 4);
	set_buffer_bind(&output->u, 5);
	set_buffer_bind(&output->v, 6);

	glUniform1f(glGetUniformLocation(add_source_shader->ID, "dt"), dt);
	glUniform1i(glGetUniformLocation(add_source_shader->ID, "n"), n);

	glDispatchCompute((n+15)/16, (n+15)/16, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	rebind(1, 6, prev_bindings);
}
void dispatch_advect(int n, int b, ssbo_data *d, ssbo_data *d0, ssbo_data *Vx, ssbo_data *Vy, float dt)
{
	GLint prev_ids[4];
	get_currently_bound(1, 4, prev_ids);

	glUseProgram(advect_shader->ID);
	set_buffer_bind(d, 1);
	set_buffer_bind(d0, 2);
	set_buffer_bind(Vx, 3);
	set_buffer_bind(Vy, 4);
	glUniform1f(glGetUniformLocation(advect_shader->ID, "dt"), dt);
	glUniform1i(glGetUniformLocation(advect_shader->ID, "b"), b);
	glUniform1i(glGetUniformLocation(advect_shader->ID, "n"), n);

	glDispatchCompute((n+15)/16, (n+15)/16, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	dispatch_set_bnd(n, b, d);

	rebind(1, 4, prev_ids);
}
void rebind(GLuint from, GLuint to, GLint *ids)
{
	for (int i = 0; i <= to-from; i++) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ids[i]);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, from+i, ids[i]);
	}
}
void get_currently_bound(GLuint from, GLuint to, GLint *out)
{
	for (int i = 0; i <= to-from; i++) {
		glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, from+i, &out[i]);
	}
}
void dispatch_jacobi(int n, int b, ssbo_data *x, ssbo_data *x0, ssbo_data *x_new, float a, float c, int iter)
{
	GLint tmp[3];
	get_currently_bound(1, 3, tmp);

	glUseProgram(lin_solve_shader->ID);
	set_buffer_bind(x, 1);
	set_buffer_bind(x0, 2);
	set_buffer_bind(x_new, 3);

	glUniform1f(glGetUniformLocation( lin_solve_shader->ID, "a"), a);
	glUniform1f(glGetUniformLocation( lin_solve_shader->ID, "c"), c);
	glUniform1i(glGetUniformLocation( lin_solve_shader->ID, "n"), n);
	glUniform1ui(glGetUniformLocation(lin_solve_shader->ID, "b"), b);

	for (int i = 0; i < iter; i++) {
		glUseProgram(lin_solve_shader->ID);
		glDispatchCompute((n+15)/16, (n+15)/16, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		dispatch_set_bnd(n, b, x_new);
		swap_buffer(x, x_new);
	}
	rebind(1, 3, tmp);
}

void dispatch_diffuse(int n, int b, ssbo_data *input, ssbo_data *output, ssbo_data *tmp, float diff, float dt)
{
	float a = dt * diff * n * n;
	dispatch_jacobi(n, b, output, input, tmp, a, 1+4*a, 100);
}

//step 1: p and div = output, lin_solve?, step 3: p = u_in, Vx, Vy = u_out, v_out
void dispatch_project(int n, ssbo_data *Vx, ssbo_data *Vy, ssbo_data *p, ssbo_data *div, ssbo_data *tmp)
{
	GLint prev_ids[9];
	get_currently_bound(1, 9, prev_ids);

	set_buffer_bind(div, 1);
	set_buffer_bind(p, 2);
	set_buffer_bind(Vx, 3);
	set_buffer_bind(Vy, 4);
	glUseProgram(project_s1_shader->ID);
	glUniform1i(glGetUniformLocation(project_s1_shader->ID, "n"), n);
	glDispatchCompute((n+15)/16, (n+15)/16, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	dispatch_set_bnd(n, 0, div);
	dispatch_set_bnd(n, 1,   p);

	dispatch_jacobi(n, 0, p, div, tmp, 1.0, 4.0, 100);

	glUseProgram(project_s2_shader->ID);
	glUniform1i(glGetUniformLocation(project_s2_shader->ID, "n"), n);
	glDispatchCompute((n+15)/16, (n+15)/16, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	dispatch_set_bnd(n, 1, Vx);
	dispatch_set_bnd(n, 2, Vy);

	rebind(1, 9, prev_ids);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void dispatch_clear(int n)
{
	GLint prev_ids[9];
	get_currently_bound(1, 9, prev_ids);

	glUseProgram(scc->ID);
	glUniform1i(glGetUniformLocation(project_s1_shader->ID, "n"), n);
	glDispatchCompute((n+31)/32, (n+31)/32, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	rebind(1, 9, prev_ids);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

