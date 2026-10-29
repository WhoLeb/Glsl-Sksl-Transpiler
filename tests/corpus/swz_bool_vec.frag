#version 320 es
precision highp float;
out vec4 color;
void main(){bvec4 v=bvec4(true,false,true,false);bvec2 w=v.zw;color=vec4(w.x?1.:0.,w.y?1.:0.,v.x?1.:0.,1.);}
