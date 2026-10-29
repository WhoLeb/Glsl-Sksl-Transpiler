#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(roundEven(2.5),roundEven(3.5),0.,1.)/4.;}
