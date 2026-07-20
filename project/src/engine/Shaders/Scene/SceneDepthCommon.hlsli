#ifndef ASH_SCENE_DEPTH_COMMON_HLSLI
#define ASH_SCENE_DEPTH_COMMON_HLSLI

bool AshSceneDepthIsBackground(float depth, bool reverse_z)
{
    return reverse_z ? depth <= 0.0 : depth >= 1.0;
}

#endif
