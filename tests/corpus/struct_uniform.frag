#version 320 es
precision highp float;
out vec4 color;
struct S{vec3 a;float b;}; uniform S u;void main(){color=vec4(u.a,u.b);}
