#include "util.h"
#include "gpu.h"
#include <stdio.h>

unsigned int screen_texture;
unsigned int quadVAO = 0;
unsigned int quadVBO;
// https://learnopengl.com/Guest-Articles/2022/Compute-Shaders/Introduction
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

void create_screen_texture(GLuint ID, size_t width, size_t height)
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
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);

	glBindImageTexture(0, screen_texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
}

// add error check Mr. lazy
ssbo_data create_ssbo(size_t binding, GLenum usage) 
{
	ssbo_data ssbo = { .binding = binding };
	glGenBuffers(1, &ssbo.id);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo.id);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(ssbo.data), NULL, usage);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ssbo.binding, ssbo.id);

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	return ssbo;
}

GLenum get_shader_type_from(const char *file)
{
	const char *extention = strrchr(file, '.');
	if (!extention) return 0;

	if (strcmp(extention, ".comp") == 0 || strcmp(extention, ".cs") == 0)
		return GL_COMPUTE_SHADER;
	if (strcmp(extention, ".vert") == 0 || strcmp(extention, ".vs") == 0)
		return GL_VERTEX_SHADER;
	if (strcmp(extention, ".frag") == 0 || strcmp(extention, ".fs") == 0)
		return GL_FRAGMENT_SHADER;

	return 0;
}

// All shader files should be in SHADER_DIR/PROG_DIR
Shader load_shader_from_name(const char *name, GLenum type)
{
	struct dirent *de;
	char path[512] = SHADER_DIR;
	Shader ret = { .loaded = false };

	DIR *d = opendir(path);
	if (!d) {
		fprintf(stderr, "Error: load_shader_from_name: Failed to open dir at \"%s\". errno: %s\n", 
				path, strerror(errno));
		return ret;
	}

	while ( (de = readdir(d)) ) {
		if (strncmp(de->d_name, name, 256) == 0) {
			fprintf(stdout, "Info: found shader file with name %s in %s\n", de->d_name, path);
			strncpy(ret.file_name, de->d_name, 256);
			strncat(path, ret.file_name, 256);
			break;
		}
	}
	closedir(d);
	
	ret.code = load_shader_code(path);
	if (!ret.code) {
		fprintf(stderr, "Error: load_shader_from_name: load_shader_code(%s) returned NULL\n", path);
	}

	ret.type = type ? type : get_shader_type_from(ret.file_name);
	if (!ret.type) 
		fprintf(stderr, "Error: load_shader_from_name: failed to get shader type\n");
	else 
		ret.loaded = true;
	return ret;
}

Shader load_and_compile_shader_from(const char *name, GLenum type)
{
	Shader ret = load_shader_from_name(name, type);
	if (!ret.loaded) {
		fprintf(stderr, "Error: load_and_compile_shader_from: load_shader_from_name(%s) failed\n", name);
		return ret;
	}

	if (ret.type == GL_COMPUTE_SHADER) create_c_shader(&ret, 0);
	else fprintf(stderr, "Info: load_and_compile_shader_from: only supports compute shaders. "
						 "shader with name \"%s\" not compiled\n", name);
	return ret;
}
// returns the amount of textures added to save_to. 
// if of_type == 0 then load all vertex, fragment and compute shaders found. 
int load_all_shaders_from(const char *dir, GLenum of_type, Shader *save_to, size_t len)
{
	struct dirent *de;
	char path[512] = SHADER_DIR;

	DIR *d = opendir(path);
	if (!d) {
		fprintf(stderr, "Error: load_all_shaders_from: Failed to open dir at \"%s\". errno: %s\n", 
				path, strerror(errno));
		return -1;
	}
	int i = 0;
	while ((de = readdir(d)) && i < len) {
		GLenum file_type = get_shader_type_from(de->d_name);
		if ((file_type == of_type && of_type != 0) || (of_type == 0 && file_type != 0)) {
			save_to[i].type = file_type;
			strncpy(save_to[i].file_name, de->d_name, 256);
			//fprintf(stdout, "Info: load_all_shaders_from: found \"%s\" at \"%s\"\n", de->d_name, path);
			strncat(path, save_to[i].file_name, 256);
			save_to[i].code = load_shader_code(path);
			if (!save_to[i].code) goto err_n_out;
			strncpy(path, SHADER_DIR, 512);
			i++;
		} 
	}
	closedir(d);
	return i;
err_n_out:
		fprintf(stderr, "Error: load_all_shaders_from: load_shader_code(%s) returned NULL\n", path);
		for (; i >= 0; i--) {
			free(save_to[i].code);
			save_to[i].loaded = false;
		}
		return -1;
}

Shader *get_shader_from_name(Shader *look_in, size_t len, const char *look_for)
{
	for (int i = 0; i < len; i++) {
		if (strncmp(look_in[i].file_name, look_for, strlen(look_for)) == 0) 
			return &look_in[i];
	}
	fprintf(stdout, "Warning: get_shader_from_name(%s) didnt find any shaders\n", 
												   look_for);
	return NULL;
}

ns_t now_ns(void) 
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ns_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int checkCompileErrors(GLuint ID, GLenum pname) 
{
	GLint success = 0;
	GLchar infoLog[1024] = "";
	if (pname == GL_LINK_STATUS) {
		glGetProgramiv(ID, pname, &success);
		if (!success) {
			glGetProgramInfoLog(ID, 1024, NULL, infoLog);
			fprintf(stderr, "Error: Program Linking: \n%s\n", 
													infoLog);
			return -1;
		}
	} else if (pname == GL_COMPILE_STATUS) {
		glGetShaderiv(ID, pname, &success);
		if (!success) {
			glGetShaderInfoLog(ID, 1024, NULL, infoLog);
			fprintf(stderr, "Error: Shader Compile: \n%s\n", 
													infoLog);
			return -1;
		}
	} else {
		fprintf(stderr, "Warning: checkCompileErrors: pname not supported\n");
		return -1;
	}
	return 0;
}

char *load_shader_code(const char *path)
{
	size_t size;
	char *code;
	FILE *shader_file = fopen(path, "r");
	if (shader_file == NULL) {
		fprintf(stderr, "Error: load_shader_code failed to open shader file at %s\n", path);
		return NULL;
	}

	fseek(shader_file, 0, SEEK_END);
	size = ftell(shader_file);
	fseek(shader_file, 0, SEEK_SET);

	code = calloc(size, sizeof(char));
	if (code == NULL) {
		fprintf(stderr, "Error: load_shader_code failed to calloc return_shader.code, size %ld\n", size);
		fclose(shader_file);
		return NULL;
	}

	fread(code, 1, size, shader_file);

	fclose(shader_file);
	return code;
}

int create_c_shader(Shader *s, GLuint ID) 
{
	if (s->type != GL_COMPUTE_SHADER) {
		fprintf(stderr, "Warning: create_c_shader: \"%s\" is not compute shader\n",
													s->file_name);
		return 0;
	}
	if (compile_shader(s)) {
		fprintf(stderr, "Error: create_c_shader: Failed to compile \"%s\"\n",
																s->file_name);
		return -1;
	}

	s->ID = ID > 0 ? ID : glCreateProgram();
	glAttachShader(s->ID, s->sID);
	glLinkProgram(s->ID);
	if (checkCompileErrors(s->ID, GL_LINK_STATUS)) {
		fprintf(stderr, "Error: create_c_shader: Failed to link \"%s\" to %d\n",
														s->file_name, s->ID);
		return -1;
	}
	// deleting shader but saving the id not good but i think i allready made a 
	// comment some were else saying the same thing
	glDeleteShader(s->sID);
	return 0;
}

int create_vf_shaders(Shader *v, Shader *f)
{
	if (compile_shader(v)) {
		fprintf(stderr, "Error: create_vf_shaders: Failed to compile \"%s\"",
																v->file_name);
		return -1;
	}
	if (compile_shader(f)) {
		fprintf(stderr, "Error: create_vf_shaders: Failed to compile \"%s\"",
																f->file_name);
		return -1;
	}

	unsigned int ID = f->ID = v->ID = glCreateProgram();
	glAttachShader(ID, f->sID);
	glAttachShader(ID, v->sID);
	glLinkProgram(ID);
	if (checkCompileErrors(ID, GL_LINK_STATUS)) {
		fprintf(stderr, "Error: Failed to link vertex/fragment shader to %d\n",
																		 ID);
		return -1;
	}
	// same as create_c_shader deleting but saving sID
	glDeleteShader(f->sID);
	glDeleteShader(v->sID);
	return 0;
}

int compile_shader(Shader *s)
{
	s->sID = glCreateShader(s->type);
	// maby should check for error here?
	glShaderSource(s->sID, 1, (const char *const*)&s->code, NULL);
	glCompileShader(s->sID);
	return checkCompileErrors(s->sID, GL_COMPILE_STATUS);
}

// tbh AI wrote this function and i dont know why it does what it does
GLXFBConfig create_fb_conf(Display *d, const int *attribList)
{
	int fb_c;
	GLXFBConfig *fbc = glXChooseFBConfig(d, DefaultScreen(d), attribList, &fb_c);

	// why all -1?????? idk 
	int best = -1, worst = -1, best_sample = -1;
	int best_samples = -1;
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
			// why save the worst option??? Mr. robt
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
