#version 320 es
precision highp float;
out vec4 color;
float n=0.; float h(){n+=1.;return n;} float f(float a[2],float b){return a[0]+b;} void main(){color=vec4(f(float[2](h(),1.),n));}
