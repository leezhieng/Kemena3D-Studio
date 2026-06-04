#version 330 core

uniform vec3 cameraRightWorldSpace;
uniform vec3 cameraUpWorldSpace;
uniform mat4 viewProjection;
uniform vec3 billboardPosition;
uniform vec2 billboardSize;

layout(location = 0) in vec3 vertexPosition;

out vec2 v_uv;

void main()
{
    vec3 worldPos = billboardPosition
        + cameraRightWorldSpace * vertexPosition.x * billboardSize.x
        + cameraUpWorldSpace    * vertexPosition.y * billboardSize.y;

    // Flip V: images are uploaded top-row-first (top at v=0), but the quad's
    // bottom maps to v=0 — without this every icon samples upside-down.
    v_uv        = vec2(vertexPosition.x + 0.5, 0.5 - vertexPosition.y);
    gl_Position = viewProjection * vec4(worldPos, 1.0);
}

// --- FRAGMENT ---

#version 330 core

uniform sampler2D albedoMap;
uniform vec3      color;

const float alphaCutoff = 0.3;

// --- Drop shadow (adjustable) ------------------------------------------------
// Offset is in UV space: +x moves the shadow right, +y moves it down on screen.
// Set shadowOpacity to 0.0 to disable the shadow entirely.
const vec2  shadowOffset   = vec2(0.07, 0.07);
const vec3  shadowColor    = vec3(0.0, 0.0, 0.0);
const float shadowOpacity  = 0.55;
const float shadowSoftness = 0.012; // UV blur radius (0 = hard edge)

in  vec2 v_uv;
out vec4 fragColor;

// Icon coverage at uv. When softness > 0 a 3x3 tap average gives a soft shadow.
float iconCoverage(vec2 uv)
{
    if (shadowSoftness <= 0.0)
        return step(alphaCutoff, texture(albedoMap, uv).a);
    float a = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            a += step(alphaCutoff, texture(albedoMap, uv + vec2(x, y) * shadowSoftness).a);
    return a / 9.0;
}

void main()
{
    vec4 tex = texture(albedoMap, v_uv);
    if (tex.a >= alphaCutoff)
    {
        fragColor = tex * vec4(color, 1.0);
        return;
    }

    // Outside the icon — draw its drop shadow (the icon shape shifted by offset).
    float s = (shadowOpacity > 0.0) ? iconCoverage(v_uv - shadowOffset) : 0.0;
    if (s > 0.0)
    {
        fragColor = vec4(shadowColor, shadowOpacity * s);
        return;
    }
    discard;
}
