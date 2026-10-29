#version 320 es
precision highp float;
out vec4 color;
struct Light{vec3 dir;vec3 col;float k;};vec3 shade(Light l,vec3 n){return l.col*max(dot(n,normalize(l.dir)),0.)*l.k;}void main(){Light ls[2];ls[0]=Light(vec3(1.),vec3(1.,.5,.5),1.);ls[1]=Light(vec3(-1.,1.,0.),vec3(.2,.2,1.),.5);vec3 c=vec3(0.);for(int i=0;i<2;++i)c+=shade(ls[i],vec3(0.,1.,0.));color=vec4(c,1.);}
