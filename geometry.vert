#version 440

layout (location = 0) in vec4 pos;

uniform mat4 transformMatrixWindowSpace;

void main()
{
    // nothing special here
    // Note: Just giving the 2D vectors all the same Z value (-1 was arbitrarily chosen; nothing 
    // special) and giving them a 1.0f w value so that they can be translated by a 4D matrix.
	
    
    
    //gl_Position = translateMatrixWindowSpace * vec4(pos, -1.0f, 1.0f);
    gl_Position = transformMatrixWindowSpace * pos;
    //gl_Position = vec4(pos, -1.0f, 1.0f);
    //gl_Position = pos;
}

