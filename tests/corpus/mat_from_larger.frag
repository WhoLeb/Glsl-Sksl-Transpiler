#version 320 es
precision highp float;
out vec4 color;
void main(){mat4 b=mat4(1.,2.,3.,4.,5.,6.,7.,8.,9.,10.,11.,12.,13.,14.,15.,16.);mat2 m=mat2(b);color=vec4(m[0],m[1])/16.;}
