#version 320 es
precision highp float;
out vec4 color;
const float K=sqrt(2.)*3.;const vec2 V=vec2(K,K*.5);void main(){color=vec4(V/5.,0.,1.);}
