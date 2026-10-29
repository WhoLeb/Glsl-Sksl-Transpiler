#version 320 es
precision highp float;
out vec4 color;
void main(){vec2 a=vec2(1.),b,c=vec2(3.);b=a+c;color=vec4(b/4.,0.,1.);}
