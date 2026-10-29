#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){float a=0.;switch(count){case 0:{float t=.5;a=t;}break;case 1:{float t=.25;a=t;}break;}color=vec4(a);}
