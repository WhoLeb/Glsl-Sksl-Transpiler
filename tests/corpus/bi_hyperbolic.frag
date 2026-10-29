#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(sinh(.5),cosh(.5),tanh(.5),1.);}
