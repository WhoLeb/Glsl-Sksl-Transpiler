#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){float a=0.;for(float f=0.;f<size.x;f+=1.)a+=.1;color=vec4(a);}
