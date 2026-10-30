#version 320 es
precision highp float;
uniform float u;out vec4 color;
void main(){if(u>1.){color=vec4(1.);return;}color=vec4(dFdx(gl_FragCoord.x));}
