#version 320 es
precision highp float;
uniform vec2 point;uniform vec2 center;uniform float radius;out vec4 color;
void main(){float d=length(point-center)-radius;float a=1.-smoothstep(-1.,1.,d);color=vec4(vec3(a),a);}
