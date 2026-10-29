#version 320 es
precision highp float;
out vec4 color;
void f(out vec4 v){v=vec4(1.);}void main(){f(gl_FragCoord);color=vec4(1.);}
