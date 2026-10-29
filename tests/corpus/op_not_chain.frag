#version 320 es
precision highp float;
out vec4 color;
void main(){bool b=true;bool c=!!b,d=!!!b;color=vec4(c?1.:0.,d?1.:0.,0.,1.);}
