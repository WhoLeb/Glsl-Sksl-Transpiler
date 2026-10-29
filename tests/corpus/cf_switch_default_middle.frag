#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){float a=0.;switch(count){case 0:a+=.1;default:a+=.2;case 1:a+=.3;break;case 2:a+=.4;}color=vec4(a);}
