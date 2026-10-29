#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;switch(2){case 1:a=.1;break;case 2:a=.2;break;}color=vec4(a);}
