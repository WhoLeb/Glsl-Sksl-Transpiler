#version 320 es
precision highp float;
uniform samplerCube env;out vec4 color;
void main(){color=texture(env,vec3(.3,.5,-1.))+textureLod(env,vec3(1.,0.,0.),2.);}
