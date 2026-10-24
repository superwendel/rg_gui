// Optional persistent textarea layout: exact bytes and widget parity.
#ifndef TEST_GUI_TEXT_AREA_CACHE_H
#define TEST_GUI_TEXT_AREA_CACHE_H

typedef char EditorCacheKeyHasNoPadding[
    sizeof(RgGuiTextAreaCacheKey)==5*sizeof(void*)+10*sizeof(u32) ? 1 : -1];
static unsigned text_area_cache_checks;
#define TEXT_AREA_CACHE_CHECK(value) do { ++text_area_cache_checks; if (!(value)) { \
    fprintf(stderr,"editor-cache check failed at %d: %s\n",__LINE__,#value);exit(1); } } while(0)

static void text_area_cache_make_font(RgTextFont* font,RgTextGlyph glyphs[4],RgTextKerning* kerning)
{
    memset(font,0,sizeof(*font));memset(glyphs,0,4*sizeof(*glyphs));memset(kerning,0,sizeof(*kerning));
    font->metrics.atlas_width=font->metrics.atlas_height=32; font->metrics.line_height=10;
    font->glyphs=glyphs;font->glyph_count=font->glyph_capacity=4;font->fallback_codepoint='?';
    const u32 codes[4]={' ','?','A','B'};const i32 advances[4]={3,5,8,6};
    for(unsigned i=0;i<4;i++){glyphs[i].codepoint=codes[i];glyphs[i].x_advance=advances[i];glyphs[i].w=i?4:0;glyphs[i].h=i?8:0;}
    kerning->left='A';kerning->right='B';kerning->x_advance=-1;
    font->kernings=kerning;font->kerning_count=font->kerning_capacity=1;
}
static void text_area_cache_equal_lines(const RgGuiTextAreaVisualLine* a,const RgGuiTextAreaVisualLine* b,u32 count)
{
    for(u32 i=0;i<count;i++) {
        TEXT_AREA_CACHE_CHECK(a[i].start==b[i].start);TEXT_AREA_CACHE_CHECK(a[i].end==b[i].end);
        TEXT_AREA_CACHE_CHECK(a[i].soft_wrap_after==b[i].soft_wrap_after);
    }
}
static int text_area_cache_parity(RgGuiContext* ctx,RgGuiTextAreaLayoutCache* cache,const char* text,
                    u32 version,f32 width)
{
    RgGuiTextAreaVisualLine reference[RG_GUI_TEXT_AREA_VISUAL_LINE_MAX];
    RgGuiTextAreaVisualLine scratch[RG_GUI_TEXT_AREA_VISUAL_LINE_MAX];
    const size_t length=strlen(text);
    const u32 count=rg_gui_text_area_build_visual_lines(ctx,text,length,width,reference,RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
    RgGuiTextAreaLayout layout={0};layout.lines=scratch;layout.cache=cache;
    TEXT_AREA_CACHE_CHECK(rg_gui_text_area_ensure_layout(ctx,&layout,text,length,version,width)==count);
    text_area_cache_equal_lines(reference,layout.lines,count);
    const u64 hits=cache?cache->hits:0,rebuilds=cache?cache->rebuilds:0;
    TEXT_AREA_CACHE_CHECK(rg_gui_text_area_ensure_layout(ctx,&layout,text,length,version,width)==count);
    TEXT_AREA_CACHE_CHECK(!cache||(cache->hits==hits&&cache->rebuilds==rebuilds));
    if(cache&&layout.lines==cache->lines) {
        TEXT_AREA_CACHE_CHECK(cache->valid&&cache->length==length&&cache->count==count);
        TEXT_AREA_CACHE_CHECK(!length||!memcmp(cache->text,text,length));
        return 1;
    }
    TEXT_AREA_CACHE_CHECK(layout.lines==scratch);
    return 0;
}
static void text_area_cache_local_invalidation(RgGuiContext* ctx,RgGuiTextAreaLayoutCache* cache,char* text)
{
    RgGuiTextAreaVisualLine scratch[RG_GUI_TEXT_AREA_VISUAL_LINE_MAX];
    RgGuiTextAreaLayout layout={0};layout.lines=scratch;layout.cache=cache;
    const size_t length=strlen(text);
    rg_gui_text_area_ensure_layout(ctx,&layout,text,length,1,24);
    const u64 rebuilds=cache->rebuilds;
    text[0]=text[0]=='A'?'B':'A';
    rg_gui_text_area_cache_invalidate(cache);
    rg_gui_text_area_ensure_layout(ctx,&layout,text,length,1,24);
    TEXT_AREA_CACHE_CHECK(cache->rebuilds==rebuilds+1);
    RgGuiTextAreaVisualLine expected[RG_GUI_TEXT_AREA_VISUAL_LINE_MAX];
    const u32 count=rg_gui_text_area_build_visual_lines(ctx,text,length,24,expected,RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
    TEXT_AREA_CACHE_CHECK(layout.count==count);text_area_cache_equal_lines(layout.lines,expected,count);
}
static u32 text_area_cache_random(u32* seed) { *seed=*seed*1664525u+1013904223u; return *seed>>8; }
static size_t text_area_cache_random_bytes(u32* seed,char* out,size_t count)
{
    static const char* pieces[]={"A","A","A","B","B"," "," ","\n","\t","\r","\xc3\xa9","\xf0","Z","AB ","BA\n"};
    size_t used=0;
    while(used<count) {
        const char* piece=pieces[text_area_cache_random(seed)%15u];size_t n=strlen(piece);
        if(used+n>count) { n=count-used; }
        memcpy(out+used,piece,n);used+=n;
    }
    return used;
}
/* One edit under an unchanged key: the lines a full build gives, through the
 * paragraph-local path when neither layout is at the line cap, else a rebuild. */
static void text_area_cache_edit_step(RgGuiContext* ctx,RgGuiTextAreaLayoutCache* cache,const char* previous,const char* text,u32 version,f32 width)
{
    const u32 old_count=cache->count;const u64 hits=cache->hits,relayouts=cache->relayouts,rebuilds=cache->rebuilds;
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(ctx,cache,text,version,width));
    if(!strcmp(previous,text)) { TEXT_AREA_CACHE_CHECK(cache->hits==hits+1&&cache->relayouts==relayouts&&cache->rebuilds==rebuilds); return; }
    const int local=old_count<RG_GUI_TEXT_AREA_VISUAL_LINE_MAX&&cache->count<RG_GUI_TEXT_AREA_VISUAL_LINE_MAX;
    TEXT_AREA_CACHE_CHECK(cache->relayouts==relayouts+(local?1u:0u));
    TEXT_AREA_CACHE_CHECK(cache->rebuilds==rebuilds+(local?0u:1u));
}
static void text_area_cache_relayout_edges(RgGuiContext* ctx,RgGuiTextAreaLayoutCache* cache)
{
    /* Empty and single-byte texts, newlines added, removed, doubled, and split
     * from \r, edits at both ends, same-length replacements, paragraphs merged
     * and split, soft-wrapped paragraphs, and whole-text replacement. */
    static const char* steps[]={"","A","","A","A\n","A\n\n","A\n","A","AB\nAB","AB\n\nAB","AB\nAB","ABZ\nAB","AB\n\nAB","AB\n\nAB\n","B\n\nAB\n",
        "ABB\nBAA","","AAAA","AAAAA","AAAA","\n\n\n","\n\n","\n","","A\r\nB","A\nB","A B","A  B","A B A B A B A B","A B A B A B A B\n",
        "A B A B A B A B\nB","B A B A B A B A\nB","B A B A B A B A\nB\n","AB","AB\n\n","AB\n\n\n","AB","A\nB\nA\nB","A\nB\nA\nB\n","B\nA\nB",
        "B\tA\tB","B \xc3\xa9 A \xf0 B","B \xc3\xa9 A \xf0 B\n\xc3\xa9","\xa9 A \xf0 B\n\xc3\xa9",NULL};
    rg_gui_text_area_cache_init(cache,cache->text,cache->text_capacity,cache->lines,cache->line_capacity);
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(ctx,cache,steps[0],1,24)&&cache->rebuilds==1);
    for(unsigned i=1;steps[i];i++) text_area_cache_edit_step(ctx,cache,steps[i-1],steps[i],i+1,24);
    TEXT_AREA_CACHE_CHECK(cache->rebuilds==1);
}
static void text_area_cache_relayout_oracle(RgGuiContext* ctx,RgGuiTextAreaLayoutCache* cache)
{
    static char doc[4096],previous[4096],insert[64];
    static const f32 widths[]={0.0f,7.0f,19.0f,24.0f,40.0f,1200.0f};
    u32 seed=7u;u64 at_cap=0,relayouts=0;
    for(unsigned w=0;w<6;w++) {
        const f32 width=widths[w];
        rg_gui_text_area_cache_init(cache,cache->text,cache->text_capacity,cache->lines,cache->line_capacity);
        /* One glyph per line at widths 0 and 7: the first stays past the line
         * cap, the second crosses it back and forth as edits land. */
        const size_t target=w==0?1300u+text_area_cache_random(&seed)%200u:w==1?1380u+text_area_cache_random(&seed)%60u:600u+text_area_cache_random(&seed)%900u;
        size_t length=text_area_cache_random_bytes(&seed,doc,target);doc[length]=0;
        TEXT_AREA_CACHE_CHECK(text_area_cache_parity(ctx,cache,doc,1,width)&&cache->rebuilds==1);
        for(unsigned step=0;step<500;step++) {
            memcpy(previous,doc,length+1);
            /* Insertions and deletions balance so the length drifts, not sinks. */
            u32 op=text_area_cache_random(&seed)%100u;
            if(length<target-150u)op=0u; else if(length>target+150u)op=50u;
            size_t at=length?text_area_cache_random(&seed)%(length+1u):0u,remove=0,add=0;
            if(op<45u)add=1u+text_area_cache_random(&seed)%24u;
            else if(op<80u)remove=1u+text_area_cache_random(&seed)%32u;
            else if(op<95u){remove=1u+text_area_cache_random(&seed)%20u;add=1u+text_area_cache_random(&seed)%20u;}
            if(remove>length-at)remove=length-at;
            if(length-remove+add>=sizeof(doc)-1u)add=0;
            text_area_cache_random_bytes(&seed,insert,add);
            memmove(doc+at+add,doc+at+remove,length-at-remove);memcpy(doc+at,insert,add);
            length=length-remove+add;doc[length]=0;
            const u64 rebuilds=cache->rebuilds;
            text_area_cache_edit_step(ctx,cache,previous,doc,step+2u,width);
            at_cap+=cache->rebuilds-rebuilds;
        }
        relayouts+=cache->relayouts;
    }
    /* Both paths ran often. */
    if(relayouts<=1500||at_cap<=400) fprintf(stderr,"oracle relayouts=%llu at_cap=%llu\n",(unsigned long long)relayouts,(unsigned long long)at_cap);
    TEXT_AREA_CACHE_CHECK(relayouts>1500&&at_cap>400);
}
static void text_area_cache_alias_checks(RgGuiContext* ctx,char* text)
{
    RgGuiTextAreaVisualLine storage[RG_GUI_TEXT_AREA_VISUAL_LINE_MAX];
    char snapshot[256];RgGuiTextAreaLayoutCache cache;
    rg_gui_text_area_cache_init(&cache,text,strlen(text),storage,RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
    TEXT_AREA_CACHE_CHECK(!text_area_cache_parity(ctx,&cache,text,1,24)&&cache.bypasses==1);
    rg_gui_text_area_cache_init(&cache,snapshot,sizeof(snapshot),storage,RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
    ctx->text_buffer=snapshot;ctx->text_buffer_capacity=sizeof(snapshot);
    TEXT_AREA_CACHE_CHECK(!text_area_cache_parity(ctx,&cache,text,1,24)&&cache.bypasses==1);
    ctx->text_buffer=NULL;ctx->text_buffer_capacity=0;
    rg_gui_text_area_cache_init(&cache,(char*)storage,sizeof(storage),storage,RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
    TEXT_AREA_CACHE_CHECK(!text_area_cache_parity(ctx,&cache,text,1,24)&&cache.bypasses==1);
    rg_gui_text_area_cache_init(&cache,snapshot,sizeof(snapshot),storage,RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
    RgGuiTextAreaLayout layout={0};layout.lines=storage;layout.cache=&cache;
    rg_gui_text_area_ensure_layout(ctx,&layout,text,strlen(text),1,24);
    TEXT_AREA_CACHE_CHECK(cache.bypasses==1&&!cache.valid&&layout.lines==storage);
    rg_gui_text_area_cache_init(&cache,(char*)&cache,sizeof(cache),storage,RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
    TEXT_AREA_CACHE_CHECK(!text_area_cache_parity(ctx,&cache,text,1,24)&&cache.bypasses==1);
    rg_gui_text_area_cache_init(&cache,snapshot,sizeof(snapshot),(RgGuiTextAreaVisualLine*)&cache,1);
    TEXT_AREA_CACHE_CHECK(!text_area_cache_parity(ctx,&cache,text,1,24)&&cache.bypasses==1);
    memset(storage,0,sizeof(storage));
    rg_gui_text_area_cache_init(&cache,snapshot,sizeof(snapshot),storage,RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
    TEXT_AREA_CACHE_CHECK(!text_area_cache_parity(ctx,&cache,(const char*)storage,1,24)&&cache.bypasses==1);
    TEXT_AREA_CACHE_CHECK(*(const char*)storage=='\0');
}
static void text_area_cache_widget_rect_equal(RgGuiRect a,RgGuiRect b)
{
    TEXT_AREA_CACHE_CHECK(a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h);
}
static void text_area_cache_widget_draw_equal(const RgGuiContext* a,const RgGuiContext* b)
{
    const RgGuiDrawList* x=rg_gui_draw_list(a);const RgGuiDrawList* y=rg_gui_draw_list(b);
    TEXT_AREA_CACHE_CHECK(x->count==y->count&&x->count>0);
    for(u32 i=0;i<x->count;i++) {
        const RgGuiDrawCmd* p=&x->cmds[i];const RgGuiDrawCmd* q=&y->cmds[i];
        TEXT_AREA_CACHE_CHECK(p->type==q->type);
        switch(p->type) {
        case RG_GUI_CMD_RECT:
            text_area_cache_widget_rect_equal(p->data.rect.rect,q->data.rect.rect);
            TEXT_AREA_CACHE_CHECK(p->color==q->color);break;
        case RG_GUI_CMD_TEXT:
            TEXT_AREA_CACHE_CHECK(p->data.text.pos.x==q->data.text.pos.x&&p->data.text.pos.y==q->data.text.pos.y);
            TEXT_AREA_CACHE_CHECK(p->data.text.scale==q->data.text.scale);
            TEXT_AREA_CACHE_CHECK(!strcmp(p->data.text.text,q->data.text.text));
            TEXT_AREA_CACHE_CHECK(p->color==q->color);break;
        case RG_GUI_CMD_CLIP_PUSH:text_area_cache_widget_rect_equal(p->data.clip.rect,q->data.clip.rect);break;
        case RG_GUI_CMD_CLIP_POP:break;
        default:TEXT_AREA_CACHE_CHECK(p->type==RG_GUI_CMD_RECT);break; /* no other widget primitive expected */
        }
    }
}
static void text_area_cache_widget_key(RgInputState* input,RgInputEventQueue* events,SDL_Scancode key,SDL_Keymod mods)
{
    SDL_Event event={0};event.type=SDL_EVENT_KEY_DOWN;
    event.key.scancode=key;event.key.mod=mods;event.key.down=true;
    rg_input_process_event_ex(input,&event,events);
    event.type=SDL_EVENT_KEY_UP;event.key.down=false;
    rg_input_process_event_ex(input,&event,events);
}
static void text_area_cache_widget_text(RgInputState* input,RgInputEventQueue* events,const char* text,int preedit,int start,int length)
{
    SDL_Event event={0};event.type=preedit?SDL_EVENT_TEXT_EDITING:SDL_EVENT_TEXT_INPUT;
    if(preedit){event.edit.text=text;event.edit.start=start;event.edit.length=length;}
    else event.text.text=text;
    rg_input_process_event_ex(input,&event,events);
}
static void text_area_cache_widget_parity(void)
{
    RgTextFont font;RgTextGlyph glyphs[4];RgTextKerning kerning;
    text_area_cache_make_font(&font,glyphs,&kerning);font.kerning_count=0;glyphs[3].x_advance=9;
    RgGuiInitDesc desc={0};desc.font=&font;desc.max_draw_cmds=256;desc.text_buffer_size=KB(4);
    const size_t bytes=rg_gui_memory_required(&desc);TEXT_AREA_CACHE_CHECK(bytes>0);
    void* memories[2]={malloc(bytes),malloc(bytes)};TEXT_AREA_CACHE_CHECK(memories[0]&&memories[1]);
    RgGuiContext gui[2];RgGuiTextAreaState area[2]={0};
    char text[2][256]={"AAAAAAAA","AAAAAAAA"};
    for(unsigned i=0;i<2;i++) {
        RgArena arena={(char*)memories[i],bytes,0,bytes};TEXT_AREA_CACHE_CHECK(rg_gui_init(&gui[i],&arena,&desc));
        gui[i].style.padding=0;gui[i].style.border_thickness=0;
        gui[i].style.scroll_bar_width=0;gui[i].style.text_height=10;
    }
    RgGuiTextAreaLayoutCache cache;char snapshot[1024];RgGuiTextAreaVisualLine lines[128];
    rg_gui_text_area_cache_init(&cache,snapshot,sizeof(snapshot),lines,RG_ARRAY_COUNT(lines));
    area[1].layout_cache=&cache;
    RgInputState input;rg_input_init(&input);
    RgInputEvent event_storage[32];char event_text[256];RgInputEventQueue events;
    rg_input_event_queue_init(&events,event_storage,RG_ARRAY_COUNT(event_storage),event_text,sizeof(event_text));
    RgGuiRect rect=rg_gui_make_rect(20,20,16,80);
    for(unsigned frame=0;frame<17;frame++) {
        rg_input_begin_frame(&input);rg_input_event_queue_reset(&events,SDL_KMOD_NONE);
        const int down=frame==0||frame==14;
        SDL_Event mouse={0};mouse.type=SDL_EVENT_MOUSE_MOTION;
        mouse.motion.x=28.0f;mouse.motion.y=frame>=14?35.0f:25.0f;
        mouse.motion.state=down?SDL_BUTTON_LMASK:0;
        rg_input_process_event_ex(&input,&mouse,&events);
        if(down!=(int)input.current_mouse[RG_MOUSE_BUTTON_LEFT]) {
            mouse.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
            mouse.button.button=SDL_BUTTON_LEFT;mouse.button.down=down!=0;
            mouse.button.clicks=1;mouse.button.x=28.0f;mouse.button.y=frame>=14?35.0f:25.0f;
            rg_input_process_event_ex(&input,&mouse,&events);
        }
        input.mouse_x=28;input.mouse_y=frame>=14?35:25;
        input.current_mouse[RG_MOUSE_BUTTON_LEFT]=down!=0;
        if(frame==3) {
            text_area_cache_widget_key(&input,&events,SDL_SCANCODE_DOWN,SDL_KMOD_NONE);
            text_area_cache_widget_key(&input,&events,SDL_SCANCODE_DOWN,SDL_KMOD_NONE);
            text_area_cache_widget_key(&input,&events,SDL_SCANCODE_LEFT,SDL_KMOD_LSHIFT);
            text_area_cache_widget_text(&input,&events,"B",0,0,0);
            text_area_cache_widget_key(&input,&events,SDL_SCANCODE_UP,SDL_KMOD_NONE);
            text_area_cache_widget_key(&input,&events,SDL_SCANCODE_UP,SDL_KMOD_NONE);
            text_area_cache_widget_text(&input,&events,"AA",1,0,1);
        }
        if(frame==4)text_area_cache_widget_text(&input,&events,"BB",1,1,1);
        if(frame==5)text_area_cache_widget_text(&input,&events,"A\xc3\xa9" "B",1,1,1);
        if(frame==6)text_area_cache_widget_text(&input,&events,"",1,0,0);
        if(frame==7) {
            text_area_cache_widget_key(&input,&events,SDL_SCANCODE_RIGHT,SDL_KMOD_LSHIFT);
            text_area_cache_widget_text(&input,&events,"AB",1,0,1);
        }
        if(frame==8)text_area_cache_widget_text(&input,&events,"B",0,0,0);
        if(frame==10)text[0][0]=text[1][0]='B';
        if(frame==11)rect.w=24;
        if(frame==12)gui[0].style.text_height=gui[1].style.text_height=12.5f;
        if(frame==13){glyphs[2].x_advance=9;rg_gui_text_area_cache_invalidate(&cache);}
        if(frame==16) {
            text_area_cache_widget_text(&input,&events,"A",0,0,0);
            text_area_cache_widget_key(&input,&events,SDL_SCANCODE_UP,SDL_KMOD_NONE);
            text_area_cache_widget_key(&input,&events,SDL_SCANCODE_DOWN,SDL_KMOD_LSHIFT);
        }
        int changed[2];
        for(unsigned i=0;i<2;i++) {
            rg_gui_begin_frame_ex(&gui[i],&input,&events,0,1.0f/60.0f);
            changed[i]=rg_gui_text_area(&gui[i],&area[i],text[i],sizeof(text[i]),rect,0x7311u);
            rg_gui_end_frame(&gui[i]);
            TEXT_AREA_CACHE_CHECK(rg_gui_diagnostics(&gui[i])->flags==0);
            TEXT_AREA_CACHE_CHECK(gui[i].focus_id==0x7311u&&gui[i].text_edit_state->active);
        }
        TEXT_AREA_CACHE_CHECK(changed[0]==changed[1]&&!strcmp(text[0],text[1]));
        const RgGuiTextEditState* a=gui[0].text_edit_state;const RgGuiTextEditState* b=gui[1].text_edit_state;
        TEXT_AREA_CACHE_CHECK(a->id==b->id&&a->active==b->active&&a->length==b->length&&a->cursor==b->cursor);
        TEXT_AREA_CACHE_CHECK(a->selection_anchor==b->selection_anchor&&a->selection_start==b->selection_start&&a->selection_end==b->selection_end);
        TEXT_AREA_CACHE_CHECK(a->view_start==b->view_start&&a->content_version==b->content_version&&a->dirty==b->dirty);
        TEXT_AREA_CACHE_CHECK(a->preedit_length==b->preedit_length&&!strcmp(a->preedit,b->preedit));
        TEXT_AREA_CACHE_CHECK(a->preedit_selection_start==b->preedit_selection_start&&a->preedit_selection_length==b->preedit_selection_length);
        TEXT_AREA_CACHE_CHECK(gui[0].active_id==gui[1].active_id&&gui[0].cursor_visible==gui[1].cursor_visible);
        TEXT_AREA_CACHE_CHECK(area[0].panel.scroll_y==area[1].panel.scroll_y&&area[0].panel.content_height==area[1].panel.content_height);
        TEXT_AREA_CACHE_CHECK(gui[0].platform_output.ime_caret_valid&&gui[1].platform_output.ime_caret_valid);
        TEXT_AREA_CACHE_CHECK(gui[0].platform_output.wants_text_input&&gui[1].platform_output.wants_text_input);
        text_area_cache_widget_rect_equal(gui[0].platform_output.ime_caret_rect,gui[1].platform_output.ime_caret_rect);
        text_area_cache_widget_draw_equal(&gui[0],&gui[1]);
        if(frame==0)TEXT_AREA_CACHE_CHECK(a->cursor==1&&gui[0].active_id==0x7311u);
        if(frame==1)TEXT_AREA_CACHE_CHECK(gui[0].active_id==0&&a->cursor==1);
        if(frame==2)TEXT_AREA_CACHE_CHECK(cache.hits>=2);
        if(frame==3)TEXT_AREA_CACHE_CHECK(changed[0]&&!strcmp(text[0],"AAAABAAA")&&a->cursor==2&&a->preedit_length==2&&area[0].panel.content_height==60);
        if(frame==4)TEXT_AREA_CACHE_CHECK(!changed[0]&&!strcmp(a->preedit,"BB")&&a->preedit_selection_start==1&&a->preedit_selection_length==1);
        if(frame==5)TEXT_AREA_CACHE_CHECK(!changed[0]&&a->preedit_length==4);
        if(frame==6)TEXT_AREA_CACHE_CHECK(!changed[0]&&!a->preedit_length&&!strcmp(text[0],"AAAABAAA")&&area[0].panel.content_height==50);
        if(frame==7)TEXT_AREA_CACHE_CHECK(a->selection_start!=a->selection_end&&a->preedit_length==2);
        if(frame==8)TEXT_AREA_CACHE_CHECK(changed[0]&&!a->preedit_length&&!strcmp(text[0],"AABABAAA"));
        if(frame==16)TEXT_AREA_CACHE_CHECK(changed[0]&&a->selection_start!=a->selection_end);
    }
    free(memories[1]);free(memories[0]);
}
static int test_text_area_cache(void)
{
    RgTextFont font;RgTextGlyph glyphs[4];RgTextKerning kerning;
    text_area_cache_make_font(&font,glyphs,&kerning);
    RgGuiTextLookup* lookups=malloc(2*sizeof(*lookups));TEXT_AREA_CACHE_CHECK(lookups!=NULL);
    TEXT_AREA_CACHE_CHECK(rg_gui_text_lookup_init(&lookups[0],&font));TEXT_AREA_CACHE_CHECK(rg_gui_text_lookup_init(&lookups[1],&font));
    RgGuiContext ctx={0};ctx.font=&font;ctx.text_lookup=&lookups[0];ctx.style.text_height=10;ctx.style.char_width=7;
    RgGuiTextAreaVisualLine storage[RG_GUI_TEXT_AREA_VISUAL_LINE_MAX];char snapshot[16384];
    RgGuiTextAreaLayoutCache cache;
    rg_gui_text_area_cache_init(&cache,snapshot,sizeof(snapshot),storage,RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
    char text[512]="AAB ABB AAB\nABBA\n";
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,1,24)&&cache.rebuilds==1&&cache.hits==0);
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,1,24)&&cache.hits==1);
    /* Same pointer, same length, same version: external bytes still invalidate. */
    text[0]=' ';TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,1,24)&&cache.rebuilds==1&&cache.relayouts==1);
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,0,24)&&cache.hits==2);
    strcpy(text,"ABA ABA\nB A\n");TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,2,24));
    strcpy(text,"AAB ABB AAB\nABBA\n");TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,3,24));
    u64 before=cache.rebuilds;
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,3,40)&&cache.rebuilds==before+1);
    ctx.style.text_height=15;before=cache.rebuilds;
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,3,40)&&cache.rebuilds==before+1);
    RgTextFont second_font=font;ctx.font=&second_font;before=cache.rebuilds;
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,3,40)&&cache.rebuilds==before+1);
    ctx.font=&font;ctx.text_lookup=&lookups[1];before=cache.rebuilds;
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,3,40)&&cache.rebuilds==before+1);
    /* Mutated data at identical addresses requires explicit CPU invalidation. */
    glyphs[2].x_advance=11;rg_gui_text_area_cache_invalidate(&cache);before=cache.rebuilds;
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,3,40)&&cache.rebuilds==before+1);
    kerning.x_advance=-3;TEXT_AREA_CACHE_CHECK(rg_gui_text_lookup_init(&lookups[1],&font));
    rg_gui_text_area_cache_invalidate(&cache);TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,3,40));
    lookups[1].ascii_kerning['A'*128+'B']=-5;rg_gui_text_area_cache_invalidate(&cache);
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,3,40));
    ctx.text_lookup=NULL;font.fallback_codepoint='A';before=cache.rebuilds;
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,"? Z A",0,22)&&cache.rebuilds==before+1);
    ctx.style.text_height=10;
    const char* cases[]={"", "\n", "A\r\nB\n", "A\tB AB\tAA\n", "\xc3\xa9 A B\n", "\xf0\x28\x8c\x28 AB", "   \t  ", "AAAAABBBBB"};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,cases[i],i,19));TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,cases[i],0,19));
        TEXT_AREA_CACHE_CHECK(!text_area_cache_parity(&ctx,NULL,cases[i],i,19));
    }
    /* IME display text may come from a reused frame buffer: compare its bytes. */
    strcpy(text,"AB preedit A\nB");TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,7,24));
    text[3]='X';before=cache.relayouts;TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,7,24)&&cache.relayouts==before+1);
    strcpy(text,"AB A\nB");TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,7,24));
    strcpy(text,"AAB ABB AAB\nABBA\n");text_area_cache_local_invalidation(&ctx,&cache,text);
    rg_gui_text_area_cache_init(&cache,snapshot,4,storage,RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
    TEXT_AREA_CACHE_CHECK(!text_area_cache_parity(&ctx,&cache,text,1,24)&&cache.bypasses==1);
    rg_gui_text_area_cache_init(&cache,snapshot,sizeof(snapshot),storage,1);
    TEXT_AREA_CACHE_CHECK(!text_area_cache_parity(&ctx,&cache,text,1,24)&&cache.bypasses==1);
    rg_gui_text_area_cache_init(&cache,NULL,0,NULL,0);
    TEXT_AREA_CACHE_CHECK(!text_area_cache_parity(&ctx,&cache,text,1,24)&&cache.bypasses==1);
    rg_gui_text_area_cache_init(&cache,snapshot,strlen(text),storage,RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,text,1,24)); /* snapshot requires no trailing NUL */
    text_area_cache_alias_checks(&ctx,text);
    /* Match the 8 KiB workload and exercise the original 1024-line boundary. */
    char long_text[8193];
    for(unsigned i=0;i<8192;i++) long_text[i]=i%64==63?'\n':(i%3?'A':'B');
    long_text[8192]='\0';
    rg_gui_text_area_cache_init(&cache,snapshot,sizeof(snapshot),storage,RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,long_text,0,1200));
    before=cache.hits;TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,long_text,0,1200)&&cache.hits==before+1);
    long_text[4096]=' ';before=cache.relayouts;
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,long_text,0,1200)&&cache.relayouts==before+1);
    memset(long_text,'\n',2048);long_text[2048]='\0';before=cache.rebuilds;
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,long_text,0,24)&&cache.count==RG_GUI_TEXT_AREA_VISUAL_LINE_MAX&&cache.rebuilds==before+1);
    /* A layout at the cap may be cut short, so edits to it rebuild. */
    long_text[5]='A';before=cache.rebuilds;
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,long_text,0,24)&&cache.rebuilds==before+1);
    text_area_cache_relayout_edges(&ctx,&cache);
    text_area_cache_relayout_oracle(&ctx,&cache);
    /* Independent editor caches cannot overwrite one another's line metadata. */
    RgGuiTextAreaLayoutCache other;char other_text[256];RgGuiTextAreaVisualLine other_lines[128];
    rg_gui_text_area_cache_init(&cache,snapshot,sizeof(snapshot),storage,RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
    rg_gui_text_area_cache_init(&other,other_text,sizeof(other_text),other_lines,128);
    TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,"A A A",1,16));TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&other,"BBB\nA",1,16));
    before=cache.hits;TEXT_AREA_CACHE_CHECK(text_area_cache_parity(&ctx,&cache,"A A A",1,16)&&cache.hits==before+1);
    free(lookups);
    text_area_cache_widget_parity();
    printf("Text area cache regressions passed (%u assertions)\n",text_area_cache_checks);
    return 1;
}

#undef TEXT_AREA_CACHE_CHECK
#endif
