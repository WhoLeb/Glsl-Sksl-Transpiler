#version 320 es
precision highp float;
out vec4 color;
float glsl_sinh(float x){return x;} void main(){float tmp1=.1,ret=.1,done=.1,result=.1,quad=.1,fragCoord=.1,coord=.1,v_L1=.1;float v=gl_FragCoord.x;color=vec4(sinh(.5)+glsl_sinh(.1),tmp1+ret+done+result,quad+fragCoord+coord+v_L1,dFdx(v));}
