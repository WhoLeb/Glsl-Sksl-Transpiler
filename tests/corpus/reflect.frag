#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(reflect(vec3(0.,-1.,0.),vec3(0.,1.,0.)),1.);}
