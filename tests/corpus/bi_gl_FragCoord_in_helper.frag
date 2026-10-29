#version 320 es
precision highp float;
uniform vec2 size;out vec4 color;
float f(){return gl_FragCoord.x;}void main(){color=vec4(f()/size.x);}
