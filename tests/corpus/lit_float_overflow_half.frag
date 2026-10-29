#version 320 es
precision highp float;
out vec4 color;
void main(){mediump float big=70000.;color=vec4(big/70000.);}
