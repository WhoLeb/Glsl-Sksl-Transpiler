#version 320 es
precision highp float;
out vec4 color;
void main(){bool b=vec2(1.)<vec2(2.);color=vec4(b?1.:0.);}
