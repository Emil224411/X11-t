#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoords;

uniform float scale;
out vec2 TexCoords;
	
void main()
{
	TexCoords = aTexCoords / scale;
    gl_Position = vec4(aPos, 1.0);
}


