#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;for(float f=0.;f<1.;f+=.25)a+=f;color=vec4(a);}
