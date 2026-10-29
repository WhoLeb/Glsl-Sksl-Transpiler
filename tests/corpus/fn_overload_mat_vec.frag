#version 320 es
precision highp float;
out vec4 color;
float f(mat2 m){return 1.;}float f(vec4 v){return 2.;} void main(){color=vec4((f(mat2(1.))+f(vec4(1.)))/3.);}
