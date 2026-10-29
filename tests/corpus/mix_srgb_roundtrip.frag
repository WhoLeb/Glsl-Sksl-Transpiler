#version 320 es
precision highp float;
out vec4 color;
vec3 toLin(vec3 c){return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(.04045,c));}vec3 toSrgb(vec3 c){return mix(c*12.92,1.055*pow(c,vec3(1./2.4))-.055,step(.0031308,c));}void main(){color=vec4(toSrgb(toLin(vec3(.5))),1.);}
