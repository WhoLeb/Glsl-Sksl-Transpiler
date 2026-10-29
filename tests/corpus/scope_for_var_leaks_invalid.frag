#version 320 es
precision highp float;
out vec4 color;
void main(){for(int i=0;i<2;++i){}color=vec4(float(i));}
