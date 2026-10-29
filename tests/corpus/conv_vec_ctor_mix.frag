#version 320 es
precision highp float;
out vec4 color;
void main(){vec4 a=vec4(1,2,3,4);vec3 b=vec3(vec4(1.));vec2 c=vec2(mat2(1.));ivec2 d=ivec2(vec2(1.5,2.5));bvec2 e=bvec2(vec2(0.,1.));color=vec4(a.x,b.x,c.x,float(d.x)+(e.y?1.:0.));}
