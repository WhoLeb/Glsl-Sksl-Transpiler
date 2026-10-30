#version 320 es
precision highp float;
uniform float u;out vec4 color;
void main(){float v=gl_FragCoord.x;color=vec4(0.);if(u>0.)color=vec4(dFdx(v*v));}
