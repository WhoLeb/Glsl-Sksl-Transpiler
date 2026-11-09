#version 320 es
precision highp float;
out vec4 color;
void main(){uint a=0xFFFFFFFFu;color=vec4(a>1u?1.:0.,a/2u>1u?1.:0.,float(a>>31u),1.);}
