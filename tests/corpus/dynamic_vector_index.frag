#version 320 es
precision highp float;
uniform int count;uniform int start;uniform int step;out vec4 color;
void main(){vec4 a=vec4(.1,.2,.3,.4);color=vec4(a[clamp(count,0,3)]);}
