#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(normalize(vec2(3.,4.)),length(vec3(1.,2.,2.))/3.,1.);}
