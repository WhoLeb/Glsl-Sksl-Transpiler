#version 320 es
precision highp float;
out vec4 color;
vec2 f(){return vec2(1.);}void main(){f().x=1.;color=vec4(1.);}
