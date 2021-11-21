#version 150

uniform sampler2D particleTexture;

in vec2 uv;
out vec4 outColor;

void main()
{
	vec4 pxColor = texture(particleTexture, uv);
	outColor = pxColor;
}