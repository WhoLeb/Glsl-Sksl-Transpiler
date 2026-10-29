#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
const float W[5]=float[5](.06,.24,.4,.24,.06);void main(){float s=0.;for(int i=0;i<5;++i){float x=position.x+float(i-2);s+=W[i]*sin(x*.1);}color=vec4(vec3(s),1.);}
