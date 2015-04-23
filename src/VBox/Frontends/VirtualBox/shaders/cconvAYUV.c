/* $Id: cconvAYUV.c 99740 2015-04-23 10:03:17Z noreply@oracle.com $ */
#extension GL_ARB_texture_rectangle : enable
uniform sampler2DRect uSrcTex;
void vboxCConvApplyAYUV(vec4 color);
void vboxCConv()
{
    vec2 srcCoord = vec2(gl_TexCoord[0]);
    vec4 color = texture2DRect(uSrcTex, srcCoord);
    vboxCConvApplyAYUV(color);
}
