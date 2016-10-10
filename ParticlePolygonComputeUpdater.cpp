#include "ParticlePolygonComputeUpdater.h"

#include "ShaderStorage.h"
#include "IParticleEmitter.h"

#include "glload/include/glload/gl_4_4.h"
#include "glm/gtc/type_ptr.hpp"

/*-----------------------------------------------------------------------------------------------
Description:
    Looks up all uniforms in the compute shader that checks particles against a polygon region.  
    Uses the "num particles" and "num faces" arguments to tell the compute shader how many will 
    be in each particle buffer and polygon face buffer, respectively.
Parameters: 
    numParticles        Used to tell the compute shader how many are in the particle buffer.
    numFaces            Used to tell the compute shader how many are in the face buffer.
    computeShaderKey    Used to look up (1) the compute shader ID and (2) uniform locations.
Returns:    None
Creator:    John Cox (7-4-2016)
-----------------------------------------------------------------------------------------------*/
ParticlePolygonComputeUpdater::ParticlePolygonComputeUpdater(unsigned int numParticles, 
    unsigned int numFaces, const std::string &computeShaderKey)
{
    _totalParticleCount = numParticles;

    ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();
    _computeProgramId = shaderStorageRef.GetShaderProgram(computeShaderKey);

    // these are constant through the program
    _unifLocParticleCount = shaderStorageRef.GetUniformLocation(computeShaderKey, "uMaxParticleCount");
    _unifLocPolygonFaceCount = shaderStorageRef.GetUniformLocation(computeShaderKey, "uPolygonFaceCount");

    glUseProgram(_computeProgramId);
    glUniform1ui(_unifLocParticleCount, numParticles);
    glUniform1ui(_unifLocPolygonFaceCount, numFaces);

    // courtesy of geeks3D (and my use of glBufferData(...) instead of glMapBuffer(...)
    // http://www.geeks3d.com/20120309/opengl-4-2-atomic-counter-demo-rendering-order-of-fragments/
    glGenBuffers(1, &_atomicCounterBuffer);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, _atomicCounterBuffer);
    GLuint atomicCounterResetVal = 0;
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), (void *)&atomicCounterResetVal, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, _atomicCounterBuffer);

    // it seems that atomic counters must be bound where they are declared and cannot be bound 
    // as the ParticleSsbo and PolygonSsbo

    glUseProgram(0);

    // these are updated during Update(...)
    _unifLocPointEmitterCenter = shaderStorageRef.GetUniformLocation(computeShaderKey, "uPointEmitterCenter");
    _unifLocBarP1 = shaderStorageRef.GetUniformLocation(computeShaderKey, "uBarEmitterP1");
    _unifLocBarP2 = shaderStorageRef.GetUniformLocation(computeShaderKey, "uBarEmitterP2");
    _unifLocBarEmitDir = shaderStorageRef.GetUniformLocation(computeShaderKey, "uBarEmitterEmitDir");

    _unifLocMinParticleVelocity = shaderStorageRef.GetUniformLocation(computeShaderKey, "uMinParticleVelocity");
    _unifLocDeltaParticleVelocity = shaderStorageRef.GetUniformLocation(computeShaderKey, "uDeltaParticleVelocity");
    _unifLocDeltaTimeSec = shaderStorageRef.GetUniformLocation(computeShaderKey, "uDeltaTimeSec");

    _unifLocMaxParticleEmitCount = shaderStorageRef.GetUniformLocation(computeShaderKey, "uMaxParticleEmitCount");
    _unifLocUsePointEmitter = shaderStorageRef.GetUniformLocation(computeShaderKey, "uUsePointEmitter");
    _unifLocOnlyResetParticles = shaderStorageRef.GetUniformLocation(computeShaderKey, "uOnlyResetParticles");
    _unifLocWindowSpaceRegionTransform = shaderStorageRef.GetUniformLocation(computeShaderKey, "uWindowSpaceRegionTransform");
    _unifLocWindowSpaceEmitterTransform = shaderStorageRef.GetUniformLocation(computeShaderKey, "uWindowSpaceEmitterTransform");

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
Creator:    John Cox (9-18-2016)
-----------------------------------------------------------------------------------------------*/
bool ParticlePolygonComputeUpdater::AddEmitter(const IParticleEmitter *pEmitter)
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
    Gives the particle collection initial values.  The particles are initialized evenly between 
    between all emitters.
    
    Note: This function is necessary because particles init with 0 values and GLSL doesn't have 
    a random() function.  This demo has instead used a random hash function that is quite 
    chaotic but relies on the particles having non-zero velocities.  If all the particles start 
    with velocities of 0, then the hash won't work.  
Parameters:
    initThis    A non-const reference to the particle collection that needs to be initialized.
Returns:    None
Creator:    John Cox, 9-25-2016
-----------------------------------------------------------------------------------------------*/
void ParticlePolygonComputeUpdater::InitParticleCollection(std::vector<Particle> &initThis)
{
    unsigned int totalParticles = initThis.size();

    for (size_t particleCount = 0; particleCount < totalParticles; )
    {
        unsigned int perEmitterCount = 0;
        for (size_t pointEmitterCount = 0; pointEmitterCount < _pointEmitters.size(); pointEmitterCount++)
        {
            // prevent particle array overrun
            if (particleCount + perEmitterCount == totalParticles)
            {
                break;
            }

            _pointEmitters[pointEmitterCount]->ResetParticle(&initThis[particleCount + perEmitterCount]);
            perEmitterCount++;
        }

        for (size_t barEmitterCount = 0; barEmitterCount < _barEmitters.size(); barEmitterCount++)
        {
            // prevent particle array overrun
            if (particleCount + perEmitterCount == totalParticles)
            {
                break;
            }

            _barEmitters[barEmitterCount]->ResetParticle(&initThis[particleCount + perEmitterCount]);
            perEmitterCount++;
        }

        particleCount += perEmitterCount;
    }
}

/*-----------------------------------------------------------------------------------------------
Description:
    Summons the compute shader.

    Particles are spread out evenly between all the emitters (or at least as best as possible; 
    technically the first emitter emitter gets first dibs at the inactive particles, then the 
    second emitter gets second dibs, etc.).  This caused some trouble because successive calls 
    to the compute shader in the same frame caused many particles to have their positions 
    updated more than once.  To solve this, the compute shader was split into two major 
    sections: 
    (1) All the particle emitters reset particles to that emitter's location (up to a limit).
    (2) Update all particle positions based on velocity and delta time.

    Note: Yes, this algorithm is such that emitters resetting particles have to traverse through 
    the entire particle collection, but since there isn't a way of telling the CPU where they 
    were when the last particle was reset and since the GPU seems pretty fast on running through 
    the entire array, this algorithm is fine.
Parameters:
    numParticles    Used to compute the number of work groups.
    deltatimeSec        Self-explanatory
Returns:    None
Creator:    John Cox (7-4-2016)
-----------------------------------------------------------------------------------------------*/
void ParticlePolygonComputeUpdater::Update(const float deltaTimeSec, 
    const glm::mat4 &windowSpaceTransform) const
{
    if (_pointEmitters.empty() && _barEmitters.empty())
    {
        // nothing to do
        return;
    }

    // it seems to run through just about all particles if the numerator is 1/10 of the max 
    // particle count (at least for this demo)
    unsigned int particlesPerEmitterThisFrame = 50 / (_pointEmitters.size() + _barEmitters.size()); 

    // spreading the particles evenly between multiple emitters is done by letting all the 
    // particle emitters have a go at all the inactive particles one by one, so all particles 
    // must be considered
    GLuint numWorkGroupsX = (_totalParticleCount / 256) + 1;
    GLuint numWorkGroupsY = 1;
    GLuint numWorkGroupsZ = 1;

    // constant throughout the update
    glUseProgram(_computeProgramId);
    glUniformMatrix4fv(_unifLocWindowSpaceRegionTransform, 1, GL_FALSE, glm::value_ptr(windowSpaceTransform));
    glUniformMatrix4fv(_unifLocWindowSpaceEmitterTransform, 1, GL_FALSE, glm::value_ptr(windowSpaceTransform));
    glUniform1ui(_unifLocOnlyResetParticles, 1);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, _atomicCounterBuffer);
    GLuint atomicCounterResetVal = 0;
    glUniform1ui(_unifLocMaxParticleEmitCount, particlesPerEmitterThisFrame);

    // give all point emitters a chance to reactive inactive particles at their positions
    glUniform1ui(_unifLocUsePointEmitter, 1);
    for (size_t pointEmitterCount = 0; pointEmitterCount < _pointEmitters.size(); pointEmitterCount++)
    {
        // reset everything necessary to control the emission parameters for this emitter
        glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), (void *)&atomicCounterResetVal);

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
        glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), (void *)&atomicCounterResetVal);

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
        glUniform4fv(_unifLocBarP1, 1, emitterBarP1Arr);
        glUniform4fv(_unifLocBarP2, 1, emitterBarP2Arr);
        glUniform4fv(_unifLocBarEmitDir, 1, emitterBarEmitDir);

        // MOAR resets!
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    }

    // position update
    glUniform1f(_unifLocDeltaTimeSec, deltaTimeSec);
    glUniform1ui(_unifLocOnlyResetParticles, 0);
    glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    // cleanup
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);
    glUseProgram(0);
}
