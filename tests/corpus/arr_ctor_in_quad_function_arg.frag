#version 320 es
precision highp float;
out vec4 color;
float g(float a[2]){return dFdx(a[0]);} void main(){color=vec4(g(float[2](gl_FragCoord.x,1.)));}
