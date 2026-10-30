#version 100
precision mediump float;
float uint(float x){return x;}
void main(){float uint2=.25;gl_FragColor=vec4(uint(.5),uint2,0.,1.);}
