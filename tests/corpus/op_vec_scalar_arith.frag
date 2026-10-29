#version 320 es
precision highp float;
out vec4 color;
void main(){vec3 v=vec3(1.)*2.+1.-vec3(.5)/2.;vec3 w=2.-v;vec3 x=1./v;color=vec4(w,x.x);}
