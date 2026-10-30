#version 320 es
precision highp float;
out vec4 color;
void main(){float v=gl_FragCoord.x*gl_FragCoord.x;color=vec4(dFdx(dFdx(v)),dFdy(dFdx(v)),0.,1.);}
