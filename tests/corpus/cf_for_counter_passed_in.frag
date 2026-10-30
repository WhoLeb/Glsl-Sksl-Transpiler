#version 320 es
precision highp float;
out vec4 color;
float w(int i){return float(i)*.1;} void main(){float a=0.;for(int i=0;i<4;++i)a+=w(i);color=vec4(a);}
