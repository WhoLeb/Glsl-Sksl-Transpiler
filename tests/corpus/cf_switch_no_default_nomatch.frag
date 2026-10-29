#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){float a=.5;switch(count){case 100:a=1.;break;}color=vec4(a);}
