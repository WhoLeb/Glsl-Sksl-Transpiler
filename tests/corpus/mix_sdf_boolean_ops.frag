#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
float circle(vec2 p,float r){return length(p)-r;}float box(vec2 p,vec2 b){vec2 d=abs(p)-b;return length(max(d,0.))+min(max(d.x,d.y),0.);}float smin(float a,float b,float k){float h=clamp(.5+.5*(b-a)/k,0.,1.);return mix(b,a,h)-k*h*(1.-h);}void main(){vec2 p=position-.5*size;float d=smin(circle(p,40.),box(p-vec2(30.,0.),vec2(30.,20.)),10.);color=vec4(vec3(1.-smoothstep(0.,2.,d)),1.);}
