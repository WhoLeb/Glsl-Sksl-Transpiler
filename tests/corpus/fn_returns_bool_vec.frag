#version 320 es
precision highp float;
out vec4 color;
bvec2 f(){return bvec2(true,false);}void main(){color=vec4(f().x?1.:0.,f().y?1.:0.,0.,1.);}
