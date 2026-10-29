#version 320 es
precision highp float;
out vec4 color;
void main(){vec4 a=vec4(1.);vec4 b=vec4(vec2(1.),vec2(2.));vec4 c=vec4(vec3(1.),2.);vec4 d=vec4(1.,vec3(2.));vec4 e=vec4(1.,vec2(2.),3.);color=a+b+c+d+e;}
