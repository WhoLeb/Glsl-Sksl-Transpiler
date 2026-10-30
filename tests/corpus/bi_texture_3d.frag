#version 320 es
precision highp float;
uniform highp sampler3D vol; // @wrap{clamp}
out vec4 color;
void main(){color=texture(vol,vec3(.5))+texelFetch(vol,ivec3(1),0);}
