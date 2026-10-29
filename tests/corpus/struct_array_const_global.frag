#version 320 es
precision highp float;
out vec4 color;
struct S{float a[2];float b;}; const S C=S(float[2](.1,.2),.3); const float Z=C.b; void main(){color=vec4(C.a[0],C.b,Z,1.);}
