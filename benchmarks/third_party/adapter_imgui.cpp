/* Native Dear ImGui adapter for the local SDL_GPU comparison.
 * The pinned backend is used unchanged. Font pixels/metrics are imported from
 * the same immutable rgfont fixture; no second font rasterizer is involved. */
#include "bench_api.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdlgpu3.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

struct ImGuiBenchState
{
    ImGuiContext* context = nullptr;
    const BenchFont* common_font = nullptr;
    ImFontLoader loader;
    ImFont* font = nullptr;
    ImDrawData* draw_data = nullptr;
    bool backend_ready = false;
    bool checks[32] = {};
    float values[32] = {};
    char document[BENCH_DOCUMENT_CAPACITY] = {};
    char filter[64] = {};
    int selected_row = -1;
    int last_case = -1;
    ImGuiID dockspace_id = 0;
};

static const BenchGlyph* common_glyph(const BenchFont* font, ImWchar codepoint)
{
    for (int i = 0; i < font->glyph_count; ++i)
        if (font->glyphs[i].codepoint == codepoint)
            return &font->glyphs[i];
    return nullptr;
}

static bool common_font_source_init(ImFontAtlas*, ImFontConfig* source)
{
    source->FontLoaderData = source->FontData;
    return source->FontLoaderData != nullptr;
}

static void common_font_source_destroy(ImFontAtlas*, ImFontConfig* source)
{
    source->FontLoaderData = nullptr;
}

static bool common_font_contains(ImFontAtlas*, ImFontConfig* source, ImWchar codepoint)
{
    return common_glyph(static_cast<const BenchFont*>(source->FontLoaderData), codepoint) != nullptr;
}

static bool common_font_baked_init(ImFontAtlas*, ImFontConfig* source,
                                   ImFontBaked* baked, void*)
{
    const BenchFont* font = static_cast<const BenchFont*>(source->FontLoaderData);
    if (baked->Size != static_cast<float>(font->line_height) || baked->RasterizerDensity != 1.0f)
        return false;
    baked->Ascent = static_cast<float>(font->ascent);
    baked->Descent = -static_cast<float>(font->descent);
    return true;
}

static bool common_font_load_glyph(ImFontAtlas* atlas, ImFontConfig* source,
                                  ImFontBaked* baked, void*, ImWchar codepoint,
                                  ImFontGlyph* output, float* advance)
{
    const BenchFont* font = static_cast<const BenchFont*>(source->FontLoaderData);
    const BenchGlyph* glyph = common_glyph(font, codepoint);
    if (!glyph || baked->Size != static_cast<float>(font->line_height))
        return false;
    if (advance)
    {
        *advance = static_cast<float>(glyph->advance);
        return true;
    }
    if (!output)
        return false;
    output->Codepoint = codepoint;
    output->AdvanceX = static_cast<float>(glyph->advance);
    output->X0 = static_cast<float>(glyph->x_offset);
    output->Y0 = static_cast<float>(glyph->y_offset);
    output->X1 = output->X0 + static_cast<float>(glyph->w);
    output->Y1 = output->Y0 + static_cast<float>(glyph->h);
    output->Visible = glyph->w > 0 && glyph->h > 0;
    if (!output->Visible)
        return true;
    if (glyph->x < 0 || glyph->y < 0 || glyph->x + glyph->w > font->width ||
        glyph->y + glyph->h > font->height)
        return false;
    ImFontAtlasRectId rectangle = ImFontAtlasPackAddRect(atlas, glyph->w, glyph->h);
    if (rectangle == ImFontAtlasRectId_Invalid)
        return false;
    ImTextureRect* packed = ImFontAtlasPackGetRect(atlas, rectangle);
    output->PackId = rectangle;
    const unsigned char* pixels = font->rgba +
        (static_cast<size_t>(glyph->y) * font->width + glyph->x) * 4u;
    ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, source, output, packed,
                                     pixels, ImTextureFormat_RGBA32, font->width * 4);
    return true;
}

static void begin_fullscreen_host()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(BENCH_WIDTH, BENCH_HEIGHT), ImGuiCond_Always);
    ImGui::Begin("##benchmark_host", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoDocking);
}

static void positioned_text(float x, float y, const char* text)
{
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::TextUnformatted(text);
}

static void submit_inspector(ImGuiBenchState* state, const BenchScene* scene)
{
    const bool persistent = scene->case_id == BENCH_MIXED;
    for (int i = 0; i < 32; ++i)
        state->values[i] = scene->values[i];
    if (!persistent || state->last_case != scene->case_id)
        for (int i = 0; i < 32; ++i)
            state->checks[i] = scene->checks[i] != 0;
    for (int i = 0; i < 32; ++i)
    {
        const float y = 16.0f + 22.0f * i;
        char check_label[24], value_label[24];
        std::snprintf(check_label, sizeof(check_label), "Enable%02d", i);
        std::snprintf(value_label, sizeof(value_label), "Value%02d", i);
        ImGui::SetCursorScreenPos(ImVec2(16, y));
        ImGui::Checkbox(check_label, &state->checks[i]);
        ImGui::SetCursorScreenPos(ImVec2(340, y));
        ImGui::SetNextItemWidth(300.0f);
        ImGui::SliderFloat(value_label, &state->values[i], 0.0f, 1.0f, "%.3f");
    }
}

static void submit_rows(ImGuiBenchState* state, const BenchScene* scene, float first_y)
{
    for (int i = 0; i < BENCH_ROWS; ++i)
    {
        ImGui::SetCursorScreenPos(ImVec2(16, first_y + 22.0f * i));
        ImGui::PushID(i);
        const int row = static_cast<int>(scene->first_row) + i;
        if (ImGui::Selectable(scene->rows[i], state->selected_row == row,
                              ImGuiSelectableFlags_None, ImVec2(600, 20)))
            state->selected_row = row;
        ImGui::PopID();
    }
}

static void reset_docks(ImGuiBenchState* state)
{
    ImGui::DockBuilderRemoveNode(state->dockspace_id);
    ImGui::DockBuilderAddNode(state->dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(state->dockspace_id, ImVec2(0, 0));
    ImGui::DockBuilderSetNodeSize(state->dockspace_id, ImVec2(BENCH_WIDTH, BENCH_HEIGHT));
    ImGuiID left = 0, right = 0, upper_left = 0, lower_left = 0, upper_right = 0, lower_right = 0;
    ImGui::DockBuilderSplitNode(state->dockspace_id, ImGuiDir_Left, 0.5f, &left, &right);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.5f, &upper_left, &lower_left);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.5f, &upper_right, &lower_right);
    const ImGuiID leaves[4] = {upper_left, upper_right, lower_left, lower_right};
    for (int i = 0; i < 4; ++i)
    {
        char name[24];
        std::snprintf(name, sizeof(name), "Panel %d", i);
        ImGui::DockBuilderDockWindow(name, leaves[i]);
    }
    ImGui::DockBuilderFinish(state->dockspace_id);
}

static void submit_docks(ImGuiBenchState* state, const BenchScene* scene)
{
    state->dockspace_id = ImGui::GetID("##benchmark_dockspace");
    if (state->last_case != BENCH_DOCKS ||
        (scene->frame > 0u && scene->frame % 256u == 0u))
        reset_docks(state);
    ImGui::DockSpace(state->dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
    for (int panel = 0; panel < 4; ++panel)
    {
        char name[24];
        std::snprintf(name, sizeof(name), "Panel %d", panel);
        /* DockBuilder stores first-use docking in the in-memory settings.
         * IniFilename is null, so allowing those settings causes no file I/O. */
        ImGui::Begin(name);
        for (int row = 0; row < 32; ++row)
        {
            ImGui::SetCursorPos(ImVec2(8.0f + 310.0f * (row / 16),
                                      28.0f + 22.0f * (row % 16)));
            ImGui::TextUnformatted(scene->labels[panel * 32 + row]);
        }
        ImGui::End();
    }
}

static int imgui_build(void* opaque, const BenchScene* scene, BenchStats* stats)
{
    ImGuiBenchState* state = static_cast<ImGuiBenchState*>(opaque);
    if (!state || !scene || !stats)
        return 0;
    std::memset(stats, 0, sizeof(*stats));
    ImGui::SetCurrentContext(state->context);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(BENCH_WIDTH, BENCH_HEIGHT);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    io.DeltaTime = 1.0f / 60.0f;
    io.AddMousePosEvent(scene->mouse_x, scene->mouse_y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, scene->mouse_down != 0);
    io.AddKeyEvent(ImGuiKey_Backspace, scene->text_action == 2);
    if (scene->text_action == 1)
        io.AddInputCharactersUTF8("x");
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();
    begin_fullscreen_host();
    switch (scene->case_id)
    {
        case BENCH_STATIC:
        case BENCH_DYNAMIC:
            for (int i = 0; i < BENCH_LABELS; ++i)
                positioned_text(16.0f + 310.0f * (i / 32),
                                12.0f + 22.0f * (i % 32), scene->labels[i]);
            stats->widgets = BENCH_LABELS;
            break;
        case BENCH_INSPECTOR:
        case BENCH_MIXED:
            submit_inspector(state, scene);
            stats->widgets = 64;
            break;
        case BENCH_LIST:
            submit_rows(state, scene, 32.0f);
            stats->widgets = stats->visible_rows = BENCH_ROWS;
            break;
        case BENCH_BROWSER:
            std::snprintf(state->filter, sizeof(state->filter), "%s", scene->filter);
            ImGui::SetCursorScreenPos(ImVec2(16, 12));
            ImGui::SetNextItemWidth(600.0f);
            ImGui::InputText("##filter", state->filter, sizeof(state->filter));
            submit_rows(state, scene, 48.0f);
            stats->widgets = BENCH_ROWS + 1;
            stats->visible_rows = BENCH_ROWS;
            break;
        case BENCH_EDITOR:
            ImGui::SetCursorScreenPos(ImVec2(16, 16));
            /* Both editors use a 16px line pitch, 1px inset and 14px
             * reserved scrollbar width in this comparison. */
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 1));
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 14.0f);
            ImGui::InputTextMultiline("##editor", state->document, sizeof(state->document),
                ImVec2(1248, 768), ImGuiInputTextFlags_WordWrap);
            ImGui::PopStyleVar(2);
            stats->widgets = 1;
            break;
        case BENCH_DOCKS:
            submit_docks(state, scene);
            stats->widgets = BENCH_LABELS;
            break;
        default:
            ImGui::End();
            ImGui::EndFrame();
            return 0;
    }
    if (scene->case_id != BENCH_DOCKS)
        ImGui::End();
    ImGui::Render();
    state->draw_data = ImGui::GetDrawData();
    state->last_case = scene->case_id;
    if (!state->draw_data || state->draw_data->TotalVtxCount <= 0)
        return SDL_SetError("Dear ImGui produced no draw geometry"), 0;
    stats->vertices = static_cast<uint32_t>(state->draw_data->TotalVtxCount);
    for (const ImDrawList* list : state->draw_data->CmdLists)
        stats->commands += static_cast<uint32_t>(list->CmdBuffer.Size);
    return 1;
}

static int imgui_upload(void* opaque, SDL_GPUCommandBuffer* command, uint32_t slot)
{
    (void)slot;
    ImGuiBenchState* state = static_cast<ImGuiBenchState*>(opaque);
    if (!state || !command || !state->draw_data)
        return 0;
    ImGui::SetCurrentContext(state->context);
    ImGui_ImplSDLGPU3_PrepareDrawData(state->draw_data, command);
    return 1;
}

static int imgui_draw(void* opaque, SDL_GPUCommandBuffer* command,
                      SDL_GPURenderPass* pass, BenchStats* stats)
{
    ImGuiBenchState* state = static_cast<ImGuiBenchState*>(opaque);
    if (!state || !command || !pass || !stats || !state->draw_data)
        return 0;
    ImGui::SetCurrentContext(state->context);
    ImGui_ImplSDLGPU3_RenderDrawData(state->draw_data, command, pass);
    for (const ImDrawList* list : state->draw_data->CmdLists)
        for (const ImDrawCmd& draw : list->CmdBuffer)
            if (draw.ElemCount && !draw.UserCallback)
                ++stats->draw_calls;
    return 1;
}

static void imgui_commit(void*, uint32_t) {}

static int imgui_inspect(void* opaque, BenchStats* stats)
{
    ImGuiBenchState* state = static_cast<ImGuiBenchState*>(opaque);
    if (!state || !stats)
        return 0;
    stats->checked_bits = 0;
    for (int i = 0; i < 32; ++i)
        if (state->checks[i])
            stats->checked_bits |= 1u << i;
    stats->first_slider = state->values[0];
    stats->selected_row = state->selected_row;
    if (state->last_case == BENCH_EDITOR)
    {
        size_t length = std::strlen(state->document);
        stats->editor_length = static_cast<uint32_t>(length);
        stats->editor_hash = bench_hash_bytes(state->document, length);
    }
    return 1;
}

static void imgui_destroy(void* opaque)
{
    ImGuiBenchState* state = static_cast<ImGuiBenchState*>(opaque);
    if (!state)
        return;
    if (state->context)
    {
        ImGui::SetCurrentContext(state->context);
        if (state->backend_ready)
            ImGui_ImplSDLGPU3_Shutdown();
        ImGui::DestroyContext(state->context);
    }
    delete state;
}

extern "C" int bench_imgui_init(BenchAdapter* adapter, const BenchInit* init)
{
    if (!adapter || !init || !init->device || !init->font || !init->font->rgba ||
        init->font->line_height != 16 || !init->initial_document ||
        std::strlen(init->initial_document) >= BENCH_DOCUMENT_CAPACITY)
        return 0;
    ImGuiBenchState* state = new ImGuiBenchState();
    state->common_font = init->font;
    state->context = ImGui::CreateContext();
    if (!state->context)
    {
        delete state;
        return 0;
    }
    ImGui::SetCurrentContext(state->context);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigInputTrickleEventQueue = false;
    io.ConfigInputTextCursorBlink = false;
    io.DisplaySize = ImVec2(BENCH_WIDTH, BENCH_HEIGHT);
    io.AddFocusEvent(true);
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = 16.0f;
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(4, 2);
    style.ItemSpacing = ImVec2(6, 2);
    style.ItemInnerSpacing = ImVec2(6, 2);
    style.WindowRounding = style.ChildRounding = style.FrameRounding = 0;
    style.PopupRounding = style.ScrollbarRounding = style.GrabRounding = style.TabRounding = 0;
    style.WindowBorderSize = style.ChildBorderSize = style.FrameBorderSize = 1;
    style.AntiAliasedLines = style.AntiAliasedLinesUseTex = style.AntiAliasedFill = false;
    style.Colors[ImGuiCol_Text] = ImVec4(1, 1, 1, 1);
    state->loader.Name = "shared prebaked Inter RGBA and metrics";
    state->loader.FontSrcInit = common_font_source_init;
    state->loader.FontSrcDestroy = common_font_source_destroy;
    state->loader.FontSrcContainsGlyph = common_font_contains;
    state->loader.FontBakedInit = common_font_baked_init;
    state->loader.FontBakedLoadGlyph = common_font_load_glyph;
    ImFontConfig config;
    config.FontData = const_cast<BenchFont*>(init->font);
    config.FontDataSize = sizeof(BenchFont);
    config.FontDataOwnedByAtlas = false;
    config.FontLoader = &state->loader;
    config.SizePixels = static_cast<float>(init->font->line_height);
    config.PixelSnapH = true;
    config.OversampleH = config.OversampleV = 1;
    std::snprintf(config.Name, sizeof(config.Name), "Shared prebaked Inter 16");
    io.Fonts->Flags |= ImFontAtlasFlags_NoMouseCursors | ImFontAtlasFlags_NoBakedLines;
    io.Fonts->TexDesiredFormat = ImTextureFormat_RGBA32;
    state->font = io.Fonts->AddFont(&config);
    if (!state->font)
    {
        imgui_destroy(state);
        return SDL_SetError("Dear ImGui could not import the shared font"), 0;
    }
    ImFontBaked* baked = state->font->GetFontBaked(16.0f);
    for (int i = 0; i < init->font->glyph_count; ++i)
    {
        const BenchGlyph* original = &init->font->glyphs[i];
        const ImFontGlyph* imported = baked->FindGlyphNoFallback(static_cast<ImWchar>(original->codepoint));
        if (!imported || imported->AdvanceX != original->advance ||
            imported->X0 != original->x_offset || imported->Y0 != original->y_offset ||
            imported->X1 != original->x_offset + original->w ||
            imported->Y1 != original->y_offset + original->h)
        {
            imgui_destroy(state);
            return SDL_SetError("Dear ImGui shared font metric mismatch"), 0;
        }
    }
    io.FontDefault = state->font;
    state->font->Flags |= ImFontFlags_LockBakedSizes;
    ImGui_ImplSDLGPU3_InitInfo backend;
    backend.Device = init->device;
    backend.ColorTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    backend.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&backend))
    {
        imgui_destroy(state);
        return 0;
    }
    state->backend_ready = true;
    std::memcpy(state->document, init->initial_document, std::strlen(init->initial_document) + 1);
    if (init->case_id == BENCH_DOCKS)
    {
        /* New docked windows need a layout/visibility settling frame. Do that
         * during initialization, without a submission or a missing benchmark
         * frame. Other cases do not acquire inactive docking windows/state. */
        BenchScene bootstrap = {};
        bootstrap.case_id = BENCH_DOCKS;
        bootstrap.mouse_x = bootstrap.mouse_y = -100.0f;
        for (int label = 0; label < BENCH_LABELS; ++label)
            std::snprintf(bootstrap.labels[label], sizeof(bootstrap.labels[label]),
                           "Bootstrap panel label %03d", label);
        for (uint32_t frame = 0; frame < 3u; ++frame)
        {
            BenchStats scratch = {};
            bootstrap.frame = frame;
            const int ready = imgui_build(state, &bootstrap, &scratch);
            if (frame == 2u && !ready)
            {
                imgui_destroy(state);
                return SDL_SetError("Dear ImGui docking bootstrap did not settle"), 0;
            }
        }
        SDL_ClearError();
    }
    adapter->state = state;
    adapter->build = imgui_build;
    adapter->upload = imgui_upload;
    adapter->draw = imgui_draw;
    adapter->commit = imgui_commit;
    adapter->inspect = imgui_inspect;
    adapter->destroy = imgui_destroy;
    return 1;
}
