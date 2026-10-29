#version 320 es
precision highp float;
out vec4 color;
const float W[3]=float[3](.2,.3,.5); const float X=W[1]; float Y=W[2]; void main(){color=vec4(X,Y,0.,1.);}
