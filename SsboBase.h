#pragma once

/*-----------------------------------------------------------------------------------------------
Description:
    Defines the constructor, which gives the members zero values, and the destructor, which 
    deletes any allocated buffers.  
Creator: John Cox, 9-20-2016
-----------------------------------------------------------------------------------------------*/
class SsboBase
{
public:
    SsboBase();
    virtual ~SsboBase();

    // derived class needs customized Init(...) function to initialize member values

    virtual void ConfigureCompute(unsigned int computeProgramId) = 0;
    virtual void ConfigureRender(unsigned int renderProgramId) = 0;

    unsigned int VaoId() const;
    unsigned int BufferId() const;
    unsigned int DrawStyle() const;
    unsigned int NumVertices() const;

protected:
    // can't be private because the derived classes need to set them
    
    bool _hasBeenInitialized;

    // save on the large header inclusion of OpenGL and write out these primitive types instead 
    // of using the OpenGL typedefs
    // Note: IDs are GLuint (unsigned int), draw style is GLenum (unsigned int), GLushort is 
    // unsigned short.
    
    unsigned int _vaoId;
    unsigned int _bufferId;
    unsigned int _drawStyle;    // GL_TRIANGLES, GL_LINES, etc.
    unsigned int _numVertices;
};
