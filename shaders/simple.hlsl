cbuffer CameraCB : register(b0)
{
    float3 pos;
    float _pad0;
    float3 forward;
    float _pad1;
    float3 up;
    float _pad2;
    float params[5]; // fov(deg), aspect, znear, zfar, intensity
};

struct VSInput {
    float3 position : POSITION;
    float3 color : COLOR;
};

struct PSInput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

PSInput VSMain(VSInput input)
{
    PSInput o;

    // Use same camera basis and projection math as RayGen/mesh shaders
    float aspect = params[1];
    float f = tan(radians(params[0]) * 0.5);

    float3 R = normalize(cross(forward, up));
    float3 U = normalize(cross(R, forward));

    float3 rel = input.position - pos;
    float x_cam = dot(rel, R);
    float y_cam = dot(rel, U);
    float z_cam = dot(rel, forward);

    // Produce clip-space position with w = z_cam so NDC matches RayGen mapping
    o.position = float4(x_cam / (aspect * f), -y_cam / f, z_cam, z_cam);
    o.color = input.color;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(input.color, 1.0);
}

// Simple mesh-only vertex shader that reads position only and outputs a constant color
struct VSInputMeshSimple { float3 position : POSITION; };
struct PSInputMeshSimple { float4 position : SV_POSITION; };
PSInputMeshSimple VSMainMeshSimple(VSInputMeshSimple input)
{
    PSInputMeshSimple o;
    float aspect = params[1];
    float f = tan(radians(params[0]) * 0.5);

    float3 R = normalize(cross(forward, up));
    float3 U = normalize(cross(R, forward));

    float3 rel = input.position - pos;
    float x_cam = dot(rel, R);
    float y_cam = dot(rel, U);
    float z_cam = dot(rel, forward);

    o.position = float4(x_cam / (aspect * f), -y_cam / f, z_cam, z_cam);
    return o;
}

float4 PSMainMeshSimple(PSInputMeshSimple input) : SV_TARGET
{
    return float4(0.8, 0.6, 0.2, 1.0);
}
