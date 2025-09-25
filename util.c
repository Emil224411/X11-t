#include "util.h"

unsigned int screen_texture;
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
	char path[512] = PROG_DIR;
	Shader ret = { .loaded = false };

	DIR *d = opendir(path);
	if (!d) {
		fprintf(stderr, "Error: load_shader_from_name: Failed to open dir at \"%s\". errno: %s\n", 
				path, strerror(errno));
		return ret;
	}

	while ( (de = readdir(d)) ) {
		if (strncmp(de->d_name, name, 256) == 0) {
			printf("found file with name %s\n", de->d_name);
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

	compile_shader(&ret);
	return ret;
}
// if of_type == 0 then load all vertex, fragment and compute shaders found. 
int load_all_shaders_from(const char *dir, GLenum of_type, Shader *save_to, size_t len)
{
	struct dirent *de;
	char path[512] = PROG_DIR;

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
			strncat(path, save_to[i].file_name, 256);
			save_to[i].code = load_shader_code(path);
			if (!save_to[i].code) goto err_n_out;
			strncpy(path, "", 512);
			i++;
		} 
	}
	closedir(d);
	return 0;
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
	return NULL;
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

void create_c_shader(Shader *s, GLuint ID) 
{
	printf("create_c_shader: %s\n", s->file_name);
	compile_shader(s);

	s->ID = ID > 0 ? ID : glCreateProgram();
	glAttachShader(s->ID, s->sID);
	glLinkProgram(s->ID);
	checkCompileErrors(s->ID, "PROGRAM");
	glDeleteShader(s->sID);
}

void create_vf_shaders(Shader *v, Shader *f)
{
	printf("create_vf_shader: %s, %s\n", v->file_name, f->file_name);
	compile_shader(v);
	compile_shader(f);

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
	checkCompileErrors(s->sID, (s->type == GL_COMPUTE_SHADER) ? "COMPUTE" : 
							   (s->type == GL_VERTEX_SHADER)  ? "VERTEX"  : "FRAGMENT");
}

GLXFBConfig create_fb_conf(Display *d, const int *attribList)
{
	int fb_c;
	GLXFBConfig *fbc = glXChooseFBConfig(d, DefaultScreen(d), attribList, &fb_c);

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
