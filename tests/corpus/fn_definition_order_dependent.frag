#version 320 es
precision highp float;
out vec4 color;
float a(){return .25;}float b(){return a()*2.;}float c(){return b()*2.;}void main(){color=vec4(c());}
