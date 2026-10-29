#version 320 es
precision highp float;
out vec4 color;
mat2 f(){return mat2(2.);}void main(){color=vec4(f()[1],f()[0]);}
