#version 150

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;

out vec2 uv;

void main()
{
	const float particleSize = 0.2;

	vec4 point = gl_in[0].gl_Position;

	vec2 bottomLeft = point.xy + vec2(-0.5, -0.5) * particleSize;
	gl_Position = projectionMatrix * modelViewMatrix * vec4(bottomLeft, point.zw);
	uv = vec2(0.0, 0.0);
	EmitVertex();

	vec2 topLeft = point.xy + vec2(-0.5, 0.5) * particleSize;
	gl_Position = projectionMatrix * modelViewMatrix * vec4(topLeft, point.zw);
	uv = vec2(0.0, 1.0);
	EmitVertex();

	vec2 bottomRight = point.xy + vec2(0.5, -0.5) * particleSize;
	gl_Position = projectionMatrix * modelViewMatrix * vec4(bottomRight, point.zw);
	uv = vec2(1.0, 0.0);
	EmitVertex();

	vec2 topRight = point.xy + vec2(0.5, 0.5) * particleSize;
	gl_Position = projectionMatrix * modelViewMatrix * vec4(topRight, point.zw);
	uv = vec2(1.0, 1.0);
	EmitVertex();

	EndPrimitive();
}