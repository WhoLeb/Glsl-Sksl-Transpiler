#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(dFdx(float[2](gl_FragCoord.x,1.)[0]));}
