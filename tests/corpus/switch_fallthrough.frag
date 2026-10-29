#version 320 es
precision highp float;
uniform int count;uniform int start;uniform int step;out vec4 color;
void main(){float a=0.;switch(count){case 0:a+=.1;case 1:a+=.2;break;default:a=.5;}color=vec4(a);}
