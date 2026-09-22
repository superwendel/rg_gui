/* Windows CPU geometry benchmark. Compile the identical source with
 * RG_GUI_GPU_USE_SSE2=0 and =1, keeping optimization and dependencies fixed.
 * Run with "check" for guarded byte-equality and full-preparation checks.
 * Otherwise emits JSONL for 1/128/1024 image commands: packing alone and
 * complete GUI preparation. No window, GPU device, uploads, or presentation.
 */
#define RG_SPRINTF_NO_ASM 1
#define RGINLINE static inline
#include "rg_gui_gpu.h"
#if RG_GUI_GPU_USE_SSE2 && !RG_GUI_GPU_SSE2_ENABLED
#error Geometry SSE2 measurements require a compiler target with SSE2 enabled.
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static __declspec(noinline) int reference_pack(RgGuiGpuRenderer* renderer,
                                          RgGuiRect rect, RgGuiRect uv, u32 color)
{
	if (renderer->vertex_count > renderer->vertex_capacity ||
	    renderer->vertex_capacity - renderer->vertex_count < 6u)
		return 0;
	RgGuiGpuVertex* out = &renderer->vertices[renderer->vertex_count];
	rg_gui_gpu_set_vertex(&out[0], rect.x, rect.y, color, uv.x, uv.y);
	rg_gui_gpu_set_vertex(&out[1], rect.x + rect.w, rect.y, color, uv.x + uv.w, uv.y);
	rg_gui_gpu_set_vertex(&out[2], rect.x + rect.w, rect.y + rect.h, color, uv.x + uv.w, uv.y + uv.h);
	rg_gui_gpu_set_vertex(&out[3], rect.x, rect.y, color, uv.x, uv.y);
	rg_gui_gpu_set_vertex(&out[4], rect.x + rect.w, rect.y + rect.h, color, uv.x + uv.w, uv.y + uv.h);
	rg_gui_gpu_set_vertex(&out[5], rect.x, rect.y + rect.h, color, uv.x, uv.y + uv.h);
	renderer->vertex_count += 6u;
	return 1;
}


static volatile u64 sink;
static double clock_scale;
static double now(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart * clock_scale; }
static void fail(const char* reason) { fprintf(stderr,"error: %s\n",reason); exit(1); }
static f32 from_bits(u32 bits) { f32 f; memcpy(&f,&bits,4); return f; }
static u64 hash(const void* p,size_t n) { const u8* b=p; u64 h=14695981039346656037ull; for(size_t i=0;i<n;i++) h=(h^b[i])*1099511628211ull; return h; }

static void check(void) {
    // Use one NaN payload: optimized C itself chooses different payloads when
    // adding two distinct NaNs, depending on register allocation/inlining.
    static const u32 bits[]={0,0x80000000u,0x3f800000u,0xbf800000u,1,0x807fffffu,0x7f7fffffu,0xff7fffffu,0x7f800000u,0xff800000u,0x7fc00001u,0x40490fdbu};
    __declspec(align(16)) u8 expected[512],actual[512];
    for(u32 align=0;align<16;align+=4) for(u32 a=0;a<RG_ARRAY_COUNT(bits);a++) for(u32 b=0;b<RG_ARRAY_COUNT(bits);b++) {
        RgGuiRect r={from_bits(bits[a]),from_bits(bits[b]),from_bits(bits[(a+3)%RG_ARRAY_COUNT(bits)]),from_bits(bits[(b+7)%RG_ARRAY_COUNT(bits)])};
        RgGuiRect uv={from_bits(bits[b]),from_bits(bits[a]),from_bits(bits[(b+5)%RG_ARRAY_COUNT(bits)]),from_bits(bits[(a+1)%RG_ARRAY_COUNT(bits)])};
        for(u32 capacity=0;capacity<=12;capacity++) for(u32 used=0;used<=13;used++) {
            memset(expected,0xa5,sizeof(expected)); memset(actual,0xa5,sizeof(actual));
            RgGuiGpuRenderer ref={0},test={0};
            ref.vertices=(RgGuiGpuVertex*)(expected+16+align); test.vertices=(RgGuiGpuVertex*)(actual+16+align);
            ref.vertex_count=test.vertex_count=used; ref.vertex_capacity=test.vertex_capacity=capacity;
            u32 color=0xff000000u|(a<<16)|(b<<8)|0xff;
            int ok1=reference_pack(&ref,r,uv,color),ok2=rg_gui_gpu_add_rect_vertices(&test,r,uv,color);
            if(ok1!=ok2||ref.vertex_count!=test.vertex_count||memcmp(expected,actual,sizeof(actual))) {
                fprintf(stderr,"packing mismatch align=%u a=%u b=%u used=%u capacity=%u\n",align,a,b,used,capacity); exit(1);
            }
        }
    }
    puts("packing validation passed (104832 guarded cases)");
}

typedef struct Bench {
    RgGuiGpuRenderer gpu;
    RgGuiRenderer text;
    RgTextFont font;
    RgTextGlyph glyph;
    RgGuiDrawCmd commands[1024];
    RgGuiDrawList list;
    void* text_memory;
} Bench;

static void init(Bench* b) {
    memset(b,0,sizeof(*b));
    b->font.metrics.atlas_width=64;b->font.metrics.atlas_height=64;b->font.metrics.line_height=16;
    b->glyph.codepoint='?';b->glyph.w=8;b->glyph.h=12;b->glyph.x_advance=8;
    b->font.glyphs=&b->glyph;b->font.glyph_count=1;b->font.fallback_codepoint='?';
    RgGuiRendererInitDesc d={0}; d.font=&b->font;
    d.limits=(RgGuiRendererLimits){8,16,1024,64,8192,16}; d.page_quads=32;
    size_t n=rg_gui_renderer_memory_required(&d.limits,32);
    b->text_memory=malloc(n); if(!b->text_memory) fail("allocation");
    RgArena arena={(char*)b->text_memory,n,0,n};
    if(!rg_gui_renderer_init(&b->text,&arena,&d)) fail("text init");
    b->gpu.vertex_capacity=1024*6;b->gpu.item_capacity=1024;
    b->gpu.vertices=calloc(b->gpu.vertex_capacity,sizeof(*b->gpu.vertices));
    b->gpu.items=calloc(b->gpu.item_capacity,sizeof(*b->gpu.items));
    if(!b->gpu.vertices||!b->gpu.items) fail("geometry allocation");
    for(u32 i=0;i<1024;i++) {
        RgGuiDrawCmd* cmd=&b->commands[i];
        cmd->type=RG_GUI_CMD_IMAGE;
        cmd->data.image.rect=(RgGuiRect){(f32)(i%32)*20.25f,(f32)(i/32)*10.5f,17.75f,8.25f};
        cmd->data.image.uv=(RgGuiRect){(f32)(i%8)*0.0625f,(f32)(i%16)*0.03125f,0.125f,0.25f};
        cmd->data.image.color=(rg_vec4){.x=0.2f,.y=0.3f,.z=0.7f,.w=0.9f};
        cmd->data.image.texture=(RgGuiTexture)(1u+i%2);
    }
    b->list=(RgGuiDrawList){b->commands,1024,1024};
}

static __declspec(noinline) void frame(Bench* b,u32 count,int full) {
    if(full) {
        b->list.count=count;
        rg_gui_renderer_begin_frame(&b->text);
        if(!rg_gui_gpu_prepare(&b->gpu,&b->text,&b->list,count)) fail("prepare");
    } else {
        b->gpu.vertex_count=0;
        for(u32 i=0;i<count;i++) {
            const RgGuiDrawCmd* cmd=&b->commands[i];
            if(!rg_gui_gpu_add_rect_vertices(&b->gpu,cmd->data.image.rect,cmd->data.image.uv,0xe6b34d33u)) fail("pack");
        }
    }
    sink+=b->gpu.vertices[b->gpu.vertex_count-1].color+b->gpu.vertex_count;
}
static int cmp(const void* a,const void* b) { double x=*(const double*)a,y=*(const double*)b; return (x>y)-(x<y); }

int main(int argc,char** argv) {
    int checking=argc>1 && strcmp(argv[1],"check")==0;
    if(checking) check();
    LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);clock_scale=1.0/(double)freq.QuadPart;
    Bench* b=calloc(1,sizeof(*b)); if(!b)fail("bench allocation");init(b);
    const u32 counts[]={1,128,1024};
    for(int full=0;full<2;full++) for(u32 k=0;k<3;k++) {
        if(checking) {
            frame(b,counts[k],full);
            RgGuiGpuVertex expected[6];
            RgGuiGpuRenderer ref={0};ref.vertices=expected;ref.vertex_capacity=6;
            for(u32 i=0;i<counts[k];i++) {
                const RgGuiDrawCmd* cmd=&b->commands[i];ref.vertex_count=0;
                u32 color=full?rg_gui_renderer_base_pack_color(cmd->data.image.color):0xe6b34d33u;
                if(!reference_pack(&ref,cmd->data.image.rect,cmd->data.image.uv,color)||memcmp(expected,b->gpu.vertices+i*6,sizeof(expected))) fail("frame geometry");
            }
            if(b->gpu.vertex_count!=counts[k]*6||(full&&b->gpu.item_count!=counts[k]))fail("frame counts");
            continue;
        }
        u32 count=counts[k];for(u32 i=0;i<100;i++)frame(b,count,full);
        double start=now();for(u32 i=0;i<1024;i++)frame(b,count,full);double duration=now()-start;
        u32 frames=(u32)(0.04/duration*1024);if(frames<100)frames=100;if(frames>10000000)frames=10000000;
        double times[7];for(u32 sample=0;sample<7;sample++) {start=now();for(u32 i=0;i<frames;i++)frame(b,count,full);times[sample]=(now()-start)*1e9/(double)frames;}
        qsort(times,7,sizeof(double),cmp);
        printf("{\"case\":\"%s_%u\",\"median_ns\":%.3f,\"min_ns\":%.3f,\"max_ns\":%.3f,\"geometry\":\"%016llx\",\"vertices\":%u,\"items\":%u}\n",full?"prepare":"pack",count,times[3],times[0],times[6],(unsigned long long)hash(b->gpu.vertices,sizeof(*b->gpu.vertices)*b->gpu.vertex_count),b->gpu.vertex_count,full?b->gpu.item_count:0u);
    }
    if(checking)puts("kernel and full prepare geometry validated for 1/128/1024 images");
    free(b->gpu.vertices);free(b->gpu.items);free(b->text_memory);free(b);return 0;
}
