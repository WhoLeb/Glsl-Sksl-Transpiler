#version 320 es
precision highp float;
out vec4 color;
void main(){mat3 m=mat3(vec2(1.,2.),3.,vec3(4.,5.,6.),vec3(7.,8.,9.));color=vec4(m[0].z,m[1].x,m[2].z,1.)/9.;}
