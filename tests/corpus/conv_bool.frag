#version 320 es
precision highp float;
out vec4 color;
void main(){float a=float(true),b=float(false);int c=int(true);bool d=bool(.5),e=bool(0),f=bool(0.);color=vec4(a+b,float(c),d?1.:0.,e||f?1.:0.);}
