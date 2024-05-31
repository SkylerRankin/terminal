#version 430

uniform float time;
out vec4 outColor;

void main() {
    float opacity = abs(sin(time * 4)) * 0.8;
    outColor = vec4(1.0, 1.0, 1.0, opacity);
};
