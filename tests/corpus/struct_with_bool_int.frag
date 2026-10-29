#version 320 es
precision highp float;
out vec4 color;
struct S{bool b;int i;ivec2 iv;bvec2 bv;}; void main(){S s=S(true,2,ivec2(3),bvec2(false));color=vec4(s.b?1.:0.,float(s.i+s.iv.x)/5.,s.bv.x?1.:0.,1.);}
