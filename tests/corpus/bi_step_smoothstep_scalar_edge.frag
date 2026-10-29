#version 320 es
precision highp float;
out vec4 color;
void main(){vec3 v=vec3(.2,.5,.8);color=vec4(step(.5,v),smoothstep(0.,1.,v).y);}
