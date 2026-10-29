#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){float a=0.;switch(count){case 0:switch(count+1){case 1:a=.5;break;}break;default:a=.1;}color=vec4(a);}
