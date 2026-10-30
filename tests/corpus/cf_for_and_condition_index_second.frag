#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){float t=0.;for(int i=0;t<time&&i<16;i++){t+=.25;}color=vec4(t);}
