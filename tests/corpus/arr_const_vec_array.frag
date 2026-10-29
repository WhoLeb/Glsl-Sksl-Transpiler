#version 320 es
precision highp float;
out vec4 color;
const vec2 P[2]=vec2[2](vec2(1.,2.),vec2(3.,4.));void main(){color=vec4(P[0],P[1])/4.;}
