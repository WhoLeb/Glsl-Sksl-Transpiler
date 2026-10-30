#version 320 es
precision highp float;
out vec4 color;
void main(){mat2 a[2];a[0]=mat2(1.);a[1]=mat2(2.);a[1][0][1]=.5;color=vec4(a[0][1],a[1][0])/2.;}
