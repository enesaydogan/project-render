// shaders/ies_lib.hlsl
// IES profile atlas resources and sampling helpers.
// Only included when IES_ENABLED is defined.

#ifndef IES_LIB_HLSL
#define IES_LIB_HLSL

Texture2DArray<float4> g_iesAtlas : register(t5002);

inline float SampleIESAtlas(int atlasIndex, float3 localDir) {
    // localDir: direction FROM light TOWARD surface, in light-local space
    // localDir.z = dot(localDir, lightForward), etc.
    float theta = acos(clamp(localDir.z, -1.0, 1.0)); // 0=forward, PI=backward
    float phi = atan2(localDir.y, localDir.x);         // azimuth

    // Map to texel coordinates matching CPU bake:
    //   CPU: y = theta/180 * 255,  x = phi/360 * 255
    uint tx = uint((phi + PI) / (2.0 * PI) * 255.0 + 0.5);
    uint ty = uint(theta / PI * 255.0 + 0.5);

    return g_iesAtlas.Load(int4(tx, ty, atlasIndex, 0)).x;
}

inline float SampleIESModulation(Light light, float3 toSurface)
{
    if (light.iesAtlasIndex < 0) return 1.0;

    float3 lightForward = normalize(light.direction);
    float3 up = abs(lightForward.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, lightForward));
    up = cross(lightForward, right);

    float3 localDir;
    localDir.x = dot(toSurface, right);
    localDir.y = dot(toSurface, up);
    localDir.z = dot(toSurface, lightForward);

    return max(0.0, SampleIESAtlas(light.iesAtlasIndex, localDir));
}

#endif // IES_LIB_HLSL
