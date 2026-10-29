#version 320 es
precision highp float;
out vec4 color;
void main(){vec3 v=vec3(1.);v*=2.;v+=1.;v-=.5;v/=2.;color=vec4(v,1.);}
