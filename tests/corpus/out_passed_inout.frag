#version 320 es
precision highp float;
out vec4 color;
void tint(inout vec4 c){c.rgb*=.5;} void main(){color=vec4(1.);tint(color);}
