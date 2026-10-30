#version 320 es
precision highp float;
out vec4 color;
struct S{float a[2];}; void main(){S x=S(float[2](1.,2.));S y=S(float[2](1.,2.));color=vec4(x==y?1.:0.,x!=y?1.:0.,0.,1.);}
