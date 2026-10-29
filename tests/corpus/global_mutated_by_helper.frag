#version 320 es
precision highp float;
out vec4 color;
float g=0.;void bump(){g+=.1;}void main(){bump();bump();color=vec4(g);}
