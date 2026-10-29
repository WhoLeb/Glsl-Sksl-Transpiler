#version 320 es
precision highp float;
out vec4 color;
float f(float a[2]){return 1.;}float f(float a[3]){return 2.;} void main(){float x[2];float y[3];color=vec4((f(x)+f(y))/3.);}
