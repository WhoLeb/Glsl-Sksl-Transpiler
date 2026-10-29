#version 320 es
precision highp float;
out vec4 color;
void main(){bool a=true,b=false;bool c=a^^b;bool d=a&&b||!a;color=vec4(c?1.:0.,d?1.:0.,0.,1.);}
