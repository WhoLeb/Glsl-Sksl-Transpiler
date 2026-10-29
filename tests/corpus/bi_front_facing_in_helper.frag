#version 320 es
precision highp float;
out vec4 color;
float f(){return gl_FrontFacing?1.:0.;}void main(){color=vec4(f());}
