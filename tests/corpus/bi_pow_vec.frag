#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(pow(vec3(2.),vec3(1.,2.,3.))/8.,1.);}
