#version 320 es
precision highp float;
uniform sampler2D t;out vec4 color;
void main(){color=textureLod(t,vec2(.5),0.)+textureProj(t,vec3(.5,.5,1.))+textureGrad(t,vec2(.5),vec2(0.),vec2(0.));}
