#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){float a=0.;switch(count){case 0:a=.1;break;case 1:a=.2;break;default:a=.3;}color=vec4(a);}
