#version 320 es
precision highp float;
uniform vec2 point;uniform vec2 size;out vec4 color;
void main(){vec2 uv=point/size;color=vec4(uv,.5,1.);}
