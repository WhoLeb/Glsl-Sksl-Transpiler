#version 320 es
precision highp float;
out vec4 color;
void main(){vec3 a=vec3(mat2(1.,2.,3.,4.));float b=float(vec3(.25,.5,.75));ivec2 c=ivec2(vec4(1.5,2.5,3.5,4.5));bvec2 d=bvec2(vec3(0.,1.,1.));color=vec4(a.z/4.,b,float(c.y)/4.,d.y?1.:0.);}
