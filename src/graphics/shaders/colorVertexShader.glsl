//
// Vertex shader for 2D points with RGB color {x y r g b}
//

#version 150 core

in vec2 vertexPosition;
in vec3 vertexColor;

out vec3 sharedColor;

uniform vec2 scaling;

void main()
{
	// write to built-in gl_Position variable, z = 0
	gl_Position = vec4(vertexPosition * scaling, 0.0, 1.0);

	// pass the vertex color on to the next shader
	sharedColor = vertexColor;
}

