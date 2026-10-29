#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;do{a+=.5;}while(false);color=vec4(a);}
