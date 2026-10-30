#version 100
precision mediump float;
#if __VERSION__ == 100
#define V .25
#endif
void main(){gl_FragColor=vec4(V);}
