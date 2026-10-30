#version 320 es
precision highp float;
uniform float u;out vec4 color;
void main(){mat2 m=u>0.?mat2(1.):mat2(.5);color=vec4(m[0],m[1]);}
