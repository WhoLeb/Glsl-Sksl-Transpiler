#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){float a=0.;for(int i=0;i<count;i++){ // @maxiter{32}
a+=.1;}color=vec4(a);}
