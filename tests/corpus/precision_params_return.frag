#version 320 es
precision highp float;
out vec4 color;
mediump float f(highp float x,lowp float y){return x+y;} void main(){color=vec4(f(.1,.2));}
