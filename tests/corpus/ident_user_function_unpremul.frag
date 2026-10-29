#version 320 es
precision highp float;
out vec4 color;
vec4 unpremul(vec4 c){return vec4(c.rgb/max(c.a,.01),c.a);} void main(){color=unpremul(vec4(.5));}
