#version 320 es
precision highp float;
out vec4 color;
mat2 m=mat2(1.);void rot(){m=mat2(0.,1.,-1.,0.)*m;}void main(){rot();color=vec4(m[0],m[1]);}
