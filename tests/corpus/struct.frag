#version 320 es
precision highp float;
struct Value {float x;};out vec4 color;
void main(){Value v;v.x=.5;color=vec4(v.x);}
