#version 430

in vec3 vertexPosition;
uniform mat4 cursorTransform;

void main() {
   gl_Position = cursorTransform * vec4(vertexPosition, 1.0);
};
