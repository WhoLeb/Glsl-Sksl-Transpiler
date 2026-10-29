#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;for(int i=0;i<10;i+=2)a+=.1;color=vec4(a);}
