#version 320 es
precision highp float;
out vec4 color;
void main(){vec3 a=vec3(vec2(1.,2.),vec2(3.,4.));vec2 b=vec2(1.,vec3(2.,3.,4.));color=vec4(a,b.y)/4.;}
