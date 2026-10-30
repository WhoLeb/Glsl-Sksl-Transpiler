#extension GL_EXT_shader_texture_lod : enable
precision mediump float;
uniform samplerCube env;
uniform sampler2D t;
void main(){gl_FragColor=textureCube(env,vec3(1.))+textureCubeLodEXT(env,vec3(1.),1.)+texture2DLodEXT(t,vec2(.5),1.);}
