#version 320 es
precision highp float;
out vec4 color;
void main(){int i=0;int k=0;while(i++<5){k++;}color=vec4(float(k)/10.,float(i)/10.,0.,1.);}
