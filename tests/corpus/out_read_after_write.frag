#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(.5);color.rgb*=.5;color.a=1.;}
