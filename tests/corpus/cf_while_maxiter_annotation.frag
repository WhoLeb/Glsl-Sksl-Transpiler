#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){float d=1.;while(d>time){ // @maxiter{64}
d*=.5;}color=vec4(d);}
