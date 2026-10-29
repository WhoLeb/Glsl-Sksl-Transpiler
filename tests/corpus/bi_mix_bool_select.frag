#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(mix(vec2(0.),vec2(1.),bvec2(true,false)),0.,1.);}
