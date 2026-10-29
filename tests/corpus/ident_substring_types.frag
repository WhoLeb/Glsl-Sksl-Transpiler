#version 320 es
precision highp float;
out vec4 color;
void main(){float vec2s=.1,ivec2x=.2,mat3y=.3,bvec4_=.4,uvec,_vec3=.5;uvec=.6;color=vec4(vec2s+ivec2x+mat3y+bvec4_+uvec+_vec3);}
