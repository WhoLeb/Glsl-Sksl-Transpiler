#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
const int K=2;void main(){float a=0.;switch(count){case K+1:a=.5;break;case 1<<1:a=.25;break;}color=vec4(a);}
