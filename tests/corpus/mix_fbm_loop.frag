#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
float hash(vec2 p){return fract(sin(dot(p,vec2(12.9898,78.233)))*43758.5453);}float fbm(vec2 p){float v=0.,a=.5;for(int i=0;i<5;++i){v+=a*hash(floor(p));p*=2.;a*=.5;}return v;}void main(){color=vec4(vec3(fbm(position*.05)),1.);}
