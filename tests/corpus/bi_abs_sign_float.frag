#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(abs(-.5),sign(-3.),sign(0.)+1.,1.);}
