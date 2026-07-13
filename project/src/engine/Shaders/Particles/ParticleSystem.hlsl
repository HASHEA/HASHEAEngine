// GPU particle system (SDD-2026-07-10-gpu-particles).
// One file, two variants:
//   PARTICLE_COMPUTE defined  -> stable classify/scan/scatter compaction + CSWriteArgs
//   PARTICLE_COMPUTE undefined -> billboard indirect draw (VSMain/PSMain)
// The two variants use different AshRootConstants layouts; the macro keeps them
// from colliding on b0 within a single compilation.

// Must match ParticleGPUData in ParticleSystemPass.h (32 bytes).
struct AshParticleData
{
    float3 position;
    float age;
    float3 velocity;
    float lifetime;
};

#if defined(PARTICLE_COMPUTE)

cbuffer AshRootConstants : register(b0)
{
    float4x4 AshEmitterTransform;
    // (spawn_count, max_particles as uint bits, delta_seconds, lifetime)
    uint AshSpawnCount;
    uint AshMaxParticles;
    float AshDeltaSeconds;
    float AshLifetime;
    float AshLifetimeVariance;
    float AshInitialSpeed;
    float AshSpreadAngleRadians;
    uint AshRandomSeed;
    uint AshTotalSpawned;
    uint AshCandidateGroupCount;
    uint2 AshParticlePad0;
    float4 AshConstantAcceleration;
};

StructuredBuffer<AshParticleData> ParticlePoolIn : register(t0);
// Previous frame's AshDrawIndirectArgs; instanceCount at byte offset 4 is the
// alive count produced by last frame's CSWriteArgs.
ByteAddressBuffer ParticleArgsIn : register(t1);
StructuredBuffer<uint> ParticleBlockCountsIn : register(t2);
StructuredBuffer<uint> ParticleBlockOffsetsIn : register(t3);
StructuredBuffer<uint> ParticleCounterIn : register(t4);
RWStructuredBuffer<uint> ParticleBlockCounts : register(u0);
RWStructuredBuffer<uint> ParticleBlockOffsets : register(u1);
RWStructuredBuffer<uint> ParticleCounter : register(u2);
RWStructuredBuffer<AshParticleData> ParticlePoolOut : register(u3);
RWByteAddressBuffer ParticleDrawArgs : register(u4);

groupshared uint AshGroupPrefix[64];

uint AshWangHash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16);
    seed *= 9u;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15);
    return seed;
}

// Deterministic per-particle stream: identical (RandomSeed, TotalSpawned + i)
// always produces identical particles (frame-dump determinism contract).
float AshRand01(inout uint state)
{
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    word = (word >> 22u) ^ word;
    return (float)word * (1.0 / 4294967296.0);
}

uint AshCandidateAlive(uint candidate_index, uint input_count)
{
    if (candidate_index < input_count)
    {
        const AshParticleData particle = ParticlePoolIn[candidate_index];
        return particle.age + AshDeltaSeconds < particle.lifetime ? 1u : 0u;
    }

    return candidate_index - input_count < AshSpawnCount ? 1u : 0u;
}

AshParticleData AshBuildCandidate(uint candidate_index, uint input_count)
{
    if (candidate_index < input_count)
    {
        AshParticleData particle = ParticlePoolIn[candidate_index];
        particle.age += AshDeltaSeconds;
        particle.velocity += AshConstantAcceleration.xyz * AshDeltaSeconds;
        particle.position += particle.velocity * AshDeltaSeconds;
        return particle;
    }

    const uint spawn_index = candidate_index - input_count;

    uint rng_state = AshWangHash(AshRandomSeed ^ AshWangHash(AshTotalSpawned + spawn_index));
    const float u_cone = AshRand01(rng_state);
    const float u_phi = AshRand01(rng_state);
    const float u_lifetime = AshRand01(rng_state);

    // Cone around emitter-local +Y.
    const float cos_theta = lerp(cos(AshSpreadAngleRadians), 1.0, u_cone);
    const float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
    const float phi = u_phi * 6.28318530718;
    const float3 local_direction = float3(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));
    float3 world_direction = mul(AshEmitterTransform, float4(local_direction, 0.0)).xyz;
    const float direction_length = length(world_direction);
    world_direction = direction_length > 1e-5 ? world_direction / direction_length : float3(0.0, 1.0, 0.0);

    AshParticleData particle;
    particle.position = mul(AshEmitterTransform, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    particle.velocity = world_direction * AshInitialSpeed;
    particle.age = 0.0;
    particle.lifetime = max(AshLifetime + (u_lifetime * 2.0 - 1.0) * AshLifetimeVariance, 0.01);
    return particle;
}

// Pass 1: deterministically count alive candidates per 64-thread block. Existing
// particles occupy the prefix of candidate space and spawns occupy its tail.
[numthreads(64, 1, 1)]
void CSClassify(
    uint3 dispatch_id : SV_DispatchThreadID,
    uint3 group_id : SV_GroupID,
    uint group_index : SV_GroupIndex)
{
    const uint input_count = min(ParticleArgsIn.Load(4), AshMaxParticles);
    AshGroupPrefix[group_index] = AshCandidateAlive(dispatch_id.x, input_count);
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 32u; stride > 0u; stride >>= 1u)
    {
        if (group_index < stride)
        {
            AshGroupPrefix[group_index] += AshGroupPrefix[group_index + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (group_index == 0u)
    {
        ParticleBlockCounts[group_id.x] = AshGroupPrefix[0];
    }
}

// Pass 2: the block list is at most 2048 entries (2 * 65536 / 64), so one
// deterministic thread can prefix-scan it without a second hierarchy.
[numthreads(1, 1, 1)]
void CSScanBlocks()
{
    uint running_count = 0u;
    for (uint block_index = 0u; block_index < AshCandidateGroupCount; ++block_index)
    {
        ParticleBlockOffsets[block_index] = running_count;
        running_count += ParticleBlockCountsIn[block_index];
    }
    ParticleCounter[0] = min(running_count, AshMaxParticles);
}

// Pass 3: repeat the local predicate, compute a stable local prefix, and scatter
// survivors in input order followed by spawns in spawn-index order.
[numthreads(64, 1, 1)]
void CSScatter(
    uint3 dispatch_id : SV_DispatchThreadID,
    uint3 group_id : SV_GroupID,
    uint group_index : SV_GroupIndex)
{
    const uint input_count = min(ParticleArgsIn.Load(4), AshMaxParticles);
    const uint alive = AshCandidateAlive(dispatch_id.x, input_count);
    AshGroupPrefix[group_index] = alive;
    GroupMemoryBarrierWithGroupSync();

    for (uint offset = 1u; offset < 64u; offset <<= 1u)
    {
        const uint prefix_add = group_index >= offset ? AshGroupPrefix[group_index - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        AshGroupPrefix[group_index] += prefix_add;
        GroupMemoryBarrierWithGroupSync();
    }

    if (alive == 0u)
    {
        return;
    }

    const uint output_index = ParticleBlockOffsetsIn[group_id.x] + AshGroupPrefix[group_index] - 1u;
    if (output_index < AshMaxParticles)
    {
        ParticlePoolOut[output_index] = AshBuildCandidate(dispatch_id.x, input_count);
    }
}

[numthreads(1, 1, 1)]
void CSWriteArgs()
{
    // AshDrawIndirectArgs { vertexCount=6, instanceCount=alive, firstVertex=0, firstInstance=0 }
    const uint alive = min(ParticleCounterIn[0], AshMaxParticles);
    ParticleDrawArgs.Store4(0, uint4(6u, alive, 0u, 0u));
}

#else // draw variant

cbuffer AshRootConstants : register(b0)
{
    float4x4 AshViewProjection;
    float4 AshCameraRightStartSize;
    float4 AshCameraUpEndSize;
    float4 AshParticleStartColor;
    float4 AshParticleEndColor;
};

StructuredBuffer<AshParticleData> ParticlePool : register(t0);

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 corner : TEXCOORD0;
};

// Two CCW triangles: (-1,-1) (1,-1) (1,1) / (-1,-1) (1,1) (-1,1).
static const float2 k_particle_corners[6] =
{
    float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
    float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)
};

VSOutput VSMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    const AshParticleData particle = ParticlePool[instance_id];
    const float life_t = saturate(particle.age / max(particle.lifetime, 1e-5));
    const float size = lerp(AshCameraRightStartSize.w, AshCameraUpEndSize.w, life_t);
    const float2 corner = k_particle_corners[vertex_id];
    const float3 world_position =
        particle.position +
        (AshCameraRightStartSize.xyz * corner.x + AshCameraUpEndSize.xyz * corner.y) * (size * 0.5);

    VSOutput output;
    output.position = mul(AshViewProjection, float4(world_position, 1.0));
    output.color = lerp(AshParticleStartColor, AshParticleEndColor, life_t);
    output.corner = corner;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    const float radius_sq = dot(input.corner, input.corner);
    const float falloff_base = saturate(1.0 - radius_sq);
    const float falloff = falloff_base * falloff_base;
    const float alpha = saturate(input.color.a * falloff);
#if defined(PARTICLE_ALPHA_BLEND)
    return float4(input.color.rgb, alpha);
#else
    // Additive (ONE/ONE): pre-multiply so alpha shapes the contribution.
    return float4(input.color.rgb * alpha, alpha);
#endif
}

#endif
