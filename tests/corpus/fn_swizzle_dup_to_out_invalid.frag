#version 320 es
precision highp float;
out vec4 color;
void f(out vec2 x){x=vec2(1.);}void main(){vec3 v;f(v.xx);color=vec4(v,1.);}
