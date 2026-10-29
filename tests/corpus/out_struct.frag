#version 320 es
precision highp float;
struct S{vec4 a;}; out S c;
void main(){c.a=vec4(1.);}
