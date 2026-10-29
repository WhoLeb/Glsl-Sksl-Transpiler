#version 320 es
precision highp float;
out vec4 color;
void main(){int i=0;bool b=(i++>5)&&(i++>0);bool c=(i++<5)||(i++>0);color=vec4(float(i),b?1.:0.,c?1.:0.,1.);}
