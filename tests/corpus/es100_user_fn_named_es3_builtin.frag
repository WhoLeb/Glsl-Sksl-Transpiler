#version 100
precision mediump float;
float round(float x){return floor(x+.5);}
float sinh(float x){return x;}
mat2 transpose(mat2 m){return mat2(m[0][0],m[1][0],m[0][1],m[1][1]);}
void main(){mat2 m=transpose(mat2(1.,2.,3.,4.));gl_FragColor=vec4(round(.6),sinh(.5),m[0])/4.;}
