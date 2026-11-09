#version 320 es
precision highp float;
out vec4 color;
uint pcg(uint v){uint s=v*747796405u+2891336453u;uint w=((s>>((s>>28u)+4u))^s)*277803737u;return (w>>22u)^w;}void main(){color=vec4(float(pcg(1u))/4294967295.);}
