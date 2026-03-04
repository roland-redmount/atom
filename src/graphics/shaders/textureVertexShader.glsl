//
// Vertex shader for 2D coordinate with texture, {x y tx ty}
//

#version 330 core
in vec4 vertex; 			//  {x y tx ty}
out vec2 textureCoords;		//  {tx ty}

uniform vec2 scaling;

void main()
{
    gl_Position = vec4(vertex.xy * scaling, 0.0, 1.0);
    textureCoords = vertex.zw;
} 
