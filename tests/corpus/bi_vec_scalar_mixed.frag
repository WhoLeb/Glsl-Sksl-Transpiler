#version 320 es
precision highp float;
out vec4 color;
void main(){vec3 v=vec3(1.,2.,3.);color=vec4(mod(v,2.),min(v,2.).x+max(v,2.).y+clamp(v,1.,2.).z);}
