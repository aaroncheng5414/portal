#version 450

layout(location = 0) in vec3 viewNormal;
layout(location = 0) out vec4 color;

void main() {
    vec3 n = normalize(viewNormal);
    vec3 light = normalize(vec3(-0.45, 0.75, 1.0));
    float diffuse = max(dot(n, light), 0.0);
    vec3 halfway = normalize(light + vec3(0.0, 0.0, 1.0));
    float specular = pow(max(dot(n, halfway), 0.0), 28.0);
    vec3 baseColor = vec3(0.62, 0.22, 0.075);
    color = vec4(baseColor * (0.22 + 0.78 * diffuse) + vec3(0.24 * specular), 1.0);
}
