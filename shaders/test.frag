#version 460 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D tex;

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{             
	vec3 dens = texture(tex, TexCoords).rgb;
    FragColor = vec4(dens, 1.0);
	/*
	float rnd = hash(gl_FragCoord.xy);
	if (rnd < dens.r) {
    	FragColor = vec4(dens, 1.0);
	} else {
		discard;
	}
	*/
}


