/* Native-widget/offscreen SDL_GPU comparison. See README.md for measurement scope. */
#include "bench_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char* cases[BENCH_CASE_COUNT]={"labels_static","labels_dynamic","inspector","list_scroll","mixed_inspector","asset_browser","text_editor","docked_workspace"};
typedef struct Sample { double frame,ui,upload,encode,submit,wait; BenchStats stats; } Sample;
static double seconds(void){return (double)SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency();}
static void json_string(const char* text){putchar('"');for(const unsigned char*p=(const unsigned char*)text;*p;p++){if(*p=='"'||*p=='\\')putchar('\\');if(*p>=32)putchar(*p);else printf("\\u%04x",*p);}putchar('"');}
static void die(const char* message){fprintf(stderr,"comparison: %s: %s\n",message,SDL_GetError());exit(1);}
static void* alloc(size_t size){void*p=calloc(1,size);if(!p)die("allocation");return p;}
static char* read_file(const char* path,size_t* size){FILE*f=fopen(path,"rb");if(!f)die(path);if(fseek(f,0,SEEK_END))die("seek");long n=ftell(f);if(n<0)die("file size");rewind(f);char*p=alloc((size_t)n+1);if(fread(p,1,(size_t)n,f)!=(size_t)n)die("read");fclose(f);*size=(size_t)n;return p;}
static void load_font(BenchFont*font){
 size_t length;char*data=read_file("examples/assets/inter_medium_16.font",&length);font->font_data=data;font->font_data_size=length;
 const char*p=data;while(*p){
  if(sscanf(p,"atlas %d %d",&font->width,&font->height)==2){}
  else if(sscanf(p,"line_height %d",&font->line_height)==1){}
  else if(sscanf(p,"ascent %d",&font->ascent)==1){}
  else if(sscanf(p,"descent %d",&font->descent)==1){}
  else if(strncmp(p,"glyph ",6)==0){BenchGlyph*g=&font->glyphs[font->glyph_count++];if(font->glyph_count>256)die("font glyph bound");if(sscanf(p,"glyph %u %d %d %d %d %d %d %d",&g->codepoint,&g->x,&g->y,&g->w,&g->h,&g->x_offset,&g->y_offset,&g->advance)!=8)die("glyph parse");}
  const char*next=strchr(p,'\n');if(!next)break;p=next+1;
 }
 font->rgba=(unsigned char*)read_file("examples/assets/inter_medium_16.rgba",&length);
 if(length!=(size_t)font->width*(size_t)font->height*4u||font->glyph_count!=95||font->line_height!=16)die("shared font validation");
 font->white_x=font->white_y=-1;
 for(int y=1;y<font->height-1&&font->white_x<0;y++)for(int x=1;x<font->width-1;x++){
  int solid=1;for(int yy=-1;yy<=1;yy++)for(int xx=-1;xx<=1;xx++)if(font->rgba[((y+yy)*font->width+x+xx)*4+3]!=255)solid=0;
  if(solid){font->white_x=x;font->white_y=y;break;}
 }
}
static SDL_GPUTexture* make_texture(SDL_GPUDevice*device,int w,int h,SDL_GPUTextureUsageFlags usage){SDL_GPUTextureCreateInfo d={0};d.type=SDL_GPU_TEXTURETYPE_2D;d.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;d.usage=usage;d.width=(Uint32)w;d.height=(Uint32)h;d.layer_count_or_depth=1;d.num_levels=1;d.sample_count=SDL_GPU_SAMPLECOUNT_1;SDL_GPUTexture*t=SDL_CreateGPUTexture(device,&d);if(!t)die("texture creation");return t;}
static void upload_atlas(SDL_GPUDevice*device,SDL_GPUTexture*atlas,const BenchFont*font){
 SDL_GPUTransferBufferCreateInfo d={0};d.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;d.size=(Uint32)(font->width*font->height*4);SDL_GPUTransferBuffer*t=SDL_CreateGPUTransferBuffer(device,&d);if(!t)die("atlas upload buffer");
 void*p=SDL_MapGPUTransferBuffer(device,t,false);if(!p)die("atlas upload map");memcpy(p,font->rgba,d.size);SDL_UnmapGPUTransferBuffer(device,t);
 SDL_GPUCommandBuffer*c=SDL_AcquireGPUCommandBuffer(device);SDL_GPUCopyPass*pass=SDL_BeginGPUCopyPass(c);if(!pass)die("atlas copy pass");
 SDL_GPUTextureTransferInfo src={0};src.transfer_buffer=t;src.pixels_per_row=(Uint32)font->width;src.rows_per_layer=(Uint32)font->height;
 SDL_GPUTextureRegion dst={0};dst.texture=atlas;dst.w=(Uint32)font->width;dst.h=(Uint32)font->height;dst.d=1;SDL_UploadToGPUTexture(pass,&src,&dst,false);SDL_EndGPUCopyPass(pass);
 if(!SDL_SubmitGPUCommandBuffer(c)||!SDL_WaitForGPUIdle(device))die("atlas submit");
 SDL_ReleaseGPUTransferBuffer(device,t);
}
static void initial_document(char*doc){size_t used=0;unsigned line=0;while(used+64<8192){int n=snprintf(doc+used,BENCH_DOCUMENT_CAPACITY-used,"Line %03u: The quick brown fox jumps over logs.\n",line++);if(n<=0)die("document fixture");used+=(size_t)n;}}
static void scene_fill(BenchScene*s,enum BenchCase kind,uint32_t frame){
 memset(s,0,sizeof(*s));s->case_id=kind;s->frame=frame;s->mouse_x=s->mouse_y=-100;
 unsigned phase=kind==BENCH_STATIC?0:frame%16;
 for(unsigned i=0;i<BENCH_LABELS;i++)snprintf(s->labels[i],sizeof(s->labels[i]),"Item %03u: The quick brown fox %02u",i,phase);
 s->first_row=(frame/4)%9969;
 snprintf(s->filter,sizeof(s->filter),"asset_%c",'a'+(int)((frame/256)%4));
 for(unsigned i=0;i<BENCH_ROWS;i++)snprintf(s->rows[i],sizeof(s->rows[i]),"asset_%c/%05u - material texture mesh",'a'+(int)((frame/256)%4),s->first_row+i);
 for(unsigned i=0;i<32;i++){s->checks[i]=(int)((i+frame/16)%2);s->values[i]=(float)((i*17+frame)%1000)/999.0f;}
 if(kind==BENCH_MIXED){unsigned p=frame%64;if(p==16||p==17){s->mouse_x=24;s->mouse_y=24;s->mouse_down=p==16;}if(p>=32&&p<=35){s->mouse_x=p==32?350:p==33?500:620;s->mouse_y=24;s->mouse_down=p<35;}}
 if(kind==BENCH_LIST||kind==BENCH_BROWSER){unsigned p=frame%64;if(p==16||p==17){s->mouse_x=80;s->mouse_y=kind==BENCH_LIST?41:57;s->mouse_down=p==16;}}
 if(kind==BENCH_EDITOR){if(frame==2||frame==3){s->mouse_x=80;s->mouse_y=48;s->mouse_down=frame==2;}if(frame>=4)s->text_action=frame%2==0?1:2;}
}
static void wait_fence(SDL_GPUDevice*device,SDL_GPUFence**fence){if(*fence){if(!SDL_WaitForGPUFences(device,true,fence,1))die("fence wait");SDL_ReleaseGPUFence(device,*fence);*fence=NULL;}}
static void capture(SDL_GPUDevice*device,SDL_GPUTexture*target,const char*path){
 SDL_GPUTransferBufferCreateInfo d={0};d.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;d.size=BENCH_WIDTH*BENCH_HEIGHT*4;
 SDL_GPUTransferBuffer*t=SDL_CreateGPUTransferBuffer(device,&d);if(!t)die("readback buffer");SDL_GPUCommandBuffer*c=SDL_AcquireGPUCommandBuffer(device);SDL_GPUCopyPass*pass=SDL_BeginGPUCopyPass(c);if(!pass)die("readback pass");
 SDL_GPUTextureRegion src={0};src.texture=target;src.w=BENCH_WIDTH;src.h=BENCH_HEIGHT;src.d=1;SDL_GPUTextureTransferInfo dst={0};dst.transfer_buffer=t;dst.pixels_per_row=BENCH_WIDTH;dst.rows_per_layer=BENCH_HEIGHT;
 SDL_DownloadFromGPUTexture(pass,&src,&dst);SDL_EndGPUCopyPass(pass);if(!SDL_SubmitGPUCommandBuffer(c)||!SDL_WaitForGPUIdle(device))die("readback submit");
 const unsigned char*p=SDL_MapGPUTransferBuffer(device,t,false);if(!p)die("readback map");FILE*f=fopen(path,"wb");if(!f||fwrite(p,1,d.size,f)!=d.size)die("capture write");fclose(f);SDL_UnmapGPUTransferBuffer(device,t);SDL_ReleaseGPUTransferBuffer(device,t);
}
static int cmp_double(const void*a,const void*b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
static void metric(const char*name,const Sample*s,unsigned count,size_t offset){double*v=alloc(sizeof(double)*count),sum=0;for(unsigned i=0;i<count;i++){const double*p=(const double*)((const char*)&s[i]+offset);v[i]=*p*1e6;sum+=v[i];}qsort(v,count,sizeof(double),cmp_double);printf("\"%s\":{\"p50\":%.6f,\"p95\":%.6f,\"p99\":%.6f,\"mean\":%.6f,\"max\":%.6f}",name,v[(count-1)/2],v[(size_t)ceil(.95*count)-1],v[(size_t)ceil(.99*count)-1],sum/count,v[count-1]);free(v);}
static void verify_state(BenchAdapter*a,BenchStats*stats,const BenchScene*scene,const char*original){
 if(!a->inspect(a->state,stats))die("adapter inspection");
 if(!stats->vertices||!stats->draw_calls)die("empty geometry");
 if(scene->case_id==BENCH_EDITOR&&scene->frame>=4){size_t length=strlen(original);size_t expected=length+(scene->text_action==1?1u:0u);uint64_t base=bench_hash_bytes(original,length);if(stats->editor_length!=expected||(scene->text_action==2&&stats->editor_hash!=base)||(scene->text_action==1&&stats->editor_hash==base)){fprintf(stderr,"editor frame=%u action=%d expected_length=%zu actual_length=%u expected_base_hash=%llu actual_hash=%llu\n",scene->frame,scene->text_action,expected,stats->editor_length,(unsigned long long)base,(unsigned long long)stats->editor_hash);die("keystroke was lost or changed unrelated bytes");}}
 if((scene->case_id==BENCH_LIST||scene->case_id==BENCH_BROWSER)&&stats->visible_rows!=32)die("virtualized row count");
 if(scene->case_id==BENCH_MIXED&&scene->frame%64==17){unsigned cycles=(scene->frame-17)/64+1;unsigned expected=cycles%2;if((stats->checked_bits&1)!=expected)die("native checkbox replay");}
 if ((scene->case_id==BENCH_LIST||scene->case_id==BENCH_BROWSER)&&scene->frame%64==17&&stats->selected_row!=(int)scene->first_row)die("native row selection replay");
 if (scene->case_id==BENCH_INSPECTOR){uint32_t expected=0;for(unsigned i=0;i<32;i++)expected|=(uint32_t)(scene->checks[i]!=0)<<i;if(stats->checked_bits!=expected||fabsf(stats->first_slider-scene->values[0])>1e-6f)die("inspector value fixture");}
 if (scene->case_id==BENCH_MIXED&&((scene->frame%64==33&&stats->first_slider<0.3f)||(scene->frame%64==34&&stats->first_slider<0.7f)))die("native slider drag replay");
 if(!isfinite(stats->first_slider)||stats->first_slider<0||stats->first_slider>1)die("slider state range");
}
int main(int argc,char**argv){
 const char*library=NULL;const char*out=NULL;int kind=-1,verify=0;unsigned warmup=512,frames=2048,flights=2,replicate=1;
 for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--library")&&++i<argc)library=argv[i];else if(!strcmp(argv[i],"--case")&&++i<argc){for(int k=0;k<BENCH_CASE_COUNT;k++)if(!strcmp(argv[i],cases[k]))kind=k;}else if(!strcmp(argv[i],"--warmup")&&++i<argc)warmup=(unsigned)strtoul(argv[i],NULL,10);else if(!strcmp(argv[i],"--frames")&&++i<argc)frames=(unsigned)strtoul(argv[i],NULL,10);else if(!strcmp(argv[i],"--flights")&&++i<argc)flights=(unsigned)strtoul(argv[i],NULL,10);else if(!strcmp(argv[i],"--output")&&++i<argc)out=argv[i];else if(!strcmp(argv[i],"--replicate")&&++i<argc)replicate=(unsigned)strtoul(argv[i],NULL,10);else if(!strcmp(argv[i],"--verify"))verify=1;else die("invalid argument");}
 if(!library||kind<0||!out||!frames||flights<1||flights>2||!replicate||replicate>64)die("required arguments: --library rg|imgui --case NAME --output PREFIX [--flights1|2]");
 if(!SDL_Init(SDL_INIT_VIDEO))die("SDL video init");
 SDL_Window*window=SDL_CreateWindow("GUI comparison (offscreen)",64,64,SDL_WINDOW_HIDDEN);if(!window)die("hidden initialization window");
 SDL_GPUDevice*device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV,false,"vulkan");if(!device)die("Vulkan device creation");
 BenchFont font={0};load_font(&font);SDL_GPUTexture*atlas=make_texture(device,font.width,font.height,SDL_GPU_TEXTUREUSAGE_SAMPLER);upload_atlas(device,atlas,&font);SDL_GPUTexture*target=make_texture(device,BENCH_WIDTH,BENCH_HEIGHT,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
 char*document=alloc(BENCH_DOCUMENT_CAPACITY);initial_document(document);const char*shader_root=getenv("RG_BENCH_SHADER_ROOT");BenchInit init={device,atlas,&font,shader_root?shader_root:"shaders",document,flights,(enum BenchCase)kind};BenchAdapter adapter={0};
 if(!strcmp(library,"rg")){if(!bench_rg_init(&adapter,&init))die("rg_gui initialization");}else if(!strcmp(library,"imgui")){if(!bench_imgui_init(&adapter,&init))die("Dear ImGui initialization");}else die("unknown library");
 Sample*samples=alloc(sizeof(Sample)*frames);BenchScene*scene=alloc(sizeof(*scene));SDL_GPUFence*fences[2]={NULL,NULL};BenchStats stats={0};char path[2048];
 FILE*states=NULL;if(verify){snprintf(path,sizeof(path),"%s.states.csv",out);states=fopen(path,"w");if(!states)die("state log");fputs("frame,widgets,rows,checked_bits,first_slider,editor_length,editor_hash,vertices,draw_calls\n",states);}
 double production_start=0,production_end=0;
 for(unsigned f=0;f<warmup+frames;f++){
  if(f==warmup){for(unsigned i=0;i<flights;i++)wait_fence(device,&fences[i]);production_start=seconds();}
  scene_fill(scene,(enum BenchCase)kind,f);memset(&stats,0,sizeof(stats));unsigned slot=f%flights;Sample row={0};double start=seconds();wait_fence(device,&fences[slot]);row.wait=seconds()-start;
  double t=seconds();if(!adapter.build(adapter.state,scene,&stats))die("native UI/preparation");row.ui=seconds()-t;
  SDL_GPUCommandBuffer*command=SDL_AcquireGPUCommandBuffer(device);if(!command)die("command buffer");t=seconds();if(!adapter.upload(adapter.state,command,slot))die("upload preparation");row.upload=seconds()-t;
  t=seconds();SDL_GPUColorTargetInfo color={0};color.texture=target;color.clear_color=(SDL_FColor){0,0,0,1};color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass*pass=SDL_BeginGPURenderPass(command,&color,1,NULL);if(!pass)die("render pass");SDL_GPUViewport viewport={0,0,BENCH_WIDTH,BENCH_HEIGHT,0,1};SDL_SetGPUViewport(pass,&viewport);SDL_Rect scissor={0,0,BENCH_WIDTH,BENCH_HEIGHT};SDL_SetGPUScissor(pass,&scissor);
  /* --replicate N encodes the prepared content N times in one pass to separate per-content GPU cost from fixed per-frame overhead. */
  for(unsigned r=0;r<replicate;r++){BenchStats scratch=stats;if(!adapter.draw(adapter.state,command,pass,r==0?&stats:&scratch))die("native draw encoding");}
  SDL_EndGPURenderPass(pass);row.encode=seconds()-t;
  t=seconds();fences[slot]=SDL_SubmitGPUCommandBufferAndAcquireFence(command);if(!fences[slot])die("GPU submission");adapter.commit(adapter.state,slot);row.submit=seconds()-t;
  if(flights==1){t=seconds();wait_fence(device,&fences[slot]);row.wait+=seconds()-t;}
  row.frame=seconds()-start;row.stats=stats;if(f>=warmup)samples[f-warmup]=row;
  if(verify){verify_state(&adapter,&stats,scene,document);fprintf(states,"%u,%u,%u,%u,%.6f,%u,%llu,%u,%u\n",f,stats.widgets,stats.visible_rows,stats.checked_bits,(double)stats.first_slider,stats.editor_length,(unsigned long long)stats.editor_hash,stats.vertices,stats.draw_calls);}
 }
 for(unsigned i=0;i<flights;i++)wait_fence(device,&fences[i]);
 production_end=seconds();
 if(!verify)verify_state(&adapter,&stats,scene,document);
 if(states)fclose(states);
 if(verify){snprintf(path,sizeof(path),"%s.rgba",out);capture(device,target,path);}
 snprintf(path,sizeof(path),"%s.frames.csv",out);FILE*csv=fopen(path,"w");if(!csv)die("frame data");fputs("frame,frame_us,ui_us,upload_us,encode_us,submit_us,wait_us,vertices,draw_calls,widgets,rows\n",csv);
 for(unsigned i=0;i<frames;i++){const Sample*r=&samples[i];fprintf(csv,"%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u,%u,%u\n",i,r->frame*1e6,r->ui*1e6,r->upload*1e6,r->encode*1e6,r->submit*1e6,r->wait*1e6,r->stats.vertices,r->stats.draw_calls,r->stats.widgets,r->stats.visible_rows);}fclose(csv);
 printf("{\"library\":\"%s\",\"case\":\"%s\",\"replicate\":%u,\"flights\":%u,\"frames\":%u,\"warmup\":%u,\"verify\":%s,\"sdl_driver\":\"%s\",\"completed_fps\":%.3f,\"stats\":{\"vertices\":%u,\"draw_calls\":%u,\"widgets\":%u,\"rows\":%u,\"editor_length\":%u,\"editor_hash\":\"%llu\"},\"timings_us\":{",library,cases[kind],replicate,flights,frames,warmup,verify?"true":"false",SDL_GetGPUDeviceDriver(device),frames/(production_end-production_start),stats.vertices,stats.draw_calls,stats.widgets,stats.visible_rows,stats.editor_length,(unsigned long long)stats.editor_hash);
 metric("frame",samples,frames,offsetof(Sample,frame));putchar(',');metric("ui",samples,frames,offsetof(Sample,ui));putchar(',');metric("upload",samples,frames,offsetof(Sample,upload));putchar(',');metric("encode",samples,frames,offsetof(Sample,encode));putchar(',');metric("submit",samples,frames,offsetof(Sample,submit));putchar(',');metric("wait",samples,frames,offsetof(Sample,wait));printf("},\"gpu\":{");
 SDL_PropertiesID props=SDL_GetGPUDeviceProperties(device);
 printf("\"name\":");json_string(SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown"));
 printf(",\"driver\":");json_string(SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING,"unknown"));
 printf(",\"version\":");json_string(SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING,"unknown"));
 printf(",\"info\":");json_string(SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING,"unknown"));puts("}}");
 adapter.destroy(adapter.state);SDL_ReleaseGPUTexture(device,target);SDL_ReleaseGPUTexture(device,atlas);SDL_DestroyGPUDevice(device);SDL_DestroyWindow(window);SDL_Quit();free((void*)font.font_data);free((void*)font.rgba);free(document);free(samples);free(scene);return 0;
}
