cbuffer vsConstants : register(b0)
{
    float4x4 modelViewProj;
    float4x4 modelView;
    float3x3 normalMatrix;
};

struct DirectionalLight
{
    float4 dirEye; //NOTE: Direction *towards* the light
    float4 color;
};

struct PointLight
{
    float4 posEye;
    float4 color;
};

// Create Constant Buffer for our Blinn-Phong vertex shader
cbuffer fsConstants : register(b0)
{
    DirectionalLight dirLight;
    PointLight pointLights[2];
};

struct VS_Input {
    float3 pos : POS;
    float2 uv : TEX;
    float3 norm : NORM;
};

struct VS_Output {
    float4 pos : SV_POSITION;
    float3 posEye : POSITION;
    float3 normalEye : NORMAL;
    float2 uv : TEXCOORD;
};

Texture2D    mytexture : register(t0);
SamplerState mysampler : register(s0);

VS_Output vs_main(VS_Input input)
{
    VS_Output output;
    output.pos = mul(float4(input.pos, 1.0f), modelViewProj);
    output.posEye = mul(float4(input.pos, 1.0f), modelView).xyz;
    output.normalEye = mul(input.norm, normalMatrix);
    output.uv = input.uv;
    return output;
}

float4 ps_main(VS_Output input) : SV_Target
{
    float3 diffuseColor = mytexture.Sample(mysampler, input.uv).xyz;

    float3 fragToCamDir = normalize(-input.posEye);
    
    // Directional Light
    float3 dirLightIntensity;
    {
        float ambientStrength = 0.1;
        float specularStrength = 0.9;
        float specularExponent = 100;
        float3 lightDirEye = dirLight.dirEye.xyz;
        float3 lightColor = dirLight.color.xyz;

        float3 iAmbient = ambientStrength;

        float diffuseFactor = max(0.0, dot(input.normalEye, lightDirEye));
        float3 iDiffuse = diffuseFactor;

        float3 halfwayEye = normalize(fragToCamDir + lightDirEye);
        float specularFactor = max(0.0, dot(halfwayEye, input.normalEye));
        float3 iSpecular = specularStrength * pow(specularFactor, 2*specularExponent);

        dirLightIntensity = (iAmbient + iDiffuse + iSpecular) * lightColor;
    }
    // Point Light
    float3 pointLightIntensity = float3(0,0,0);
    for(int i=0; i<2; ++i)
    {
        float ambientStrength = 0.1;
        float specularStrength = 0.9;
        float specularExponent = 100;
        float3 lightDirEye = pointLights[i].posEye.xyz - input.posEye;
        float inverseDistance = 1 / length(lightDirEye);
        lightDirEye *= inverseDistance; //normalise
        float3 lightColor = pointLights[i].color.xyz;

        float3 iAmbient = ambientStrength;

        float diffuseFactor = max(0.0, dot(input.normalEye, lightDirEye));
        float3 iDiffuse = diffuseFactor;

        float3 halfwayEye = normalize(fragToCamDir + lightDirEye);
        float specularFactor = max(0.0, dot(halfwayEye, input.normalEye));
        float3 iSpecular = specularStrength * pow(specularFactor, 2*specularExponent);

        pointLightIntensity += (iAmbient + iDiffuse + iSpecular) * lightColor * inverseDistance;
    }

    float3 result = (dirLightIntensity + pointLightIntensity) * diffuseColor;

    return float4(result, 1.0);
}