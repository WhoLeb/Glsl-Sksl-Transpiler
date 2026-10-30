#version 320 es
precision highp float;
out vec4 color;
vec2 sin(vec2 a,vec2 b){return a+b;} void main(){color=vec4(sin(vec2(.1),vec2(.2)),0.,1.);}
