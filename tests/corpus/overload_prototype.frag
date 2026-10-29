#version 320 es
precision highp float;
out vec4 color;
float f(float x);float f(float x){return x*.5;}vec2 f(vec2 x){return x*.25;}void main(){color=vec4(f(vec2(1.)),f(1.),1.);}
