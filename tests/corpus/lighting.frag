#version 320 es
precision highp float;
uniform vec3 normal;uniform vec3 light;out vec4 color;
void main(){vec3 n=normalize(normal);float d=max(dot(n,normalize(light)),0.);color=vec4(vec3(.1)+vec3(.8)*d,1.);}
