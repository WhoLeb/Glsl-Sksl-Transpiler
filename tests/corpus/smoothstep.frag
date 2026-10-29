#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(smoothstep(0.,1.,.4));}
