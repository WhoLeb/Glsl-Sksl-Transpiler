#version 320 es
precision highp float;
out vec4 color;
void main(){bool b=1.<2.<3.;color=vec4(b?1.:0.);}
