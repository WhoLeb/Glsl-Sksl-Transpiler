#version 320 es
precision highp float;
out vec4 color;
const float A=1.;const float B=A+1.;const float C=A*B;void main(){color=vec4(C/2.);}
