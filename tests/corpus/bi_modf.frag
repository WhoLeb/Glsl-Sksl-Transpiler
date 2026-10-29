#version 320 es
precision highp float;
out vec4 color;
void main(){float i;float f=modf(2.75,i);color=vec4(f,i/4.,0.,1.);}
