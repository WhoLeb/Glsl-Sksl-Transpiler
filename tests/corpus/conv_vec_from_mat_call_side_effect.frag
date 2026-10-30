#version 320 es
precision highp float;
out vec4 color;
float n=0.; mat2 h(){n+=.25;return mat2(n);} void main(){vec3 a=vec3(h());color=vec4(a.x,n,0.,1.);}
