#version 320 es
precision highp float;
out vec4 color;
struct L{vec3 c;float i;}; void main(){L ls[2]=L[2](L(vec3(1.),.5),L(vec3(.2),1.));color=vec4(ls[1].c*ls[0].i,1.);}
