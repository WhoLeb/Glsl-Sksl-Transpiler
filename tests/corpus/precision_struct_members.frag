#version 320 es
precision highp float;
out vec4 color;
struct S{lowp float a;mediump vec2 b;highp vec3 c;}; void main(){S s;s.a=.1;s.b=vec2(.2);s.c=vec3(.3);color=vec4(s.a+s.b.x+s.c.x);}
