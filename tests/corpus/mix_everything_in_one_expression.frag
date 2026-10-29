#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(mix(vec3(.1,.2,.3).bgr,vec3(min(max(abs(-.5),.1),.9)),clamp(dot(normalize(vec3(1.)),vec3(.5))*2.,0.,1.)),true?1.:0.);}
