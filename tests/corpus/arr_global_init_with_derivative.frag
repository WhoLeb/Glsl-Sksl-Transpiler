#version 320 es
precision highp float;
out vec4 color;
float G[2]=float[2](.25,.5); void main(){G[1]+=gl_FragCoord.x; color=vec4(G[0],dFdx(G[1]),0.,1.);}
