#version 320 es
precision highp float;
out vec4 color;
void main(){color=mix(vec4(0.),vec4(1.),clamp(.3,0.,1.));}
