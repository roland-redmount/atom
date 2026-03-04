//
// Fragment shader
//
// This processes color at each pixel (fragment)
//

#version 150 core

// input the shared color variable
in vec3 sharedColor;
// resulting color of the fragment
out vec4 fragmentColor;

void main()
{
    fragmentColor = vec4(sharedColor, 1.0);
}
