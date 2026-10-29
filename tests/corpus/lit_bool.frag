#version 320 es
precision highp float;
out vec4 color;
void main(){bool a=true,b=false;color=vec4(a&&!b?1.:0.);}
