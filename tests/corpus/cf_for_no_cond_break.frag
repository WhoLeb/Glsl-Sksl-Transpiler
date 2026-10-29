#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;for(;;){a+=.1;if(a>.3)break;}color=vec4(a);}
