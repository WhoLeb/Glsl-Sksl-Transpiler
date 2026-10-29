#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(1.);color+=vec4(1.);color-=vec4(.5);color*=vec4(.5);color/=vec4(2.);}
