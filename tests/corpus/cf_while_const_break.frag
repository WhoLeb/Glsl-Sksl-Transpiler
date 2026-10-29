#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;int i=0;while(true){a+=.1;if(++i>3)break;}color=vec4(a);}
