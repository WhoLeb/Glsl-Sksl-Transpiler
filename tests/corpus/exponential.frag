#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(exp(.2),log(2.),sqrt(.5),1.);}
