#include "backends/opengl/gl_programs.h"

#include "log/log.h"
#include "zui/glyph_cover_glsl.h"

namespace zui {
namespace backends {

bool GlPrograms::create(GlContext& ctx, bool es) {
        es_ = es;
        // The desktop and ES profiles used to carry a byte-identical copy of each shader body, so
        // an edit had to be made twice or GL silently kept running the old shader. Each body is
        // one string now and the profile contributes only its `#version` head.
        // zui-shader-body: ui-vert
        const char* vs_body =
            "layout(location=0) in vec2 aPos;\n"
            "layout(location=1) in vec2 aUv;\n"
            "layout(location=2) in vec4 aCol;\n"
            "uniform vec2 uScreen;\n"
            "out vec2 vUv; out vec4 vCol;\n"
            "void main(){\n"
            "  vec2 p = vec2(aPos.x/uScreen.x*2.0-1.0, 1.0-aPos.y/uScreen.y*2.0);\n"
            "  gl_Position = vec4(p,0,1); vUv=aUv; vCol=aCol;\n"
            "}\n";
        // zui-shader-body: fill
        const char* fs_body =
            "in vec2 vUv; in vec4 vCol; out vec4 o;\n"
            "uniform sampler2D uTex; uniform int uMode; uniform vec2 uScreen; uniform float uScale;\n"
            "uniform int uRoundCount; uniform vec4 uRoundRect[4]; uniform vec4 uRoundR;\n"
            "uniform int uPolyCount; uniform vec2 uPoly[16];\n"
            "uniform vec4 uShape; uniform vec4 uShapeRadii; uniform vec4 uShapeRadiiY; uniform float uShapeStroke;\n"
            "uniform int uGradCount; uniform vec2 uGradP0; uniform vec2 uGradD; uniform float uGradLen;\n"
            "uniform vec4 uGradOff; uniform vec4 uGradCol[4];\n"
            "uniform int uAff2; uniform vec4 uAff2Inv0; uniform vec2 uAff2Inv1;\n"
            "uniform sampler2D uDst; uniform int uIntBlend;\n"
            "uniform float uShapeBlur;\n"
            "uniform int uGradRepeat; uniform int uRadial; uniform vec2 uRadialR;\n"
            "uniform int uConic; uniform float uConicFrom;\n"
            "uniform int uBlendMode; uniform int uMaskGrad; uniform float uGroupOpacity;\n"
            "uniform int uBlurRadius; uniform float uBlurSigma; uniform vec2 uBlurDir;\n"
            // The display list resolves the gradient line and its stops; the shader only has to walk
            // the stops along that line. Stops interpolate in premultiplied space, which is what CSS
            // Images 3 section 3.4.4 asks for and what gradient_color_at() does on the CPU path.
            // Negative uGradCount = abs stops with t-range clamp so >4-stop gradients can loop.
            // uAff2!=0: mode-2 samples SDF after inverse Aff2 (GPU rounded Aff2 FillRect).
            "vec4 grad(vec2 p){\n"
            "  float t;\n"
            "  if(uConic!=0){\n"
            "    vec2 d=p-uGradP0;\n"
            "    float ang=atan(d.x,-d.y);\n"
            "    t=(ang-uConicFrom)/6.28318530718;\n"
            "    t=t-floor(t);\n"
            "  } else if(uRadial!=0){\n"
            "    vec2 q=(p-uGradP0)/max(uRadialR,vec2(0.0001));\n"
            "    t=length(q);\n"
            "  } else {\n"
            "    t=dot(p-uGradP0,uGradD)/max(uGradLen,0.0001);\n"
            "  }\n"
            "  bool clamp_range=uGradCount<0;\n"
            "  int n=clamp_range?-uGradCount:uGradCount;\n"
            "  float t_lo=uGradOff.x;\n"
            "  float t_hi=(n<=1)?uGradOff.x:((n==2)?uGradOff.y:((n==3)?uGradOff.z:uGradOff.w));\n"
            // Repeating gradients tile the stop interval. Same wrap as lerp_stops: fmod into
            // [first, last) so a later period is not clamped to the last stop.
            "  if(uGradRepeat!=0){\n"
            "    float period=t_hi-t_lo;\n"
            "    if(period>0.000001){\n"
            "      float u=mod(t-t_lo,period);\n"
            "      if(u<0.0) u+=period;\n"
            "      t=t_lo+u;\n"
            "    }\n"
            "  }\n"
            "  if(clamp_range){\n"
            "    bool is_last=uShapeStroke>-1.5&&uShapeStroke<-0.5;\n"
            "    bool is_first=uShapeStroke<-1.5;\n"
            "    if(!is_first&&t<t_lo-1e-5) return vec4(0.0);\n"
            "    if(!is_last&&t>=t_hi-1e-5) return vec4(0.0);\n"
            "  }\n"
            "  vec4 prev=uGradCol[0]; float po=uGradOff.x;\n"
            "  if(t<=po) return prev;\n"
            "  for(int i=1;i<4;++i){\n"
            "    if(i>=n) break;\n"
            "    vec4 cur=uGradCol[i];\n"
            "    float co=(i==1)?uGradOff.y:((i==2)?uGradOff.z:uGradOff.w);\n"
            "    if(t<=co){\n"
            "      float f=(co>po)?(t-po)/(co-po):0.0;\n"
            "      vec4 a=vec4(prev.rgb*prev.a,prev.a);\n"
            "      vec4 b=vec4(cur.rgb*cur.a,cur.a);\n"
            "      vec4 m=mix(a,b,f);\n"
            "      return vec4(m.a>0.0? m.rgb/m.a : vec3(0.0), m.a);\n"
            "    }\n"
            "    prev=cur; po=co;\n"
            "  }\n"
            "  return prev;\n"
            "}\n"
            "float sd_round(vec2 p, vec4 box, float rad){\n"
            "  vec2 half_size=box.zw*0.5;\n"
            "  rad=min(max(rad,0.0), min(half_size.x, half_size.y));\n"
            "  vec2 q=abs(p-(box.xy+half_size))-half_size+rad;\n"
            "  return min(max(q.x,q.y),0.0)+length(max(q,0.0))-rad;\n"
            "}\n"
            // radx/rady = (tl, tr, br, bl). y grows downward in display-list space.
            "float sd_round4(vec2 p, vec4 box, vec4 radx, vec4 rady){\n"
            "  vec2 half_size=box.zw*0.5;\n"
            "  vec2 q=p-(box.xy+half_size);\n"
            "  vec4 rx4=vec4(radx.z, radx.y, radx.w, radx.x);\n"
            "  vec4 ry4=vec4(rady.z, rady.y, rady.w, rady.x);\n"
            "  rx4.xy=(q.x>0.0)?rx4.xy:rx4.zw;\n"
            "  ry4.xy=(q.x>0.0)?ry4.xy:ry4.zw;\n"
            "  float rx=min(max((q.y>0.0)?rx4.x:rx4.y,0.0), half_size.x);\n"
            "  float ry=min(max((q.y>0.0)?ry4.x:ry4.y,0.0), half_size.y);\n"
            "  if(abs(rx-ry)<0.001){\n"
            "    float rad=rx;\n"
            "    vec2 m=abs(q)-half_size+rad;\n"
            "    return min(max(m.x,m.y),0.0)+length(max(m,0.0))-rad;\n"
            "  }\n"
            "  vec2 r=max(vec2(rx,ry), vec2(1e-6));\n"
            "  vec2 d=abs(q)-half_size+r;\n"
            "  float inside=min(max(d.x,d.y),0.0);\n"
            "  vec2 m=max(d,0.0);\n"
            "  float outside;\n"
            "  if(m.x>0.0 && m.y>0.0){\n"
            "    vec2 n=m/r;\n"
            "    float len=length(n);\n"
            "    outside=(len-1.0)*length(r*(n/max(len,1e-6)));\n"
            "  } else {\n"
            "    float rad=(m.x>0.0)?r.x:((m.y>0.0)?r.y:min(r.x,r.y));\n"
            "    outside=length(m)-rad;\n"
            "  }\n"
            "  return inside+outside;\n"
            "}\n"
            // Half-pixel linear coverage. A smoothstep the width of fwidth (~1px) leaves an
            // axis-aligned 1px border at about half coverage on two rows; Edge's snapped border
            // is one solid device pixel. sd is in CSS pixels, which is the device pixel at scale 1.
            "float cover(float sd){ return clamp(0.5-sd, 0.0, 1.0); }\n"
            "float poly_cover(vec2 p){\n"
            "  int n=uPolyCount<0?-uPolyCount:uPolyCount;\n"
            "  if(n<3) return 1.0;\n"
            "  bool even=uPolyCount<0;\n"
            "  float dist=1e9; int wind=0; int hits=0;\n"
            "  for(int i=0;i<16;++i){\n"
            "    if(i>=n) break;\n"
            "    vec2 a=uPoly[i];\n"
            "    int j=i+1; if(j>=n) j=0;\n"
            "    vec2 b=uPoly[j];\n"
            "    vec2 e=b-a; float el2=dot(e,e);\n"
            "    if(el2>1e-8){ float t=clamp(dot(p-a,e)/el2,0.0,1.0); dist=min(dist,length(p-(a+e*t))); }\n"
            "    bool up=a.y<=p.y && b.y>p.y;\n"
            "    bool down=b.y<=p.y && a.y>p.y;\n"
            "    if(up||down){\n"
            "      float t=(p.y-a.y)/(b.y-a.y);\n"
            "      if(mix(a.x,b.x,t)>p.x){ if(even) hits+=1; else wind+=up?1:-1; }\n"
            "    }\n"
            "  }\n"
            "  bool inside=even ? (hits-2*(hits/2))!=0 : wind!=0;\n"
            "  float sd=inside?-dist:dist;\n"
            "  return clamp(0.5-sd,0.0,1.0);\n"
            "}\n"
            "float clip_cover(vec2 p){\n"
            "  float c=1.0; int i=0;\n"
            "  while(i<uRoundCount&&i<4){\n"
            "    float rr=(i==0)?uRoundR.x:((i==1)?uRoundR.y:((i==2)?uRoundR.z:uRoundR.w));\n"
            "    if(rr<-0.5){\n"
            "      if(i+2>=4) break;\n"
            "      vec4 local=uRoundRect[i]; vec4 inv0=uRoundRect[i+1]; vec4 inv1=uRoundRect[i+2];\n"
            "      float lx=inv0.x*p.x+inv0.y*p.y+inv0.z;\n"
            "      float ly=inv0.w*p.x+inv1.x*p.y+inv1.y; float rad=inv1.z;\n"
            "      if(rad>0.5) c*=cover(sd_round(vec2(lx,ly),local,rad));\n"
            "      else if(lx<local.x||ly<local.y||lx>=local.x+local.z||ly>=local.y+local.w) c=0.0;\n"
            "      i+=3;\n"
            "    } else { c*=cover(sd_round(p,uRoundRect[i],rr)); i+=1; }\n"
            "  }\n"
            "  c*=poly_cover(p);\n"
            "  return c;\n"
            "}\n"
            "void main(){\n"
            "  float s=max(uScale,0.0001);\n"
            "  vec2 p=vec2(gl_FragCoord.x/s, uScreen.y-gl_FragCoord.y/s);\n"
            "  float clip_c=clip_cover(p);\n"
            "  if(clip_c<=0.0) discard;\n"
            "  vec4 col=vCol;\n"
            "  if(uMode==2){\n"
            "    vec2 sp=p; if(uAff2!=0) sp=vec2(uAff2Inv0.x*p.x+uAff2Inv0.y*p.y+uAff2Inv0.z, uAff2Inv0.w*p.x+uAff2Inv1.x*p.y+uAff2Inv1.y);\n"
            "    float sd=sd_round4(sp,uShape,uShapeRadii,uShapeRadiiY);\n"
            "    float cov=cover(sd);\n"
            "    if(uGradCount>=0&&uShapeStroke>0.0) cov=max(cov-cover(sd+uShapeStroke),0.0);\n"
            "    vec4 base=(uGradCount!=0)? grad(p) : vCol;\n"
            "    col=vec4(base.rgb, base.a*cov);\n"
            "  }\n"
            "  col.a*=clip_c;\n"
            // Skia SrcOver on 8888. The destination term stays (dst * (256 - srcA)) >> 8:
            // dividing by 255 truncates 232*115/255 to 104, and Edge stores 105, which is
            // (232 * 116) >> 8. That scale keeps a black 0.55 overlay on #161c27 at (9,12,17).
            // The source term is SkMulDiv255Round. (c * 255) >> 8 is c-1, so an opaque pixel
            // that shared this batch was stored one darker on every channel.
            "  if(uIntBlend!=0){\n"
            "    vec4 dst=texelFetch(uDst,ivec2(gl_FragCoord.xy),0);\n"
            "    ivec4 s=ivec4(clamp(round(col*255.0),0.0,255.0));\n"
            "    ivec4 d=ivec4(clamp(round(dst*255.0),0.0,255.0));\n"
            "    int scale=256-s.a;\n"
            "    ivec3 prod=s.rgb*s.a+128;\n"
            "    ivec3 premul=(prod+(prod>>8))>>8;\n"
            "    ivec3 rgb=premul+((d.rgb*scale)>>8);\n"
            "    int a=min(255, s.a+((d.a*scale)>>8));\n"
            "    col=vec4(vec3(rgb),float(a))/255.0;\n"
            "  }\n"
            "  o=col;\n"
            "}\n";
        // Desktop is 4.3 so the glyph program can use a storage buffer. The UI body is the
        // same one GLES compiles as 300 es.
        const std::string vs =
            std::string(es ? "#version 300 es\n" : "#version 430 core\n") + vs_body;
        const std::string fs = std::string(es ? "#version 300 es\nprecision mediump float;\n"
                                               : "#version 430 core\n") +
                               fs_body;
        prog_ = compile(ctx, vs.c_str(), fs.c_str());
        if (prog_ == 0) {
            return false;
        }
        // zui-shader-body: image
        const char* image_fs_body =
            "in vec2 vUv; in vec4 vCol; out vec4 o;\n"
            "uniform sampler2D uTex; uniform vec2 uScreen; uniform float uScale;\n"
            "uniform int uRoundCount; uniform vec4 uRoundRect[4]; uniform vec4 uRoundR;\n"
            "uniform int uPolyCount; uniform vec2 uPoly[16];\n"
            "float sd_round(vec2 p, vec4 box, float rad){\n"
            "  vec2 half_size=box.zw*0.5;\n"
            "  rad=min(max(rad,0.0), min(half_size.x, half_size.y));\n"
            "  vec2 q=abs(p-(box.xy+half_size))-half_size+rad;\n"
            "  return min(max(q.x,q.y),0.0)+length(max(q,0.0))-rad;\n"
            "}\n"
            "float cover(float sd){ return clamp(0.5-sd, 0.0, 1.0); }\n"
            "float poly_cover(vec2 p){\n"
            "  int n=uPolyCount<0?-uPolyCount:uPolyCount;\n"
            "  if(n<3) return 1.0;\n"
            "  bool even=uPolyCount<0;\n"
            "  float dist=1e9; int wind=0; int hits=0;\n"
            "  for(int i=0;i<16;++i){\n"
            "    if(i>=n) break;\n"
            "    vec2 a=uPoly[i];\n"
            "    int j=i+1; if(j>=n) j=0;\n"
            "    vec2 b=uPoly[j];\n"
            "    vec2 e=b-a; float el2=dot(e,e);\n"
            "    if(el2>1e-8){ float t=clamp(dot(p-a,e)/el2,0.0,1.0); dist=min(dist,length(p-(a+e*t))); }\n"
            "    bool up=a.y<=p.y && b.y>p.y;\n"
            "    bool down=b.y<=p.y && a.y>p.y;\n"
            "    if(up||down){\n"
            "      float t=(p.y-a.y)/(b.y-a.y);\n"
            "      if(mix(a.x,b.x,t)>p.x){ if(even) hits+=1; else wind+=up?1:-1; }\n"
            "    }\n"
            "  }\n"
            "  bool inside=even ? (hits-2*(hits/2))!=0 : wind!=0;\n"
            "  float sd=inside?-dist:dist;\n"
            "  return clamp(0.5-sd,0.0,1.0);\n"
            "}\n"
            "float clip_cover(vec2 p){\n"
            "  float c=1.0; int i=0;\n"
            "  while(i<uRoundCount&&i<4){\n"
            "    float rr=(i==0)?uRoundR.x:((i==1)?uRoundR.y:((i==2)?uRoundR.z:uRoundR.w));\n"
            "    if(rr<-0.5){\n"
            "      if(i+2>=4) break;\n"
            "      vec4 local=uRoundRect[i]; vec4 inv0=uRoundRect[i+1]; vec4 inv1=uRoundRect[i+2];\n"
            "      float lx=inv0.x*p.x+inv0.y*p.y+inv0.z;\n"
            "      float ly=inv0.w*p.x+inv1.x*p.y+inv1.y; float rad=inv1.z;\n"
            "      if(rad>0.5) c*=cover(sd_round(vec2(lx,ly),local,rad));\n"
            "      else if(lx<local.x||ly<local.y||lx>=local.x+local.z||ly>=local.y+local.w) c=0.0;\n"
            "      i+=3;\n"
            "    } else { c*=cover(sd_round(p,uRoundRect[i],rr)); i+=1; }\n"
            "  }\n"
            "  c*=poly_cover(p);\n"
            "  return c;\n"
            "}\n"
            "void main(){\n"
            "  float s=max(uScale,0.0001);\n"
            "  vec2 p=vec2(gl_FragCoord.x/s, uScreen.y-gl_FragCoord.y/s);\n"
            "  float clip_c=clip_cover(p);\n"
            "  if(clip_c<=0.0) discard;\n"
            "  vec4 col=texture(uTex,vUv)*vCol;\n"
            "  col.a*=clip_c;\n"
            "  o=col;\n"
            "}\n";
        // zui-shader-body: shadow
        const char* shadow_fs_body =
            "in vec4 vCol; out vec4 o;\n"
            "uniform vec2 uScreen; uniform float uScale;\n"
            "uniform int uRoundCount; uniform vec4 uRoundRect[4]; uniform vec4 uRoundR;\n"
            "uniform int uPolyCount; uniform vec2 uPoly[16];\n"
            "uniform vec4 uShape; uniform vec4 uShapeRadii; uniform vec4 uShapeRadiiY;\n"
            "uniform float uShapeStroke; uniform float uShapeBlur;\n"
            "uniform int uAff2; uniform vec4 uAff2Inv0; uniform vec2 uAff2Inv1;\n"
            "uniform int uGradCount;\n"
            "float shadow_cover(float sd, float sigma){\n"
            "  float x=-sd/max(sigma,0.0001);\n"
            "  float t=1.0/(1.0+0.2316419*abs(x));\n"
            "  float d=0.3989423*exp(-0.5*x*x);\n"
            "  float p=d*t*(0.3193815+t*(-0.3565638+t*(1.781478+t*(-1.821256+t*1.330274))));\n"
            "  return (x>0.0)? 1.0-p : p;\n"
            "}\n"
            "float sd_round(vec2 p, vec4 box, float rad){\n"
            "  vec2 half_size=box.zw*0.5;\n"
            "  rad=min(max(rad,0.0), min(half_size.x, half_size.y));\n"
            "  vec2 q=abs(p-(box.xy+half_size))-half_size+rad;\n"
            "  return min(max(q.x,q.y),0.0)+length(max(q,0.0))-rad;\n"
            "}\n"
            "float sd_round4(vec2 p, vec4 box, vec4 radx, vec4 rady){\n"
            "  vec2 half_size=box.zw*0.5;\n"
            "  vec2 q=p-(box.xy+half_size);\n"
            "  vec4 rx4=vec4(radx.z, radx.y, radx.w, radx.x);\n"
            "  vec4 ry4=vec4(rady.z, rady.y, rady.w, rady.x);\n"
            "  rx4.xy=(q.x>0.0)?rx4.xy:rx4.zw;\n"
            "  ry4.xy=(q.x>0.0)?ry4.xy:ry4.zw;\n"
            "  float rx=min(max((q.y>0.0)?rx4.x:rx4.y,0.0), half_size.x);\n"
            "  float ry=min(max((q.y>0.0)?ry4.x:ry4.y,0.0), half_size.y);\n"
            "  if(abs(rx-ry)<0.001){\n"
            "    float rad=rx;\n"
            "    vec2 m=abs(q)-half_size+rad;\n"
            "    return min(max(m.x,m.y),0.0)+length(max(m,0.0))-rad;\n"
            "  }\n"
            "  vec2 r=max(vec2(rx,ry), vec2(1e-6));\n"
            "  vec2 d=abs(q)-half_size+r;\n"
            "  float inner=min(max(d.x,d.y),0.0);\n"
            "  vec2 m=max(d,0.0);\n"
            "  float outside;\n"
            "  if(m.x>0.0 && m.y>0.0){\n"
            "    vec2 n=m/r;\n"
            "    float len=length(n);\n"
            "    outside=(len-1.0)*length(r*(n/max(len,1e-6)));\n"
            "  } else {\n"
            "    float rad=(m.x>0.0)?r.x:((m.y>0.0)?r.y:min(r.x,r.y));\n"
            "    outside=length(m)-rad;\n"
            "  }\n"
            "  return inner+outside;\n"
            "}\n"
            "float cover(float sd){ return clamp(0.5-sd, 0.0, 1.0); }\n"
            "float poly_cover(vec2 p){\n"
            "  int n=uPolyCount<0?-uPolyCount:uPolyCount;\n"
            "  if(n<3) return 1.0;\n"
            "  bool even=uPolyCount<0;\n"
            "  float dist=1e9; int wind=0; int hits=0;\n"
            "  for(int i=0;i<16;++i){\n"
            "    if(i>=n) break;\n"
            "    vec2 a=uPoly[i];\n"
            "    int j=i+1; if(j>=n) j=0;\n"
            "    vec2 b=uPoly[j];\n"
            "    vec2 e=b-a; float el2=dot(e,e);\n"
            "    if(el2>1e-8){ float t=clamp(dot(p-a,e)/el2,0.0,1.0); dist=min(dist,length(p-(a+e*t))); }\n"
            "    bool up=a.y<=p.y && b.y>p.y;\n"
            "    bool down=b.y<=p.y && a.y>p.y;\n"
            "    if(up||down){\n"
            "      float t=(p.y-a.y)/(b.y-a.y);\n"
            "      if(mix(a.x,b.x,t)>p.x){ if(even) hits+=1; else wind+=up?1:-1; }\n"
            "    }\n"
            "  }\n"
            "  bool inside=even ? (hits-2*(hits/2))!=0 : wind!=0;\n"
            "  float sd=inside?-dist:dist;\n"
            "  return clamp(0.5-sd,0.0,1.0);\n"
            "}\n"
            "float clip_cover(vec2 p){\n"
            "  float c=1.0; int i=0;\n"
            "  while(i<uRoundCount&&i<4){\n"
            "    float rr=(i==0)?uRoundR.x:((i==1)?uRoundR.y:((i==2)?uRoundR.z:uRoundR.w));\n"
            "    if(rr<-0.5){\n"
            "      if(i+2>=4) break;\n"
            "      vec4 local=uRoundRect[i]; vec4 inv0=uRoundRect[i+1]; vec4 inv1=uRoundRect[i+2];\n"
            "      float lx=inv0.x*p.x+inv0.y*p.y+inv0.z;\n"
            "      float ly=inv0.w*p.x+inv1.x*p.y+inv1.y; float rad=inv1.z;\n"
            "      if(rad>0.5) c*=cover(sd_round(vec2(lx,ly),local,rad));\n"
            "      else if(lx<local.x||ly<local.y||lx>=local.x+local.z||ly>=local.y+local.w) c=0.0;\n"
            "      i+=3;\n"
            "    } else { c*=cover(sd_round(p,uRoundRect[i],rr)); i+=1; }\n"
            "  }\n"
            "  c*=poly_cover(p);\n"
            "  return c;\n"
            "}\n"
            "void main(){\n"
            "  float s=max(uScale,0.0001);\n"
            "  vec2 p=vec2(gl_FragCoord.x/s, uScreen.y-gl_FragCoord.y/s);\n"
            "  float clip_c=clip_cover(p);\n"
            "  if(clip_c<=0.0) discard;\n"
            "  vec2 sp=p; if(uAff2!=0) sp=vec2(uAff2Inv0.x*p.x+uAff2Inv0.y*p.y+uAff2Inv0.z, uAff2Inv0.w*p.x+uAff2Inv1.x*p.y+uAff2Inv1.y);\n"
            "  float sd=sd_round4(sp,uShape,uShapeRadii,uShapeRadiiY);\n"
            "  float cov=(uShapeBlur>0.0)? shadow_cover(sd,uShapeBlur) : cover(sd);\n"
            "  if(uGradCount>=0&&uShapeStroke>0.0) cov=max(cov-cover(sd+uShapeStroke),0.0);\n"
            "  o=vec4(vCol.rgb, vCol.a*cov*clip_c);\n"
            "}\n";
        // zui-shader-body: blur
        const char* blur_fs_body =
            "out vec4 o;\n"
            "uniform sampler2D uTex;\n"
            "uniform vec2 uScreen; uniform float uScale;\n"
            "uniform int uBlurRadius; uniform float uBlurSigma; uniform vec2 uBlurDir;\n"
            "uniform vec4 uBlurTint;\n"
            "void main(){\n"
            "  float s=max(uScale,0.0001);\n"
            "  vec4 acc=vec4(0.0);\n"
            "  float wsum=0.0;\n"
            "  float sig=max(uBlurSigma,0.0001);\n"
            "  int rad=uBlurRadius;\n"
            "  if(rad>48) rad=48;\n"
            "  if(rad<0) rad=0;\n"
            "  ivec2 base=ivec2(gl_FragCoord.xy);\n"
            "  ivec2 step=ivec2(int(uBlurDir.x),int(uBlurDir.y));\n"
            "  ivec2 sz=ivec2(int(uScreen.x*s+0.5),int(uScreen.y*s+0.5));\n"
            "  for(int i=-48;i<=48;++i){\n"
            "    if(i>rad||i<-rad) continue;\n"
            "    float w=exp(-0.5*float(i*i)/(sig*sig));\n"
            "    ivec2 c=clamp(base+step*i,ivec2(0),sz-ivec2(1));\n"
            "    acc+=texelFetch(uTex,c,0)*w;\n"
            "    wsum+=w;\n"
            "  }\n"
            "  o=acc/max(wsum,0.0001);\n"
            // A drop-shadow replaces the blurred RGB with the shadow color times the blurred
            // alpha. uBlurTint.a == 0 leaves the sample alone (element blur, box-shadow, backdrop).
            "  if(uBlurTint.a>0.0){\n"
            "    float a=o.a*uBlurTint.a;\n"
            "    o=vec4(uBlurTint.rgb*a,a);\n"
            "  }\n"
            "}\n";
        // zui-shader-body: layer
        // Filter-layer composite. Clips were applied while the layer was filled. Blend mode 0 with
        // no mask is the premul src-over used to place a blurred mask back on the framebuffer.
        const char* layer_fs_body =
            "out vec4 o;\n"
            "uniform sampler2D uTex; uniform sampler2D uDst;\n"
            "uniform vec2 uScreen; uniform float uScale;\n"
            "uniform int uGradCount; uniform vec2 uGradP0; uniform vec2 uGradD; uniform float uGradLen;\n"
            "uniform vec4 uGradOff; uniform vec4 uGradCol[4];\n"
            "uniform int uGradRepeat; uniform int uRadial; uniform vec2 uRadialR;\n"
            "uniform int uConic; uniform float uConicFrom;\n"
            "uniform float uShapeStroke;\n"
            "uniform int uBlendMode; uniform int uMaskGrad; uniform float uGroupOpacity;\n"
            "uniform vec2 uLayerOffset;\n"
            "uniform int uColorCount;\n"
            "uniform vec4 uCMa[8]; uniform vec4 uCMb[8]; uniform vec4 uCMc[8];\n"
            "vec4 grad(vec2 p){\n"
            "  float t;\n"
            "  if(uConic!=0){\n"
            "    vec2 d=p-uGradP0;\n"
            "    float ang=atan(d.x,-d.y);\n"
            "    t=(ang-uConicFrom)/6.28318530718;\n"
            "    t=t-floor(t);\n"
            "  } else if(uRadial!=0){\n"
            "    vec2 q=(p-uGradP0)/max(uRadialR,vec2(0.0001));\n"
            "    t=length(q);\n"
            "  } else {\n"
            "    t=dot(p-uGradP0,uGradD)/max(uGradLen,0.0001);\n"
            "  }\n"
            "  bool clamp_range=uGradCount<0;\n"
            "  int n=clamp_range?-uGradCount:uGradCount;\n"
            "  float t_lo=uGradOff.x;\n"
            "  float t_hi=(n<=1)?uGradOff.x:((n==2)?uGradOff.y:((n==3)?uGradOff.z:uGradOff.w));\n"
            "  if(uGradRepeat!=0){\n"
            "    float period=t_hi-t_lo;\n"
            "    if(period>0.000001){\n"
            "      float u=mod(t-t_lo,period);\n"
            "      if(u<0.0) u+=period;\n"
            "      t=t_lo+u;\n"
            "    }\n"
            "  }\n"
            "  if(clamp_range){\n"
            "    bool is_last=uShapeStroke>-1.5&&uShapeStroke<-0.5;\n"
            "    bool is_first=uShapeStroke<-1.5;\n"
            "    if(!is_first&&t<t_lo-1e-5) return vec4(0.0);\n"
            "    if(!is_last&&t>=t_hi-1e-5) return vec4(0.0);\n"
            "  }\n"
            "  vec4 prev=uGradCol[0]; float po=uGradOff.x;\n"
            "  if(t<=po) return prev;\n"
            "  for(int i=1;i<4;++i){\n"
            "    if(i>=n) break;\n"
            "    vec4 cur=uGradCol[i];\n"
            "    float co=(i==1)?uGradOff.y:((i==2)?uGradOff.z:uGradOff.w);\n"
            "    if(t<=co){\n"
            "      float f=(co>po)?(t-po)/(co-po):0.0;\n"
            "      vec4 a=vec4(prev.rgb*prev.a,prev.a);\n"
            "      vec4 b=vec4(cur.rgb*cur.a,cur.a);\n"
            "      vec4 m=mix(a,b,f);\n"
            "      return vec4(m.a>0.0? m.rgb/m.a : vec3(0.0), m.a);\n"
            "    }\n"
            "    prev=cur; po=co;\n"
            "  }\n"
            "  return prev;\n"
            "}\n"
            // Mode numbers match paint::MixBlendMode. Multiply and screen stay on the integer
            // formula the opaque mix-blend case already matched. The other separable modes round
            // half-up. Backdrop alpha mixes the unblended source back in, so a transparent
            // background colour does not multiply toward black.
            "float zui_soft(float cb,float cs){\n"
            "  if(cs<=0.5) return cb-(1.0-2.0*cs)*cb*(1.0-cb);\n"
            "  float d=cb<=0.25?((16.0*cb-12.0)*cb+4.0)*cb:sqrt(cb);\n"
            "  return cb+(2.0*cs-1.0)*(d-cb);\n"
            "}\n"
            "ivec3 zui_blend(int mode,ivec3 s,ivec3 d){\n"
            "  if(mode==1) return (s*d+127)/255;\n"
            "  if(mode==2) return 255-((255-s)*(255-d)+127)/255;\n"
            "  if(mode==4) return min(s,d);\n"
            "  if(mode==5) return max(s,d);\n"
            "  if(mode==10) return ivec3(abs(s.r-d.r),abs(s.g-d.g),abs(s.b-d.b));\n"
            "  vec3 S=vec3(s)/255.0; vec3 D=vec3(d)/255.0; vec3 B=S;\n"
            "  if(mode==3) B=mix(2.0*D*S,1.0-2.0*(1.0-D)*(1.0-S),step(0.5,D));\n"
            "  else if(mode==6) B=vec3(S.r<=0.0?0.0:(D.r>=1.0?1.0:min(1.0,D.r/(1.0-S.r))),S.g<=0.0?0.0:(D.g>=1.0?1.0:min(1.0,D.g/(1.0-S.g))),S.b<=0.0?0.0:(D.b>=1.0?1.0:min(1.0,D.b/(1.0-S.b))));\n"
            "  else if(mode==7) B=vec3(S.r>=1.0?1.0:(D.r<=0.0?0.0:1.0-min(1.0,(1.0-D.r)/max(S.r,0.0001))),S.g>=1.0?1.0:(D.g<=0.0?0.0:1.0-min(1.0,(1.0-D.g)/max(S.g,0.0001))),S.b>=1.0?1.0:(D.b<=0.0?0.0:1.0-min(1.0,(1.0-D.b)/max(S.b,0.0001))));\n"
            "  else if(mode==8) B=mix(2.0*S*D,1.0-2.0*(1.0-S)*(1.0-D),step(0.5,S));\n"
            "  else if(mode==9) B=vec3(zui_soft(D.r,S.r),zui_soft(D.g,S.g),zui_soft(D.b,S.b));\n"
            "  else if(mode==11) B=clamp(S+D-2.0*S*D,0.0,1.0);\n"
            "  return ivec3(clamp(floor(B*255.0+0.5),0.0,255.0));\n"
            "}\n"
            "void main(){\n"
            "  float s=max(uScale,0.0001);\n"
            "  vec2 p=vec2(gl_FragCoord.x/s, uScreen.y-gl_FragCoord.y/s);\n"
            // CSS y grows down and GL's framebuffer y grows up, so the caller stores the offset
            // already flipped. A sample that leaves the texture is transparent: texelFetch does
            // not honor the sampler's clamp.
            "  vec2 shifted=round(uLayerOffset);\n"
            "  ivec2 c=ivec2(gl_FragCoord.xy)-ivec2(int(shifted.x),int(shifted.y));\n"
            "  ivec2 sz=textureSize(uTex,0);\n"
            "  vec4 src=vec4(0.0);\n"
            "  if(c.x>=0&&c.y>=0&&c.x<sz.x&&c.y<sz.y) src=texelFetch(uTex,c,0);\n"
            // CSS filter color functions run in source order on the composited layer, in encoded
            // sRGB. Each function clamps before the next (a composed matrix would let contrast
            // exceed 1). The last value is snapped with half-up so a .5 channel matches Chrome;
            // GLSL round() is half-even and turns contrast(2) of #7a3eb1 red into 116 instead of 117.
            "  if(uColorCount>0){\n"
            "    for(int i=0;i<8;++i){\n"
            "      if(i>=uColorCount) break;\n"
            "      vec3 lin=src.rgb;\n"
            "      src.rgb=clamp(vec3(dot(uCMa[i].xyz,lin)+uCMa[i].w,dot(uCMb[i].xyz,lin)+uCMb[i].w,dot(uCMc[i].xyz,lin)+uCMc[i].w),0.0,1.0);\n"
            "    }\n"
            "    src.rgb=floor(src.rgb*255.0+0.5)/255.0;\n"
            "  }\n"
            "  float maskA=1.0;\n"
            "  if(uMaskGrad!=0) maskA=grad(p).a;\n"
            "  float g=clamp(uGroupOpacity,0.0,1.0)*maskA;\n"
            "  if(src.a<=0.001||g<=0.001) discard;\n"
            "  vec4 dst=texelFetch(uDst,ivec2(gl_FragCoord.xy),0);\n"
            "  ivec4 sv=ivec4(clamp(round(src*255.0),0.0,255.0));\n"
            "  ivec4 dv=ivec4(clamp(round(dst*255.0),0.0,255.0));\n"
            "  int G=int(clamp(round(g*255.0),0.0,255.0));\n"
            "  ivec3 Cs=sv.rgb;\n"
            "  if(uBlendMode!=0){\n"
            "    ivec3 blended=zui_blend(uBlendMode,sv.rgb,dv.rgb);\n"
            "    if(dv.a>=255) Cs=blended;\n"
            "    else if(dv.a>0) Cs=(sv.rgb*(255-dv.a)+blended*dv.a+127)/255;\n"
            "  }\n"
            "  int a;\n"
            "  ivec3 premul;\n"
            "  if(uBlendMode==0){\n"
            "    int pa=sv.a*G+128;\n"
            "    a=(pa+(pa>>8))>>8;\n"
            "    ivec3 pr=sv.rgb*G+128;\n"
            "    premul=(pr+(pr>>8))>>8;\n"
            "  } else {\n"
            "    int pa=sv.a*G+128;\n"
            "    a=(pa+(pa>>8))>>8;\n"
            "    ivec3 pr=Cs*a+128;\n"
            "    premul=(pr+(pr>>8))>>8;\n"
            "  }\n"
            "  int scale=256-a;\n"
            "  ivec3 rgb=premul+((dv.rgb*scale)>>8);\n"
            "  int oa=min(255,a+((dv.a*scale)>>8));\n"
            "  o=vec4(vec3(rgb),float(oa))/255.0;\n"
            "}\n";
        const std::string image_fs = std::string(es ? "#version 300 es\nprecision mediump float;\n"
                                                     : "#version 430 core\n") +
                                     image_fs_body;
        const std::string shadow_fs = std::string(es ? "#version 300 es\nprecision mediump float;\n"
                                                      : "#version 430 core\n") +
                                      shadow_fs_body;
        const std::string blur_fs = std::string(es ? "#version 300 es\nprecision mediump float;\n"
                                                    : "#version 430 core\n") +
                                    blur_fs_body;
        const std::string layer_fs = std::string(es ? "#version 300 es\nprecision mediump float;\n"
                                                     : "#version 430 core\n") +
                                     layer_fs_body;
        image_ = compile(ctx, vs.c_str(), image_fs.c_str());
        shadow_ = compile(ctx, vs.c_str(), shadow_fs.c_str());
        blur_ = compile(ctx, vs.c_str(), blur_fs.c_str());
        layer_ = compile(ctx, vs.c_str(), layer_fs.c_str());
        if (image_ == 0 || shadow_ == 0 || blur_ == 0 || layer_ == 0) {
            return false;
        }
        glyph_ = 0;
        if (es) {
            return true;
        }
        // zui-shader-body: glyph-vert
        const char* glyph_vs =
            "#version 430 core\n"
            "layout(location=0) in vec2 aEm;\n"
            "layout(location=1) in vec4 aCol;\n"
            "layout(location=2) in uvec2 aRange;\n"
            "layout(location=3) in vec3 aM0;\n"
            "layout(location=4) in vec3 aM1;\n"
            "uniform vec2 uScreen;\n"
            "out vec2 vEm; out vec4 vCol; flat out uvec2 vRange;\n"
            "void main(){\n"
            "  vec2 s=vec2(dot(vec3(aEm,1.0),aM0), dot(vec3(aEm,1.0),aM1));\n"
            "  vec2 p=vec2(s.x/uScreen.x*2.0-1.0, 1.0-s.y/uScreen.y*2.0);\n"
            "  gl_Position=vec4(p,0,1); vEm=aEm; vCol=aCol; vRange=aRange;\n"
            "}\n";
        // zui-shader-body: glyph
        const char* glyph_fs_head =
            "#version 430 core\n"
            "in vec2 vEm; in vec4 vCol; flat in uvec2 vRange; out vec4 o;\n"
            "uniform vec2 uScreen; uniform float uScale;\n"
            "uniform int uRoundCount; uniform vec4 uRoundRect[4]; uniform vec4 uRoundR;\n"
            "uniform int uPolyCount; uniform vec2 uPoly[16];\n"
            "struct Curve { vec4 a; vec4 b; int kind; int pad0; int pad1; int pad2; };\n"
            "layout(std430, binding=0) readonly buffer CurveBlock { Curve curves[]; };\n"
            "#define zui_scale ((uScale > 0.0) ? uScale : 1.0)\n"
            "#define zui_round_count uRoundCount\n"
            "#define zui_round_rect uRoundRect\n"
            "#define zui_round_r uRoundR\n"
            "#define zui_poly_count uPolyCount\n"
            "#define zui_poly uPoly\n"
            "#define zui_frag_xy (vec2(gl_FragCoord.x / zui_scale, uScreen.y - gl_FragCoord.y / "
            "zui_scale))\n";
        const std::string glyph_fs = std::string(glyph_fs_head) + kGlyphCoverGlsl;
        glyph_ = compile(ctx, glyph_vs, glyph_fs.c_str());
        if (glyph_ == 0) {
            return false;
        }
        return true;
    }

GLuint GlPrograms::compile(GlContext& ctx, const char* vs, const char* fs) {
        auto sh = [&](GLenum type, const char* src) {
            GLuint s = ctx.gl_.CreateShader(type);
            ctx.gl_.ShaderSource(s, 1, &src, nullptr);
            ctx.gl_.CompileShader(s);
            GLint ok = 0;
            ctx.gl_.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[512];
                ctx.gl_.GetShaderInfoLog(s, 512, nullptr, log);
                ZUI_ERROR("shader: {}", log);
                ctx.gl_.DeleteShader(s);
                return GLuint{0};
            }
            return s;
        };
        GLuint v = sh(GL_VERTEX_SHADER, vs);
        GLuint f = sh(GL_FRAGMENT_SHADER, fs);
        if (!v || !f) {
            return 0;
        }
        GLuint p = ctx.gl_.CreateProgram();
        ctx.gl_.AttachShader(p, v);
        ctx.gl_.AttachShader(p, f);
        ctx.gl_.LinkProgram(p);
        ctx.gl_.DeleteShader(v);
        ctx.gl_.DeleteShader(f);
        GLint ok = 0;
        ctx.gl_.GetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512];
            ctx.gl_.GetProgramInfoLog(p, 512, nullptr, log);
            ZUI_ERROR("program: {}", log);
            ctx.gl_.DeleteProgram(p);
            return 0;
        }
        return p;
}

}  // namespace backends
}  // namespace zui
