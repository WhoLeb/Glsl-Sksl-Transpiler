#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
vec2 h2(vec2 p){return fract(sin(vec2(dot(p,vec2(127.1,311.7)),dot(p,vec2(269.5,183.3))))*43758.5453);}void main(){vec2 p=position*.05;vec2 i=floor(p),f=fract(p);float m=8.;for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){vec2 g=vec2(float(x),float(y));vec2 o=h2(i+g);m=min(m,length(g+o-f));}color=vec4(vec3(m),1.);}
