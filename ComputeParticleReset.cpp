#include "ComputeParticleReset.h"

#include "ShaderStorage.h"

#include "glload/include/glload/gl_4_4.h"
#include "glm/gtc/type_ptr.hpp"

// for starting up the atomic counter for the compute shader's rand hash
#include <random>
#include <time.h>


/*-----------------------------------------------------------------------------------------------
Description:
    Generates atomic counters for use in the "particle reset" compute shader.
    Looks up all uniforms in the compute shader.

    Note: This constructor takes a string key for the compute shader instead of a program ID 
    because the shader storage object, which is responsible for finding uniforms, takes a shader 
    key instead of a direct program ID.
Parameters: 
    numParticles        Used to tell the compute shader how many are in the particle buffer.
    computeShaderKey    Used to look up (1) the compute shader ID and (2) uniform locations.
Returns:    None
Creator:    John Cox (11-24-2016)
-----------------------------------------------------------------------------------------------*/
ComputeParticleReset::ComputeParticleReset(unsigned int numParticles, 
    const std::string &computeShaderKey)
{
    _totalParticleCount = numParticles;
    ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();
    
    // find the uniforms in the "reset" compute shader
    _unifLocParticleCount = shaderStorageRef.GetUniformLocation(computeShaderKey, "uMaxParticleCount");
    _unifLocMaxParticleEmitCount = shaderStorageRef.GetUniformLocation(computeShaderKey, "uMaxParticleEmitCount");
    _unifLocMinParticleVelocity = shaderStorageRef.GetUniformLocation(computeShaderKey, "uMinParticleVelocity");
    _unifLocDeltaParticleVelocity = shaderStorageRef.GetUniformLocation(computeShaderKey, "uDeltaParticleVelocity");
    _unifLocUsePointEmitter = shaderStorageRef.GetUniformLocation(computeShaderKey, "uUsePointEmitter");
    _unifLocPointEmitterCenter = shaderStorageRef.GetUniformLocation(computeShaderKey, "uPointEmitterCenter");
    _unifLocBarEmitterP1 = shaderStorageRef.GetUniformLocation(computeShaderKey, "uBarEmitterP1");
    _unifLocBarEmitterP2 = shaderStorageRef.GetUniformLocation(computeShaderKey, "uBarEmitterP2");
    _unifLocBarEmitterEmitDir = shaderStorageRef.GetUniformLocation(computeShaderKey, "uBarEmitterEmitDir");

    // now set up the atomic counters
    _computeProgramId = shaderStorageRef.GetShaderProgram(computeShaderKey);

    glUseProgram(_computeProgramId);

    // the program in which this uniform is located must be bound in order to set the value
    glUniform1ui(_unifLocParticleCount, numParticles);

    // atomic counter initialization courtesy of geeks3D (and my use of glBufferData(...) 
    // instead of glMapBuffer(...)
    // http://www.geeks3d.com/20120309/opengl-4-2-atomic-counter-demo-rendering-order-of-fragments/

    // particle counter
    // Note: Don't bother giving it an initial value.  It is updated on every call to 
    // ResetParticles(...).
    glGenBuffers(1, &_acParticleCounterBufferId);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, _acParticleCounterBufferId);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), 0, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);

    // starting up the random hash counter
    // Note: This value is also updated on every call to ResetParticles(...), so don't give it 
    // an initial value.  DO seed random though.
    srand(time(0));
    glGenBuffers(1, &_acRandSeed);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, _acRandSeed);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), 0, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);

    glUseProgram(_computeProgramId);

    // don't need to have a program or bound buffer to set the buffer base
    // Note: It seems that atomic counters must be bound where they are declared and cannot be 
    // bound dynamically like the ParticleSsbo and PolygonSsbo.  So remember to use the SAME buffer 
    // binding base as specified in the shader.
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 3, _acParticleCounterBufferId);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 4, _acRandSeed);
}

/*-----------------------------------------------------------------------------------------------
Description:
    Cleans up buffers that were allocated in this object.
Parameters: None
Returns:    None
Creator:    John Cox (11-24-2016)
-----------------------------------------------------------------------------------------------*/
ComputeParticleReset::~ComputeParticleReset()
{
    glDeleteBuffers(1, &_acParticleCounterBufferId);
    glDeleteBuffers(1, &_acRandSeed);
}

/*-----------------------------------------------------------------------------------------------
Description:
    Adds a point emitter to internal storage.  This is used to initialize particles.  If there 
    multiple emitters, then the update will need to perform multiple calls to the compute 
    shader, each with different emitter information.

    If, for some reason, the particle emitter cannot be cast to either a point emitter or a bar 
    emitter, then the emitter will not be added to either particle emitter collection and 
    "false" is returned. 

    Note: Particles are evenly split between all emitters.
Parameters:
    pEmitter    A pointer to a "particle emitter" interface.
Returns:    
    True if the emitter was added, otherwise false.
Creator:    John Cox (9-18-2016)    (created prior to this class in an earlier design)
-----------------------------------------------------------------------------------------------*/
bool ComputeParticleReset::AddEmitter(const IParticleEmitter *pEmitter)
{
    const ParticleEmitterPoint *pointEmitter =
        dynamic_cast<const ParticleEmitterPoint *>(pEmitter);
    const ParticleEmitterBar *barEmitter =
        dynamic_cast<const ParticleEmitterBar *>(pEmitter);

    if (pointEmitter != 0 && (_pointEmitters.size() < MAX_EMITTERS))
    {
        _pointEmitters.push_back(pointEmitter);
        return true;
    }
    else if (barEmitter != 0 && (_barEmitters.size() < MAX_EMITTERS))
    {
        _barEmitters.push_back(barEmitter);
        return true;
    }

    return false;
}

/*-----------------------------------------------------------------------------------------------
Description:
    All the particle emitters reset particles to that emitter's location (up to a limit).

    Particles are spread out evenly between all the emitters (or at least as best as possible; 
    technically the first emitter emitter gets first dibs at the inactive particles, then the 
    second emitter gets second dibs, etc.).  This caused some trouble because successive calls 
    to the compute shader in the same frame caused many particles to have their positions 
    updated more than once.  To solve this, the compute shader was split into two major 
    sections: 
Parameters:    
    particlesPerEmitterPerFrame        Limits the number of particles that are reset per frame so 
    that they don't all spawn at once.
Returns:    None
Creator:    John Cox (10-10-2016)
            (created in an earlier class, but later split into a dedicated class)
-----------------------------------------------------------------------------------------------*/
void ComputeParticleReset::ResetParticles(unsigned int particlesPerEmitterPerFrame)
{
    if (_pointEmitters.empty() || _barEmitters.empty())
    {
        // nothing to do
        return;
    }

    GLuint acResetCounterValue = 0;

    // spreading the particles evenly between multiple emitters is done by letting all the 
    // particle emitters have a go at all the inactive particles one by one, so all particles 
    // must be considered
    // Note: Yes, this algorithm is such that emitters resetting particles have to traverse 
    // through the entire particle collection, but since there isn't a way of telling the CPU 
    // where they were when the last particle was reset and since the GPU seems pretty fast on 
    // running through the entire array, this algorithm is fine.
    GLuint numWorkGroupsX = (_totalParticleCount / 256) + 1;
    GLuint numWorkGroupsY = 1;
    GLuint numWorkGroupsZ = 1;

    glUseProgram(_computeProgramId);

    // give the rand seed some variance from the last frame
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, _acRandSeed);
    GLuint acRandSeed = rand();
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), (void *)&_acRandSeed, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);

    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, _acParticleCounterBufferId);
    glUniform1ui(_unifLocMaxParticleEmitCount, particlesPerEmitterPerFrame);

    // give all point emitters a chance to reactive inactive particles at their positions
    glUniform1ui(_unifLocUsePointEmitter, 1);
    for (size_t pointEmitterCount = 0; pointEmitterCount < _pointEmitters.size(); pointEmitterCount++)
    {
        // reset everything necessary to control the emission parameters for this emitter
        glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), (void *)&acResetCounterValue);

        const ParticleEmitterPoint *emitter = _pointEmitters[pointEmitterCount];
        glUniform1f(_unifLocMinParticleVelocity, emitter->GetMinVelocity());
        glUniform1f(_unifLocDeltaParticleVelocity, emitter->GetDeltaVelocity());

        // use an array for uploading vectors to be certain of the ordering 
        glm::vec4 emitterPos = emitter->GetPos();
        float emitterPosArr[4] = { emitterPos.x, emitterPos.y, emitterPos.z, emitterPos.w };
        glUniform4fv(_unifLocPointEmitterCenter, 1, emitterPosArr);

        // compute ALL the resets!
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);

        // tell the GPU:
        // (1) Accesses to the shader buffer after this call will reflect writes prior to the 
        // barrier.  This is only available in OpenGL 4.3 or higher.
        // (2) Vertex data sourced from buffer objects after the barrier will reflect data 
        // written by shaders prior to the barrier.  The affected buffer(s) is determined by the 
        // buffers that were bound for the vertex attributes.  In this case, that means 
        // GL_ARRAY_BUFFER.
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    }

    // repeat for any bar emitters
    glUniform1ui(_unifLocUsePointEmitter, 0);
    for (size_t barEmitterCount = 0; barEmitterCount < _barEmitters.size(); barEmitterCount++)
    {
        glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), (void *)&acResetCounterValue);

        const ParticleEmitterBar *emitter = _barEmitters[barEmitterCount];
        glUniform1f(_unifLocMinParticleVelocity, emitter->GetMinVelocity());
        glUniform1f(_unifLocDeltaParticleVelocity, emitter->GetDeltaVelocity());

        // each bar has three vectors that need to be uploaded (p1, p2, and emit direction)
        glm::vec4 emitterBarP1 = emitter->GetBarStart();
        glm::vec4 emitterBarP2 = emitter->GetBarEnd();
        glm::vec4 emitDir = emitter->GetEmitDir();
        float emitterBarP1Arr[4] = { emitterBarP1.x, emitterBarP1.y, emitterBarP1.z, emitterBarP1.w };
        float emitterBarP2Arr[4] = { emitterBarP2.x, emitterBarP2.y, emitterBarP2.z, emitterBarP2.w };
        float emitterBarEmitDir[4] = { emitDir.x, emitDir.y, emitDir.y, emitDir.z };
        glUniform4fv(_unifLocBarEmitterP1, 1, emitterBarP1Arr);
        glUniform4fv(_unifLocBarEmitterP2, 1, emitterBarP2Arr);
        glUniform4fv(_unifLocBarEmitterEmitDir, 1, emitterBarEmitDir);

        // MOAR resets!
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    }

    // cleanup
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);
    glUseProgram(0);
}
