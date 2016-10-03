#include "PolygonSsbo.h"

#include "glload/include/glload/gl_4_4.h"

/*-----------------------------------------------------------------------------------------------
Description:
    Calls the base class to give members initial values (zeros).
Parameters: None
Returns:    None
Creator: John Cox, 9-8-2016
-----------------------------------------------------------------------------------------------*/
PolygonSsbo::PolygonSsbo() :
    SsboBase()
{
}

/*-----------------------------------------------------------------------------------------------
Description:
    Does nothing.  Exists to be declared virtual so that the base class' destructor is called 
    upon object death.
Parameters: None
Returns:    None
Creator: John Cox, 9-8-2016
-----------------------------------------------------------------------------------------------*/
PolygonSsbo::~PolygonSsbo()
{
}

/*-----------------------------------------------------------------------------------------------
Description:
    Given a collection of polygon faces, this allocates an SSBO, dumps the face data into them, 
    and sets up the vertex attribtues for the render shader.

    Note: The compute shader ID is not necessary to set up the SSBO.
Parameters: 
    faceCollection  Self-explanatory.
    computeProgramId    Required for binding the compute shader's face buffer to the SSBO.
    renderProgramId The rendering shader that will be drawing this polygon.
Returns:    None
Creator: John Cox, 9-25-2016
-----------------------------------------------------------------------------------------------*/
void PolygonSsbo::Init(const std::vector<PolygonFace> &faceCollection, 
    unsigned int computeProgramId, unsigned int renderProgramId)
{
    _drawStyle = GL_LINES;

    // two vertices per face
    _numVertices = faceCollection.size() * 2;

    // unlike the VAOs, the compute shader program is not required for buffer creation, but it 
    // is required for buffer binding
    glGenBuffers(1, &_bufferId);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bufferId);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(PolygonFace) * faceCollection.size(), faceCollection.data(), GL_STATIC_DRAW);
    
    // see the corresponding area in ParticleSsbo::Init(...) for explanation
    GLuint ssboBindingPointIndex = 13;   // or 1, or 5, or 17, or wherever IS UNUSED
    GLuint storageBlockIndex = glGetProgramResourceIndex(computeProgramId, GL_SHADER_STORAGE_BLOCK, "FaceBuffer");
    glShaderStorageBlockBinding(computeProgramId, storageBlockIndex, ssboBindingPointIndex);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ssboBindingPointIndex, _bufferId);

    // cleanup
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // the render program is required for vertex attribute initialization or else the program WILL crash at runtime
    glUseProgram(renderProgramId);
    glGenVertexArrays(1, &_vaoId);
    glBindVertexArray(_vaoId);

    // the vertex array attributes only work on whatever is bound to the array buffer, so bind 
    // shader storage buffer to the array buffer, set up the vertex array attributes, and the 
    // VAO will then use the buffer ID of whatever is bound to it
    glBindBuffer(GL_ARRAY_BUFFER, _bufferId);
    // do NOT call glBufferData(...) because it was called earlier for the shader storage buffer

    // each face is made up of two vertices, so set the attribtues for the vertices
    unsigned int vertexArrayIndex = 0;
    unsigned int bufferStartOffset = 0;
    unsigned int bytesPerStep = sizeof(MyVertex);

    // position
    GLenum itemType = GL_FLOAT;
    unsigned int numItems = sizeof(MyVertex::_position) / sizeof(float);
    glEnableVertexAttribArray(vertexArrayIndex);
    glVertexAttribPointer(vertexArrayIndex, numItems, itemType, GL_FALSE, bytesPerStep, (void *)bufferStartOffset);

    // normal
    itemType = GL_FLOAT;
    numItems = sizeof(MyVertex::_normal) / sizeof(float);
    bufferStartOffset += sizeof(MyVertex::_position);
    vertexArrayIndex++;
    glEnableVertexAttribArray(vertexArrayIndex);
    glVertexAttribPointer(vertexArrayIndex, numItems, itemType, GL_FALSE, bytesPerStep, (void *)bufferStartOffset);

    // cleanup
    glBindVertexArray(0);   // unbind this BEFORE the array
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);    // render program
}

