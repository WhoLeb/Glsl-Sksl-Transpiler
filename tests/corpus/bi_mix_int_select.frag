#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(mix(ivec2(0),ivec2(1),bvec2(true,false)),0,1);}
