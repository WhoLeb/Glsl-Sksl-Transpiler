#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;for(int i=0;i<4;++i){switch(i){case 1:break;default:a+=.1;}a+=.05;}color=vec4(a);}
