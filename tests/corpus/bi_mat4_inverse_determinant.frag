#version 320 es
precision highp float;
out vec4 color;
void main(){mat4 m=mat4(2.);m[3]=vec4(1.,2.,3.,1.);mat4 i=inverse(m);color=vec4(i[3].xyz/-2.,determinant(m)/16.);}
