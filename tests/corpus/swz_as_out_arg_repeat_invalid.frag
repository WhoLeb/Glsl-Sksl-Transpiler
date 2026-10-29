#version 320 es
precision highp float;
out vec4 color;
void f(out vec2 a){a=vec2(1.);} void main(){vec4 v;f(v.xx);color=v;}
