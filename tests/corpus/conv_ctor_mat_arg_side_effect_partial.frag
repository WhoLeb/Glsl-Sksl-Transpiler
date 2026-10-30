#version 320 es
precision highp float;
out vec4 color;
float n=0.; mat3 h(){n+=.25;return mat3(n);} void main(){vec3 a=vec3(.25,h());color=vec4(a.y,float(h()),n,1.);}
