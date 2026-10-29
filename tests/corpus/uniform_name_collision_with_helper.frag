#version 320 es
precision highp float;
uniform float scale_;out vec4 color;
float scale(float x){return x*scale_;}void main(){color=vec4(scale(1.));}
