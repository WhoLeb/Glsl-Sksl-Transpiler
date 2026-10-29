#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){int n=int(position.x)%4;float f=float(n)*.25+float(int(position.y)/2)*.01;color=vec4(f);}
