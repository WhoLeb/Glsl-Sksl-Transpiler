#version 320 es
precision highp float;
out vec4 color;
float saturate(float x){return clamp(x,0.,1.);} void main(){color=vec4(saturate(2.));}
