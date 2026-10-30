#version 320 es
precision highp float;
out vec4 color;
void main(){float s=0.;for(int i=0;i<2;++i){int i=1;s+=float(i);}color=vec4(s);}
