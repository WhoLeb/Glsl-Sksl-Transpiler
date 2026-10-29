#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(faceforward(vec3(1.),vec3(0.,0.,1.),vec3(0.,0.,-1.)).x,refract(vec3(0.,0.,-1.),vec3(0.,0.,1.),.5).z,0.,1.);}
