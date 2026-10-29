#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
float map(vec3 p){return length(p)-1.;}void main(){vec2 uv=(position-.5*size)/size.y;vec3 ro=vec3(0.,0.,-3.),rd=normalize(vec3(uv,1.));float t=0.;for(int i=0;i<32;++i){float d=map(ro+rd*t);if(d<.001)break;t+=d;}color=vec4(vec3(1.-t/6.),1.);}
