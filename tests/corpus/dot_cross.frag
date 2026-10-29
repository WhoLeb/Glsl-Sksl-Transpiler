#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(cross(vec3(1.,0.,0.),vec3(0.,1.,0.)),dot(vec2(1.),vec2(.5)));}
