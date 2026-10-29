#version 320 es
precision highp float;
out vec4 color;
void main(){bvec3 b=bvec3(true,false,true);color=vec4(any(b)?1.:0.,all(b)?1.:0.,b.x==b.y?1.:0.,not(b).y?1.:0.);}
