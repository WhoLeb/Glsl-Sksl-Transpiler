#version 320 es
precision highp float;
out vec4 color;
float a=1.; float b=a*2.; void main(){color=vec4(b);}
