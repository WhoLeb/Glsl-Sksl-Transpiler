precision mediump float;
void main(){gl_FragColor=vec4(gl_DepthRange.far,gl_FrontFacing?1.:0.,0.,1.);}
