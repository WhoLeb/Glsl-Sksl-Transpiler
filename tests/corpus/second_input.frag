#version 320 es
precision highp float;
layout(location = 0) in vec2 uv;layout(location = 1) in vec4 tint;out vec4 color;
void main(){color=vec4(uv+tint.xy,0.,1.);}
