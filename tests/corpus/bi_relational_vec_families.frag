#version 320 es
precision highp float;
out vec4 color;
void main(){vec2 a=vec2(1.,2.),b=vec2(2.,2.);color=vec4(lessThan(a,b).x?1.:0.,lessThanEqual(a,b).y?1.:0.,greaterThan(a,b).x?1.:0.,greaterThanEqual(a,b).y?1.:0.);}
