#version 320 es
precision highp float;
out vec4 color;
float f(bool b){return 1.;}float f(int i){return 2.;} void main(){color=vec4((f(true)+f(1))/3.);}
