#version 320 es
precision highp float;
out vec4 color;
float d(float x){return dFdx(x);} void main(){float a=d(gl_FragCoord.x);float b=d(gl_FragCoord.y*2.);color=vec4(a,b,0.,1.);}
