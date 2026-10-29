#version 320 es
precision highp float;
out vec4 color;
void main(){bool a=vec2(1.)==vec2(1.);bool b=mat2(1.)==mat2(1.);color=vec4(a&&b?1.:0.);}
