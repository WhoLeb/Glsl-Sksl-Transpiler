#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(mix(vec3(0.),vec3(1.),.5),mix(0.,1.,.25));}
