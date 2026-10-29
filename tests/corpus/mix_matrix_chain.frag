#version 320 es
precision highp float;
out vec4 color;
void main(){mat4 m=mat4(1.);m[3]=vec4(.1,.2,.3,1.);vec4 p=m*m*vec4(0.,0.,0.,1.);color=vec4(p.xyz,1.);}
