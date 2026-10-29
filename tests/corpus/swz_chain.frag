#version 320 es
precision highp float;
out vec4 color;
void main(){vec4 v=vec4(1.,2.,3.,4.);color=v.xyzw.wzyx.xy.yx.xxyy/4.;}
