cbuffer CameraCB : register(b0)
{
    float3 pos;
    float _pad0;
    float3 forward;
    float _pad1;
    float3 up;
    float _pad2;
    float4 params; // fov(deg), aspect, znear, zfar
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

    // Compute view matrix
    float3 right = normalize(cross(forward, up));
    float3 trueUp = cross(right, forward);

    float3x3 viewRot = {
        right.x, trueUp.x, -forward.x,
        right.y, trueUp.y, -forward.y,
        right.z, trueUp.z, -forward.z
    };

    float3 viewPos = mul(viewRot, input.position - pos);

    // Compute projection matrix
    float fovRad = params.x * 3.14159265359 / 180.0;
    float f = 1.0 / tan(fovRad / 2.0);
    float aspect = params.y;
    float nearZ = params.z;
    float farZ = params.w;

    float4 projPos;
    projPos.x = viewPos.x * f / aspect;
    projPos.y = viewPos.y * f;
    projPos.z = viewPos.z * (-(farZ + nearZ) / (farZ - nearZ)) - 2.0 * farZ * nearZ / (farZ - nearZ);
    projPos.w = -viewPos.z;

    o.position = projPos;
    o.color = input.color;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(input.color, 1.0);
}
