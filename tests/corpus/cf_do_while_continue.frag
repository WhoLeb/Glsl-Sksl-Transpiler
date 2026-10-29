#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;int i=0;do{++i;if(i==2)continue;a+=.1;}while(i<4);color=vec4(a);}
