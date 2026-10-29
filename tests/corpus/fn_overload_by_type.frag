#version 320 es
precision highp float;
out vec4 color;
float f(float x){return 1.;}float f(int x){return 2.;}float f(vec2 x){return 3.;}float f(vec3 x){return 4.;}float f(float x,float y){return 5.;} void main(){color=vec4((f(1.)+f(1)+f(vec2(1.))+f(vec3(1.))+f(1.,2.))/15.);}
