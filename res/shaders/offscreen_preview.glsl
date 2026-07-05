#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 3) in vec3 aNormal;
layout(location = 5) in vec3 aBitangent;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform vec3 viewPos;

out vec3 vNormal;
out vec3 vBitangent;
out vec3 vFragPos;

void main() {
    mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));
    vFragPos    = vec3(modelMatrix * vec4(aPosition, 1.0));
    vNormal     = normalize(normalMatrix * aNormal);
    vBitangent  = normalize(normalMatrix * aBitangent);
    gl_Position = projectionMatrix * viewMatrix * vec4(vFragPos, 1.0);
}

// --- FRAGMENT ---

#version 330 core

in vec3 vNormal;
in vec3 vBitangent;
in vec3 vFragPos;

uniform vec3 viewPos;

out vec4 fragColor;

void main() {
    vec3  n      = normalize(vNormal);
    vec3  b      = normalize(vBitangent);
    vec3  V      = normalize(viewPos - vFragPos);
    float dN     = dot(n, V) * 0.5 + 0.5;
    float dB     = dot(b, V) * 0.5 + 0.5;
    float top    = n.y * 0.5 + 0.5;
    float shade  = dN * 0.50 + dB * 0.25 + top * 0.25;
    vec3  albedo = vec3(0.75, 0.75, 0.75);
    fragColor    = vec4(albedo * (0.10 + shade * 0.90), 1.0);
}
