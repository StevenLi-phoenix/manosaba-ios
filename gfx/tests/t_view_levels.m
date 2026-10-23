// A pass on a 4-level colour view, then SetRenderTargets to a 1-level view of the same texture (plan item 263 kept this from t_rt_range when the
// DM_NVN_RT_RANGE candidate that continued such passes was removed): the second SetRenderTargets ends the first pass, so the sequence is two
// passes whether the pass is ended by hand or by it, with the same picture byte for byte (hand-calculated colours, the depth and stencil seeds kept),
// the first draw in depth read-only mode, the occlusion samples after the reset, and the bandwidth ledger's attachment bytes (DontCare: one
// depth-stencil store saved a pass). --small-depth: a depth target one level down; --sample-attachment: the second pass samples the colour
// texture's other level. The first targets taken for the second's before its SetRenderTargets (--same-targets, the broken variant): the
// same-target continuation joins the passes, 2 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void){return NULL;}
module_t *module_of(const void *p){(void)p;return NULL;}
module_t g_mod[MAX_MOD];int g_nmod;
void svc_thread_dump(void){}
uint64_t now_ns(void){return bp_now();}
static int cases,bad,small_depth,force_invalid,sample_attachment;
#define E(n,w,a) do{uint64_t actual=(a),want=(w);cases++;printf("%s expected %llu → got %llu\n",n,(unsigned long long)want,(unsigned long long)actual);bad+=want!=actual;}while(0)
typedef struct{uint8_t b[8192];size_t n;} code;
static void emit(code *c,int op,const void *p,size_t n){size_t z=(8+n+7)&~7ull;op_hdr h={op,(uint32_t)z};memcpy(c->b+c->n,&h,8);if(n)memcpy(c->b+c->n+8,p,n);c->n+=z;}
static void st(code *c,int k,const void *v,size_t n){p_bind_state b={.kind=k};memcpy(b.bytes,v,n);emit(c,OP_BIND_STATE,&b,sizeof b);}
static tex_t color={.serial=52000},depth={.serial=52001};static prog_t pg[2]={{.serial=52002},{.serial=52003}};
static p_set_rt rt(int levels){p_set_rt r={.n=1,.depth=&depth,.has_dview=1};r.color[0]=&color;r.has_cview[0]=1;
 r.cview[0]=(view_t){.set_levels=1,.set_layers=1,.set_format=1,.set_target=1,.nlevels=levels,.nlayers=1,.format=NVN_FORMAT_RGBA8,.target=NVN_TEXTURE_TARGET_2D};
 r.dview=r.cview[0];r.dview.nlevels=1;r.dview.base_level=small_depth?1:0;r.dview.format=NVN_FORMAT_DEPTH32F_STENCIL8;return r;}
static void seed(void){RPD *p=[RPD new];p.colorAttachments[0].texture=tex_mtl(&color);p.colorAttachments[0].loadAction=MTLLoadActionClear;p.colorAttachments[0].storeAction=MTLStoreActionStore;p.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
 p.depthAttachment.texture=p.stencilAttachment.texture=tex_mtl(&depth);p.depthAttachment.loadAction=p.stencilAttachment.loadAction=MTLLoadActionClear;p.depthAttachment.storeAction=p.stencilAttachment.storeAction=MTLStoreActionStore;p.depthAttachment.clearDepth=.75;p.stencilAttachment.clearStencil=0x2d;enc_r_end(enc_r_begin(p));
 if(sample_attachment){RPD *q=[RPD new];q.colorAttachments[0].texture=tex_mtl(&color);q.colorAttachments[0].level=1;q.colorAttachments[0].loadAction=MTLLoadActionClear;q.colorAttachments[0].storeAction=MTLStoreActionStore;q.colorAttachments[0].clearColor=MTLClearColorMake(0,1,0,0);enc_r_end(enc_r_begin(q));}
 if(small_depth){p.colorAttachments[0].texture=nil;p.depthAttachment.level=p.stencilAttachment.level=1;enc_r_end(enc_r_begin(p));}}
static void rr_read_planes(id<MTLBuffer> __strong *b){id<MTLTexture> t=tex_mtl(&color),d=tex_mtl(&depth);[blit()copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)sourceSize:MTLSizeMake(8,8,1)toBuffer:b[0]destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
 [blit()copyFromTexture:d sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)sourceSize:MTLSizeMake(8,8,1)toBuffer:b[1]destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256 options:MTLBlitOptionDepthFromDepthStencil];
 [blit()copyFromTexture:d sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)sourceSize:MTLSizeMake(8,8,1)toBuffer:b[2]destinationOffset:0 destinationBytesPerRow:8 destinationBytesPerImage:64 options:MTLBlitOptionStencilFromDepthStencil];}
int main(int argc,char **argv){@autoreleasepool{for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--small-depth"))small_depth=1;else if(!strcmp(argv[i],"--same-targets"))force_invalid=1;else if(!strcmp(argv[i],"--sample-attachment"))sample_attachment=1;}mtl_init();NSError *err=nil;
 id<MTLLibrary> lib=[g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;vertex float4 v(uint n[[vertex_id]]){float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[n],.5,1);}\nfragment float4 r(){return float4(1,0,0,1);}\nfragment float4 g(){return float4(0,1,0,0);}struct A{texture2d<float> t[[id(0)]];};fragment float4 s(constant A&a[[buffer(0)]]){return a.t.read(uint2(0));}\n"options:nil error:&err];if(!lib)abort();
 for(int i=0;i<2;i++){NvProg *p=[NvProg new];p->sh[0]=[NvShader new];p->sh[0]->fn=[lib newFunctionWithName:@"v"];p->sh[1]=[NvShader new];p->sh[1]->fn=[lib newFunctionWithName:i?(sample_attachment?@"s":@"g"):@"r"];p->sh[1]->stage=1;p->sh[1]->rf.color_mask=1;if(i&&sample_attachment){p->sh[1]->rf.nb=1;p->sh[1]->rf.b[0]=(nvn_rbind){.kind=NVN_RB_TEX,.dtt=MTLTextureType2D};p->sh[1]->ae=[p->sh[1]->fn newArgumentEncoderWithBufferIndex:0];}
 serial_put(pg[i].serial,p);}
 color.d.width=color.d.height=depth.d.width=depth.d.height=8;color.d.depth=color.d.samples=depth.d.depth=depth.d.samples=1;color.d.levels=4;depth.d.levels=small_depth?2:1;color.d.target=depth.d.target=NVN_TEXTURE_TARGET_2D;color.d.format=NVN_FORMAT_RGBA8;depth.d.format=NVN_FORMAT_DEPTH32F_STENCIL8;
 (void)tex_mtl(&color);(void)tex_mtl(&depth);if(sample_attachment){view_t v={.set_levels=1,.base_level=1,.nlevels=1};mtl_reg_tex(1000,&color,&v);}
 uint16_t *ix;posix_memalign((void **)&ix,getpagesize(),getpagesize());for(int i=0;i<120;i++)ix[i]=i%3;mtl_pool_add(ix,getpagesize());
 code first={0},second={0};p_set_rt a=rt(4),b=rt(1);emit(&first,OP_SET_RT,&a,sizeof a);ds_st ds={.dt=1,.df=NVN_DEPTH_FUNC_LESS};st(&first,5,&ds,sizeof ds);
 color_st col={0,NVN_LOGIC_OP_COPY,NVN_ALPHA_FUNC_ALWAYS};st(&first,1,&col,sizeof col);p_bind_program pr={&pg[0],3};emit(&first,OP_BIND_PROGRAM,&pr,sizeof pr);uint64_t reset=NVN_COUNTER_TYPE_SAMPLES_PASSED;emit(&first,OP_RESET_COUNTER,&reset,8);p_draw dw={NVN_DRAW_PRIMITIVE_TRIANGLES,0,3};emit(&first,OP_DRAW,&dw,sizeof dw);emit(&first,OP_END,NULL,0);
 emit(&second,OP_RESET_COUNTER,&reset,8);emit(&second,OP_SET_RT,&b,sizeof b);if(small_depth){ds_st off={.df=NVN_DEPTH_FUNC_ALWAYS};st(&second,5,&off,sizeof off);}pr.prog=&pg[1];emit(&second,OP_BIND_PROGRAM,&pr,sizeof pr);if(sample_attachment){p_bind_handles h={.stage=1,.first=0,.n=1};h.h[0]=1000;emit(&second,OP_BIND_TEX,&h,sizeof h);}col.blend_en=1;st(&second,1,&col,sizeof col);blend_st blend={0,NVN_BLEND_FUNC_ONE,NVN_BLEND_FUNC_ONE,NVN_BLEND_FUNC_ONE,NVN_BLEND_FUNC_ZERO,NVN_BLEND_EQUATION_ADD,NVN_BLEND_EQUATION_ADD};st(&second,0,&blend,sizeof blend);p_draw_idx di={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=120,.itype=1,.ibuf=(uint64_t)ix,.instances=1};emit(&second,OP_DRAW_IDX,&di,sizeof di);emit(&second,OP_END,NULL,0);
 id<MTLBuffer> buffers[2][3];for(int m=0;m<2;m++)for(int j=0;j<3;j++)buffers[m][j]=[g_dev newBufferWithLength:j==2?64:256 options:MTLResourceStorageModeShared];uint64_t passes[2],bytes[2],q[2][2]={{0}};
 mtl_sub_begin();
 for(int m=0;m<2;m++){state_reset();memset(&R,0,sizeof R);vis_clear();g_vis_on=g_vis_counting=0;seed();uint64_t p=g_stat_pass,l=g_bw_load,s=g_bw_store,draws=g_stat_draw;nvn_exec(first.b);E("first draw in depth read-only mode",4,g_enc_mode);if(!m)rp_end();else if(force_invalid)R.rt.cview[0].nlevels=1; // broken variant: the first targets taken for the second's
 nvn_exec(second.b);rp_end();passes[m]=g_stat_pass-p;bytes[m]=g_bw_load-l+g_bw_store-s;E("real draw count unchanged",2,g_stat_draw-draws);post((post_op){3,NULL,0,0,(uint64_t)q[m],(uint32_t)reset,vis_snapshot()});rr_read_planes(buffers[m]);if(g_blit){[g_blit endEncoding];g_blit=nil;}}
 int done=mtl_wait(mtl_sub_commit(),5000000000ull);E("same texture, same CB completed",1,done);if(!done)abort();E("no error in submit feedback",0,mtl_last_commit_error());
 uint8_t yellow[4]={255,255,0,0},green[4]={0,255,0,0}; // alpha is given 0 by the second draw's ONE/ ZERO; integer closed-form RGB yellow.
 for(int m=0;m<2;m++){unsigned bc=0,bd=0,bs=0;for(int i=0;i<64;i++){bc+=memcmp((uint8_t *)buffers[m][0].contents+4*i,small_depth && (i%8>=4||i/8>=4)?green:yellow,4)!=0;bd+=((float *)buffers[m][1].contents)[i]!=.75f;bs+=((uint8_t *)buffers[m][2].contents)[i]!=0x2d;}E("64 texels each, hand-calculated yellow",0,bc);E("depth .75 seed kept",0,bd);E("stencil 0x2d seed kept",0,bs);E("samples after Reset, 120 indices 40 triangles",2560,q[m][0]);}
 E("three planes, the pass ended by hand vs by SetRenderTargets, byte for byte",0,memcmp(buffers[0][0].contents,buffers[1][0].contents,256)||memcmp(buffers[0][1].contents,buffers[1][1].contents,256)||memcmp(buffers[0][2].contents,buffers[1][2].contents,64));
 E("ended by hand: two passes",2,passes[0]);E("ended by SetRenderTargets: two passes",2,passes[1]);{ // DontCare (not stored if this pass did not write it): both passes' depth state enables only test, not write, so each pass saves one depth-stencil store = 8×8×5 bytes (Depth32F_S8 attachment model, 5 B/pixel)
   const char *u=getenv("DM_NVN_UNWRITTEN_DC"); int udc=!(u&&!strcmp(u,"0")); unsigned dsb=udc&&!small_depth&&!sample_attachment?8*8*5:0;
   for(int m=0;m<2;m++)E(udc?"attachment model 2048−2×320 (DontCare)":"attachment model 2048",(small_depth?1152:2048)-2*dsb,bytes[m]); }
 mtl_pool_del(ix);free(ix);printf("VIEW_LEVELS cases=%d failed=%d\n",cases,bad);return bad!=0;
}}
