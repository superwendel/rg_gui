#ifndef RG_GUI_THIRD_PARTY_BENCH_API_H
#define RG_GUI_THIRD_PARTY_BENCH_API_H
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BENCH_WIDTH 1280
#define BENCH_HEIGHT 800
#define BENCH_LABELS 128
#define BENCH_ROWS 32
#define BENCH_DOCUMENT_CAPACITY 16384
/* Geometry/style are deliberately shared; native widget shapes may differ. */
enum BenchCase { BENCH_STATIC, BENCH_DYNAMIC, BENCH_INSPECTOR, BENCH_LIST,
                 BENCH_MIXED, BENCH_BROWSER, BENCH_EDITOR, BENCH_DOCKS, BENCH_CASE_COUNT };
typedef struct BenchGlyph { uint32_t codepoint; int x,y,w,h,x_offset,y_offset,advance; } BenchGlyph;
typedef struct BenchFont {
 int width,height,line_height,ascent,descent,glyph_count;
 BenchGlyph glyphs[256]; const unsigned char* rgba;
 const char* font_data; size_t font_data_size;
 int white_x,white_y;
} BenchFont;
typedef struct BenchInit {
 SDL_GPUDevice* device; SDL_GPUTexture* atlas; const BenchFont* font;
 const char* shader_root; const char* initial_document;
 uint32_t flights;
 enum BenchCase case_id;
} BenchInit;
typedef struct BenchScene {
 enum BenchCase case_id; uint32_t frame;
 char labels[BENCH_LABELS][128]; char rows[BENCH_ROWS][128];
 uint32_t first_row; char filter[64];
 float values[32]; int checks[32];
 float mouse_x,mouse_y; int mouse_down;
 /* Editor: click down/up at frames2/3; from frame4 alternate insertx/backspace.
  * Keep Backspace down only on text_action2; release it on action1. */
 int text_action;
} BenchScene;
typedef struct BenchStats {
 uint32_t commands,vertices,glyphs,draw_calls;
 uint32_t widgets,visible_rows,checked_bits;
 uint32_t editor_length; uint64_t editor_hash;
 float first_slider; int selected_row;
} BenchStats;
typedef struct BenchAdapter {
 void* state;
 int (*build)(void*,const BenchScene*,BenchStats*); /* Native UI + CPU renderer prep. */
 int (*upload)(void*,SDL_GPUCommandBuffer*,uint32_t); /* Copy pass before render. */
 int (*draw)(void*,SDL_GPUCommandBuffer*,SDL_GPURenderPass*,BenchStats*);
 void (*commit)(void*,uint32_t);
 int (*inspect)(void*,BenchStats*); /* Untimed semantic checks; may hash text. */
 void (*destroy)(void*);
} BenchAdapter;
int bench_rg_init(BenchAdapter*,const BenchInit*);
int bench_imgui_init(BenchAdapter*,const BenchInit*);
static inline uint64_t bench_hash_bytes(const char* data,size_t length) {
 uint64_t hash=1469598103934665603ull;
 for(size_t i=0;i<length;i++){hash^=(unsigned char)data[i];hash*=1099511628211ull;}
 return hash;
}
#ifdef __cplusplus
}
#endif
#endif
