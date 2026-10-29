#version 320 es
precision highp float;
out vec4 color;
struct S{float a;vec2 b;}; void main(){S x=S(1.,vec2(2.)),y=S(1.,vec2(2.));color=vec4(x==y?1.:0.,x!=y?1.:0.,0.,1.);}
