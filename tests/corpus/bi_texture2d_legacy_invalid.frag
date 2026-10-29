#version 320 es
precision highp float;
uniform sampler2D t;out vec4 color;
void main(){color=texture2D(t,vec2(.5));}
