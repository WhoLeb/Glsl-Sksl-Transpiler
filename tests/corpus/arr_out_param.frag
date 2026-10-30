#version 320 es
precision highp float;
out vec4 color;
void fill(out float a[2]){a[0]=.25;a[1]=.5;} void main(){float x[2];fill(x);color=vec4(x[0],x[1],0.,1.);}
