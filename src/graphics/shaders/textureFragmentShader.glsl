//
// Vertex shader for texture sampling for font rendering
// uses the 
// 

#version 330 core

in vec2 textureCoords;				// {tx ty}
out vec4 fragmentColor;				// {r g b a}

uniform sampler2D image;
uniform vec3 textColor;		// use to set the text color {r g b}

void main()
{
	// set alpha value from texture red channel
	vec4 sampled = vec4(1.0, 1.0, 1.0, texture(image, textureCoords).r);
	fragmentColor = vec4(textColor, 1.0) * sampled;

	// NOTE: why not just this ?
	// color = vec4(textColor, texture(image, TexCoords).r);
}
