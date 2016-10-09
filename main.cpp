// the OpenGL version include also includes all previous versions
// Build note: Due to a minefield of preprocessor build flags, the gl_load.hpp must come after 
// the version include.
// Build note: Do NOT mistakenly include _int_gl_4_4.h.  That one doesn't define OpenGL stuff 
// first.
// Build note: Also need to link opengl32.lib (unknown directory, but VS seems to know where it 
// is, so don't put in an "Additional Library Directories" entry for it).
// Build note: Also need to link glload/lib/glloadD.lib.
#include "glload/include/glload/gl_4_4.h"
#include "glload/include/glload/gl_load.hpp"

// Build note: Must be included after OpenGL code (in this case, glload).
// Build note: Also need to link freeglut/lib/freeglutD.lib.  However, the linker will try to 
// find "freeglut.lib" (note the lack of "D") instead unless the following preprocessor 
// directives are set either here or in the source-building command line (VS has a
// "Preprocessor" section under "C/C++" for preprocessor definitions).
// Build note: Also need to link winmm.lib (VS seems to know where it is, so don't put in an 
// "Additional Library Directories" entry).
#define FREEGLUT_STATIC
#define _LIB
#define FREEGLUT_LIB_PRAGMAS 0
#include "freeglut/include/GL/freeglut.h"

// this linking approach is very useful for portable, crude, barebones demo code, but it is 
// better to link through the project building properties
#pragma comment(lib, "glload/lib/glloadD.lib")
#pragma comment(lib, "opengl32.lib")            // needed for glload::LoadFunctions()
#pragma comment(lib, "freeglut/lib/freeglutD.lib")
#ifdef WIN32
#pragma comment(lib, "winmm.lib")               // Windows-specific; freeglut needs it
#endif

// apparently the FreeType lib also needs a companion file, "freetype261d.pdb"
#pragma comment (lib, "freetype-2.6.1/objs/vc2010/Win32/freetype261d.lib")

// for printf(...)
#include <stdio.h>

// for basic OpenGL stuff
#include "OpenGlErrorHandling.h"
#include "ShaderStorage.h"

// for particles, where they live, and how to update them
#include "glm/vec2.hpp"
#include "ParticleSsbo.h"
#include "PolygonSsbo.h"
#include "ParticlePolygonComputeUpdater.h"

// for moving the shapes around in window space
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

// for the frame rate counter
#include "FreeTypeEncapsulated.h"
#include "Stopwatch.h"

Stopwatch gTimer;
FreeTypeEncapsulated gTextAtlases;

// in a bigger program, uniform locations would probably be stored in the same place as the 
// shader programs
GLint gUnifLocGeometryTransform;

// ??stored in scene??
ParticleSsbo gParticleBuffer;
PolygonSsbo gPolygonFaceBuffer;

// in a bigger program, this would somehow be encapsulated and associated with both the circle
// geometry and the circle particle region, and ditto for the polygon
glm::mat4 gRegionTransformMatrix;

// in a bigger program, ??where would particle stuff be stored??
IParticleEmitter *gpParticleEmitterPoint;
//IParticleEmitter *gpParticleEmitterBar;
ParticlePolygonComputeUpdater *gpParticleComputeUpdater;

// divide between the circle and the polygon regions
// Note: 
// - 10,000 particles => ~60 fps on my computer
// - 15,000 particles => 30-40 fps on my computer
const unsigned int MAX_PARTICLE_COUNT = 15000;



/*-----------------------------------------------------------------------------------------------
Description:
    Encapsulates the rotating of a 2D vector by -90 degrees (+90 degrees not used in this demo).
Parameters:
    v   A const 2D vector.
Returns:
    A 2D vector rotated -90 degrees from the provided one.
Creator:    John Cox (7-2-2016)
-----------------------------------------------------------------------------------------------*/
static glm::vec2 RotateNeg90(const glm::vec2 &v)
{
    return glm::vec2(v.y, -(v.x));
}

/*-----------------------------------------------------------------------------------------------
Description:
    Manually (it's a demo, so eh?) generates a polygon centered around the window space origin 
    (0,0) with corners at:
    - vec2(-0.25f, -0.5f);
    - vec2(+0.25f, -0.5f);
    - vec2(+0.5f, +0.25f);
    - vec2(-0.5f, +0.25f);
Parameters:
    polygonFaceCollection   A pointer to the structure that needs to be filled out.
Returns:    None
Creator:    John Cox (9-25-2016)
-----------------------------------------------------------------------------------------------*/
static void GeneratePolygonRegion(std::vector<PolygonFace> *polygonFaceCollection)
{
    glm::vec2 p1(-0.5f, -0.75f);
    glm::vec2 p2(+0.5f, -0.75f);
    glm::vec2 p3(+0.75f, +0.5f);
    glm::vec2 p4(-0.75f, +0.5f);
    glm::vec2 n1(RotateNeg90(p2 - p1));
    glm::vec2 n2(RotateNeg90(p3 - p2));
    glm::vec2 n3(RotateNeg90(p4 - p3));
    glm::vec2 n4(RotateNeg90(p1 - p4));
    PolygonFace face1(MyVertex(p1, n1), MyVertex(p2, n1));
    PolygonFace face2(MyVertex(p2, n2), MyVertex(p3, n2));
    PolygonFace face3(MyVertex(p3, n3), MyVertex(p4, n3));
    PolygonFace face4(MyVertex(p4, n4), MyVertex(p1, n4));

    polygonFaceCollection->push_back(face1);
    polygonFaceCollection->push_back(face2);
    polygonFaceCollection->push_back(face3);
    polygonFaceCollection->push_back(face4);
}


/*-----------------------------------------------------------------------------------------------
Description:
    Governs window creation, the initial OpenGL configuration (face culling, depth mask, even
    though this is a 2D demo and that stuff won't be of concern), the creation of geometry, and
    the creation of a texture.
Parameters: None
Returns:    None
Creator:    John Cox (3-7-2016)
-----------------------------------------------------------------------------------------------*/
void Init()
{
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glDepthRange(0.0f, 1.0f);

    // inactive particle Z = -0.6   alpha = 0
    // active particle Z = -0.7     alpha = 1
    // polygon fragment Z = -0.8    alpha = 1
    // Note: The blend function is done via glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).  
    // The first argument is the scale factor for the source color (presumably the existing 
    // fragment color in the frame buffer) and the second argument is the scale factor for the 
    // destination color (presumably the color of the fragment that is being added).  The second 
    // argument ("one minus source alpha") means that, when any color is being added, the 
    // resulting color will be "(existingFragmentAlpha * existingFragmentColor) - 
    // (addedFragmentAlpha * addedFragmentColor)".  
    // Also Note: If the color furthest from the camera is black (vec4(0,0,0,0)), then any 
    // color on top of it will end up as (using the equation) "vec4(0,0,0,0) - whatever", which 
    // is clamped at 0.  So put the opaque (alpha=1) furthest from the camera (this demo is 2D, 
    // so make it a lower Z).  The depth range is 0-1, so the lower Z limit is -1.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

    // FreeType initialization
    std::string freeTypeShaderKey = "freetype";
    shaderStorageRef.NewShader(freeTypeShaderKey);
    shaderStorageRef.AddShaderFile(freeTypeShaderKey, "freeType.vert", GL_VERTEX_SHADER);
    shaderStorageRef.AddShaderFile(freeTypeShaderKey, "freeType.frag", GL_FRAGMENT_SHADER);
    shaderStorageRef.LinkShader(freeTypeShaderKey);
    GLuint freeTypeProgramId = shaderStorageRef.GetShaderProgram(freeTypeShaderKey);
    gTextAtlases.Init("FreeSans.ttf", freeTypeProgramId);

    // for the particle compute shader stuff
    std::string computeShaderKey = "compute particles";
    shaderStorageRef.NewShader(computeShaderKey);
    //shaderStorageRef.AddComputeShaderFile("particleCompute", "particleStructure.comp");
    //shaderStorageRef.AddComputeShaderFile("particleCompute", "particleMain.comp");
    //shaderStorageRef.CompileComputeShader("particleCompute");
    shaderStorageRef.AddShaderFile(computeShaderKey, "particlePolygonRegion.comp", GL_COMPUTE_SHADER);
    shaderStorageRef.LinkShader(computeShaderKey);

    // a render shader specifically for the particles (particle color may change depending on 
    // particle state, so it isn't the same as the geometry's render shader)
    std::string renderParticlesShaderKey = "render particles";
    shaderStorageRef.NewShader(renderParticlesShaderKey);
    shaderStorageRef.AddShaderFile(renderParticlesShaderKey, "particleRender.vert", GL_VERTEX_SHADER);
    shaderStorageRef.AddShaderFile(renderParticlesShaderKey, "particleRender.frag", GL_FRAGMENT_SHADER);
    shaderStorageRef.LinkShader(renderParticlesShaderKey);

    // a render shader specifically for the geometry (nothing special; just a transform, color 
    // white, pass through to frag shader)
    std::string renderGeometryShaderKey = "render geometry";
    shaderStorageRef.NewShader(renderGeometryShaderKey);
    shaderStorageRef.AddShaderFile(renderGeometryShaderKey, "geometry.vert", GL_VERTEX_SHADER);
    shaderStorageRef.AddShaderFile(renderGeometryShaderKey, "geometry.frag", GL_FRAGMENT_SHADER);
    shaderStorageRef.LinkShader(renderGeometryShaderKey);
    gUnifLocGeometryTransform = shaderStorageRef.GetUniformLocation(renderGeometryShaderKey, "transformMatrixWindowSpace");

    // the polygon region
    std::vector<PolygonFace> polygonFaces;
    GeneratePolygonRegion(&polygonFaces);
    gPolygonFaceBuffer.Init(polygonFaces, 
        shaderStorageRef.GetShaderProgram(computeShaderKey),
        shaderStorageRef.GetShaderProgram(renderGeometryShaderKey));

    // stick the point emitter in the center of the polygon region (0,0)
    gpParticleEmitterPoint = new ParticleEmitterPoint(glm::vec2(), 0.3f, 0.5f);

    // start up the encapsulation of the CPU side of the computer shader
    gpParticleComputeUpdater = new ParticlePolygonComputeUpdater(MAX_PARTICLE_COUNT, polygonFaces.size(), computeShaderKey);
    gpParticleComputeUpdater->AddEmitter(gpParticleEmitterPoint, 100);
    std::vector<Particle> allParticles(MAX_PARTICLE_COUNT);
    gpParticleComputeUpdater->InitParticleCollection(allParticles);

    // the particle buffer needs the particles to be initialized first by the compute updater's 
    // InitParticleCollection(...)
    gParticleBuffer.Init(allParticles, 
        shaderStorageRef.GetShaderProgram(computeShaderKey),
        shaderStorageRef.GetShaderProgram(renderParticlesShaderKey));

    // the timer will be used for framerate calculations
    gTimer.Init();
    gTimer.Start();
}

/*-----------------------------------------------------------------------------------------------
Description:
    This is the rendering function.  It tells OpenGL to clear out some color and depth buffers,
    to set up the data to draw, to draw than stuff, and to report any errors that it came across.
    This is not a user-called function.

    This function is registered with glutDisplayFunc(...) during glut's initialization.
Parameters: None
Returns:    None
Creator:    John Cox (2-13-2016)
-----------------------------------------------------------------------------------------------*/
void Display()
{
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // update all particle locations

    glm::mat4 windowSpaceTransform = glm::rotate(glm::mat4(), 45.0f, glm::vec3(0.0f, 0.0f, 1.0f));
    windowSpaceTransform *= glm::translate(glm::mat4(), glm::vec3(+0.3f, +0.3f, 0.0f));

    // the magic happens here
    gpParticleComputeUpdater->Update(0.01f, windowSpaceTransform);

    // draw the particle region borders
    glUseProgram(ShaderStorage::GetInstance().GetShaderProgram("render geometry"));
    glUniformMatrix4fv(gUnifLocGeometryTransform, 1, GL_FALSE, glm::value_ptr(windowSpaceTransform));
    glBindVertexArray(gPolygonFaceBuffer.VaoId());
    glDrawArrays(GL_LINES, 0, gPolygonFaceBuffer.NumVertices());

    // draw the particles
    glUseProgram(ShaderStorage::GetInstance().GetShaderProgram("render particles"));
    glBindVertexArray(gParticleBuffer.VaoId());
    glDrawArrays(gParticleBuffer.DrawStyle(), 0, gParticleBuffer.NumVertices());

    // draw the frame rate once per second in the lower left corner
    GLfloat color[4] = { 0.5f, 0.5f, 0.0f, 1.0f };
    char str[8];
    static int elapsedFramesPerSecond = 0;
    static double elapsedTime = 0.0;
    static double frameRate = 0.0;
    elapsedFramesPerSecond++;
    elapsedTime += gTimer.Lap();
    if (elapsedTime > 1.0)
    {
        frameRate = (double)elapsedFramesPerSecond / elapsedTime;
        elapsedFramesPerSecond = 0;
        elapsedTime -= 1.0f;
    }
    sprintf(str, "%.2lf", frameRate);

    // Note: The font textures' orgin is their lower left corner, so the "lower left" in screen 
    // space is just above [-1.0f, -1.0f].
    float xy[2] = { -0.99f, -0.99f };
    float scaleXY[2] = { 1.0f, 1.0f };

    // the first time that "get shader program" runs, it will load the atlas
    glUseProgram(ShaderStorage::GetInstance().GetShaderProgram("freetype"));
    gTextAtlases.GetAtlas(48)->RenderText(str, xy, scaleXY, color);

    // clean up bindings
    glUseProgram(0);
    glBindVertexArray(0);       // unbind this BEFORE the buffer
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // tell the GPU to swap out the displayed buffer with the one that was just rendered
    glutSwapBuffers();

    // tell glut to call this display() function again on the next iteration of the main loop
    // Note: https://www.opengl.org/discussion_boards/showthread.php/168717-I-dont-understand-what-glutPostRedisplay()-does
    // Also Note: This display() function will also be registered to run if the window is moved
    // or if the viewport is resized.  If glutPostRedisplay() is not called, then as long as the
    // window stays put and doesn't resize, display() won't be called again (tested with 
    // debugging).
    // Also Also Note: It doesn't matter where this is called in this function.  It sets a flag
    // for glut's main loop and doesn't actually call the registered display function, but I 
    // got into the habbit of calling it at the end.
    glutPostRedisplay();
}

/*-----------------------------------------------------------------------------------------------
Description:
    Tell's OpenGL to resize the viewport based on the arguments provided.  This is an 
    opportunity to call glViewport or glScissor to keep up with the change in size.
    
    This is not a user-called function.  It is registered with glutReshapeFunc(...) during 
    glut's initialization.
Parameters:
    w   The width of the window in pixels.
    h   The height of the window in pixels.
Returns:    None
Creator:    John Cox (2-13-2016)
-----------------------------------------------------------------------------------------------*/
void Reshape(int w, int h)
{
    glViewport(0, 0, w, h);
}

/*-----------------------------------------------------------------------------------------------
Description:
    Executes when the user presses a key on the keyboard.

    This is not a user-called function.  It is registered with glutKeyboardFunc(...) during
    glut's initialization.

    Note: Although the x and y arguments are for the mouse's current position, this function does
    not respond to mouse presses.
Parameters:
    key     The ASCII code of the key that was pressed (ex: ESC key is 27)
    x       The horizontal viewport coordinates of the mouse's current position.
    y       The vertical window coordinates of the mouse's current position
Returns:    None
Creator:    John Cox (2-13-2016)
-----------------------------------------------------------------------------------------------*/
void Keyboard(unsigned char key, int x, int y)
{
    // this statement is mostly to get ride of an "unreferenced parameter" warning
    printf("keyboard: x = %d, y = %d\n", x, y);
    switch (key)
    {
    case 27:
    {
        // ESC key
        glutLeaveMainLoop();
        return;
    }
    default:
        break;
    }
}

/*-----------------------------------------------------------------------------------------------
Description:
    I don't know what this does, but I've kept it around since early times, and this was the 
    comment given with it:
    
    "Called before FreeGLUT is initialized. It should return the FreeGLUT display mode flags 
    that you want to use. The initial value are the standard ones used by the framework. You can 
    modify it or just return you own set.  This function can also set the width/height of the 
    window. The initial value of these variables is the default, but you can change it."
Parameters:
    displayMode     ??
    width           ??
    height          ??
Returns:
    ??what??
Creator:    John Cox (2-13-2016)
-----------------------------------------------------------------------------------------------*/
unsigned int Defaults(unsigned int displayMode, int &width, int &height) 
{
    // this statement is mostly to get ride of an "unreferenced parameter" warning
    printf("Defaults: width = %d, height = %d\n", width, height);
    return displayMode; 
}

/*-----------------------------------------------------------------------------------------------
Description:
    Cleans up GPU memory.  This might happen when the processes die, but be a good memory steward
    and clean up properly.

    Note: A big program would have the textures, program IDs, buffers, and other things 
    encapsulated somewhere, and each other those would do the cleanup, but in this barebones 
    demo, I can just clean up everything here.
Parameters: None
Returns:    None
Creator:    John Cox (2-13-2016)
-----------------------------------------------------------------------------------------------*/
void CleanupAll()
{
    //// these deletion functions need the buffer ID, but they take a (void *) for the second 
    delete gpParticleEmitterPoint;
    delete gpParticleComputeUpdater;
}

/*-----------------------------------------------------------------------------------------------
Description:
    Program start and end.
Parameters:
    argc    The number of strings in argv.
    argv    A pointer to an array of null-terminated, C-style strings.
Returns:
    0 if program ended well, which it always does or it crashes outright, so returning 0 is fine
Creator:    John Cox (2-13-2016)
-----------------------------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    glutInit(&argc, argv);

    int width = 500;
    int height = 500;
    unsigned int displayMode = GLUT_DOUBLE | GLUT_ALPHA | GLUT_DEPTH | GLUT_STENCIL;
    displayMode = Defaults(displayMode, width, height);

    glutInitDisplayMode(displayMode);
    glutInitContextVersion(4, 4);
    glutInitContextProfile(GLUT_CORE_PROFILE);

    // enable this for automatic message reporting (see OpenGlErrorHandling.cpp)
#define DEBUG
#ifdef DEBUG
    glutInitContextFlags(GLUT_DEBUG);
#endif

    glutInitWindowSize(width, height);
    glutInitWindowPosition(300, 200);
    int window = glutCreateWindow(argv[0]);

    glload::LoadTest glLoadGood = glload::LoadFunctions();
    // ??check return value??

    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_CONTINUE_EXECUTION);

    if (!glload::IsVersionGEQ(3, 3))
    {
        printf("Your OpenGL version is %i, %i. You must have at least OpenGL 3.3 to run this tutorial.\n",
            glload::GetMajorVersion(), glload::GetMinorVersion());
        glutDestroyWindow(window);
        return 0;
    }

    if (glext_ARB_debug_output)
    {
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
        glDebugMessageCallbackARB(DebugFunc, (void*)15);
    }

    Init();

    glutDisplayFunc(Display);
    glutReshapeFunc(Reshape);
    glutKeyboardFunc(Keyboard);
    glutMainLoop();

    CleanupAll();

    return 0;
}