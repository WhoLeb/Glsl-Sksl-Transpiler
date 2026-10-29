#version 320 es
precision highp float;
out vec4 color;
void f(out vec2 a){a=vec2(1.,2.);} void main(){vec4 v=vec4(0.);f(v.zw);color=v/2.;}
