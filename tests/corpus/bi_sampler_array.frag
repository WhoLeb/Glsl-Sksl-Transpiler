#version 320 es
precision highp float;
uniform sampler2D t[2];out vec4 color;
void main(){color=texture(t[0],vec2(.5));}
