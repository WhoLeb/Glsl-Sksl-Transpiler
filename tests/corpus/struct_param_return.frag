#version 320 es
precision highp float;
out vec4 color;
struct S{float x;vec2 y;}; S make(float a){S s;s.x=a;s.y=vec2(a);return s;} float get(S s){return s.x+s.y.x;} void main(){color=vec4(get(make(.25)));}
