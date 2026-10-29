#version 320 es
precision highp float;
out vec4 color;
void f(out vec2 x){x=vec2(.5);}void main(){mat2 m=mat2(0.);f(m[1]);color=vec4(m[0],m[1]);}
