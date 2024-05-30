#version 430

in vec3 vertexPosition;
uniform mat4 paddingTransform;

void main() {
   gl_Position = paddingTransform * vec4(vertexPosition, 1.0);
};
