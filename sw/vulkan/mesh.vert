#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 0) out vec3 viewNormal;

layout(push_constant) uniform View {
    vec2 scale;
    float angle;
} view;

vec3 rotate(vec3 v) {
    float c = cos(view.angle);
    float s = sin(view.angle);
    v = vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
    // Tilt the view to show the top of the model.
    return vec3(v.x, 0.94 * v.y - 0.34 * v.z, 0.34 * v.y + 0.94 * v.z);
}

void main() {
    vec3 p = rotate(position);
    gl_Position = vec4(p.xy * view.scale, 0.5 - 0.4 * p.z, 1.0);
    viewNormal = rotate(normal);
}
