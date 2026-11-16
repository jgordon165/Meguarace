/* An SDL renderer on the Xbox 360's GPU (Xenos), through xsdk, for the .xex.
 *
 * SDL's own code turns copies, fills and clears into triangles
 * (QueueGeometry); this draws them with one shader pair (platform/xenon/gpu:
 * the texel times the vertex colour), so the game's layers, scaling,
 * smoothing and blend modes all run on the GPU instead of the CPU.
 *
 * eDRAM holds what is being drawn: the window (the TV's 1280x720) from tile
 * 0, and a render target texture after it. Leaving a target resolves it into
 * its texture; drawing into one again first draws its texture back. A target
 * too big to sit beside the window takes eDRAM from tile 0, the window
 * resolved into a backup and drawn back after. Present resolves the window
 * into the front buffer the platform shows (xp_gpu_back_buffer).
 *
 * It takes SDL's VITA_GXM driver slot (SDL_render.c lists its drivers by
 * name; SDL_VIDEO_RENDER_VITA_GXM is defined for this build only), so SDL's
 * sources stay as they are. If the GPU won't start, SDL goes on to its
 * software renderer. */
#include "SDL_internal.h"

#if SDL_VIDEO_RENDER_VITA_GXM

#include "render/SDL_sysrender.h"
#include "xenon_platform.h"
#include "../gpu/quad_shaders.h"

#include "xsdk/gpu.h"
#include "xsdk/log.h"
#include "xsdk/shader.h"
#include "xsdk/texture.h"

#define VERTEX_FLOATS 12                 /* x y z w, r g b a, u v, the texture's w h (edge.frag) */
#define VERTEX_BYTES (VERTEX_FLOATS * 4)
/* The vertex ring: a part per frame in flight (the presenter's three
 * buffers), each reused once the GPU has passed that frame. */
#define RING_PARTS 3
#define PART_BYTES (2 * 1024 * 1024)
#define RING_BYTES (RING_PARTS * PART_BYTES)
#define WAIT_MS 2000

typedef struct
{
    XsdkTexture tex;
    int alloc_w, alloc_h;                /* the texture's size, at least the SDL texture's */
    float u_scale, v_scale;              /* SDL's 0..1 to the allocation's */
    Uint32 *shadow;                      /* the texels, for partial updates and locks, made when first needed */
    SDL_bool shadow_valid;               /* the shadow holds what the texture holds */
    SDL_bool target;
    SDL_bool has_content;                /* a target resolved into at least once */
    uint32_t fence;                      /* the last frame's commands that read it */
} XenosTexture;

typedef struct
{
    int out_w, out_h;                    /* the window */
    uint32_t window_tiles;
    XsdkShader vs, ps;
    XsdkShader edge_vs, edge_ps;         /* ANTI-ALIAS: textures at SDL_ScaleModeBest */
    SDL_bool edge_ok;
    SDL_bool edge_on;                    /* the edge pair is the one set */
    XsdkSamplerState point, linear;
    XsdkDepthStencilState depth_off;
    XsdkRasterState raster;
    XsdkTexture white;
    XsdkTexture backup;                  /* the window while a big target has eDRAM */
    SDL_bool backup_ok;
    SDL_bool window_spilled;
    SDL_bool window_drawn;               /* anything drawn since the last present */

    volatile uint32_t *ring;
    uint32_t ring_address;
    uint32_t ring_used;                  /* bytes into the current part */
    int ring_part;
    uint32_t part_fence[RING_PARTS];

    SDL_Texture *target;                 /* NULL: the window */
    SDL_bool bound;                      /* eDRAM set up for the current target since the last resolve */
    SDL_bool target_drawn;
    SDL_bool pending_drawback;
    uint32_t target_tile;

    SDL_Rect viewport;
    SDL_bool clip_on;
    SDL_Rect clip;
    int vsync;
} XenosData;

/* ---- helpers ---------------------------------------------------------- */

static void wait_fence(uint32_t fence)
{
    if (fence && !xsdk_gpu_fence_done(fence)) (void)xsdk_gpu_wait(fence, WAIT_MS);
}

static int round_up(int v, int to) { return (v + to - 1) / to * to; }

static uint32_t blend_factor(SDL_BlendFactor f)
{
    switch (f) {
    case SDL_BLENDFACTOR_ZERO: return XSDK_BLEND_ZERO;
    case SDL_BLENDFACTOR_ONE: return XSDK_BLEND_ONE;
    case SDL_BLENDFACTOR_SRC_COLOR: return XSDK_BLEND_SRC_COLOR;
    case SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR: return XSDK_BLEND_ONE_MINUS_SRC_COLOR;
    case SDL_BLENDFACTOR_SRC_ALPHA: return XSDK_BLEND_SRC_ALPHA;
    case SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA: return XSDK_BLEND_ONE_MINUS_SRC_ALPHA;
    case SDL_BLENDFACTOR_DST_COLOR: return XSDK_BLEND_DST_COLOR;
    case SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR: return XSDK_BLEND_ONE_MINUS_DST_COLOR;
    case SDL_BLENDFACTOR_DST_ALPHA: return XSDK_BLEND_DST_ALPHA;
    case SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA: return XSDK_BLEND_ONE_MINUS_DST_ALPHA;
    default: return 0xFFFFFFFFu;
    }
}

static uint32_t blend_op(SDL_BlendOperation op)
{
    switch (op) {
    case SDL_BLENDOPERATION_ADD: return XSDK_BLEND_OP_ADD;
    case SDL_BLENDOPERATION_SUBTRACT: return XSDK_BLEND_OP_SUBTRACT;
    case SDL_BLENDOPERATION_REV_SUBTRACT: return XSDK_BLEND_OP_REVERSE_SUBTRACT;
    case SDL_BLENDOPERATION_MINIMUM: return XSDK_BLEND_OP_MIN;
    case SDL_BLENDOPERATION_MAXIMUM: return XSDK_BLEND_OP_MAX;
    default: return 0xFFFFFFFFu;
    }
}

static SDL_bool make_blend(SDL_BlendMode mode, XsdkBlendState *out)
{
    XsdkBlend b;
    if (mode == SDL_BLENDMODE_NONE) {
        b.color_source = XSDK_BLEND_ONE; b.color_dest = XSDK_BLEND_ZERO; b.color_op = XSDK_BLEND_OP_ADD;
        b.alpha_source = XSDK_BLEND_ONE; b.alpha_dest = XSDK_BLEND_ZERO; b.alpha_op = XSDK_BLEND_OP_ADD;
    } else {
        b.color_source = blend_factor(SDL_GetBlendModeSrcColorFactor(mode));
        b.color_dest = blend_factor(SDL_GetBlendModeDstColorFactor(mode));
        b.color_op = blend_op(SDL_GetBlendModeColorOperation(mode));
        b.alpha_source = blend_factor(SDL_GetBlendModeSrcAlphaFactor(mode));
        b.alpha_dest = blend_factor(SDL_GetBlendModeDstAlphaFactor(mode));
        b.alpha_op = blend_op(SDL_GetBlendModeAlphaOperation(mode));
        if (b.color_source == 0xFFFFFFFFu || b.color_dest == 0xFFFFFFFFu || b.color_op == 0xFFFFFFFFu ||
            b.alpha_source == 0xFFFFFFFFu || b.alpha_dest == 0xFFFFFFFFu || b.alpha_op == 0xFFFFFFFFu)
            return SDL_FALSE;
    }
    b.write_mask = XSDK_WRITE_ALL;
    if (out) xsdk_gpu_blend_state(out, &b);
    return SDL_TRUE;
}

static void target_size(XenosData *data, int *w, int *h)
{
    if (data->target) {
        XenosTexture *t = (XenosTexture *)data->target->driverdata;
        *w = t->alloc_w;
        *h = t->alloc_h;
    } else {
        *w = data->out_w;
        *h = data->out_h;
    }
}

/* Room in the vertex ring; waits for the GPU and starts over when full. */
static volatile uint32_t *ring_take(XenosData *data, uint32_t bytes, uint32_t *address)
{
    volatile uint32_t *at;
    const uint32_t base = (uint32_t)data->ring_part * PART_BYTES;
    bytes = (bytes + 31) & ~31u;
    if (data->ring_used + bytes > PART_BYTES) {
        wait_fence(xsdk_gpu_fence());
        data->ring_used = 0;
    }
    at = data->ring + (base + data->ring_used) / 4;
    *address = data->ring_address + base + data->ring_used;
    data->ring_used += bytes;
    return at;
}

static float vertex_size_w, vertex_size_h;   /* the texture's size, for the vertices being written */

static void put_vertex(volatile uint32_t *at, float x, float y, const float *c, float u, float v)
{
    union { float f; uint32_t u; } w[VERTEX_FLOATS];
    int i;
    w[0].f = x; w[1].f = y; w[2].f = 0.0f; w[3].f = 1.0f;
    w[4].f = c[0]; w[5].f = c[1]; w[6].f = c[2]; w[7].f = c[3];
    w[8].f = u; w[9].f = v;
    w[10].f = vertex_size_w; w[11].f = vertex_size_h;
    for (i = 0; i < VERTEX_FLOATS; ++i) at[i] = w[i].u;
}

/* Clip space from target pixels, edges half a pixel before the pixels for
 * Direct3D's centres (xsdk's tests do the same). */
static void put_pixel_vertex(volatile uint32_t *at, int tw, int th, float px, float py, const float *c, float u, float v)
{
    put_vertex(at, (px - 0.5f) / ((float)tw * 0.5f) - 1.0f, 1.0f - (py - 0.5f) / ((float)th * 0.5f), c, u, v);
}

static void draw_quad(XenosData *data, const XsdkTexture *tex, const XsdkSamplerState *sampler,
                      float x0, float y0, float x1, float y1, float u1, float v1, const float *c)
{
    int tw, th;
    uint32_t address;
    volatile uint32_t *v;
    if (data->edge_on) {
        xsdk_gpu_set_shaders(&data->vs, &data->ps);
        data->edge_on = SDL_FALSE;
    }
    vertex_size_w = vertex_size_h = 1.0f;
    v = ring_take(data, 6 * VERTEX_BYTES, &address);
    target_size(data, &tw, &th);
    put_pixel_vertex(v + 0 * VERTEX_FLOATS, tw, th, x0, y0, c, 0.0f, 0.0f);
    put_pixel_vertex(v + 1 * VERTEX_FLOATS, tw, th, x1, y0, c, u1, 0.0f);
    put_pixel_vertex(v + 2 * VERTEX_FLOATS, tw, th, x0, y1, c, 0.0f, v1);
    put_pixel_vertex(v + 3 * VERTEX_FLOATS, tw, th, x0, y1, c, 0.0f, v1);
    put_pixel_vertex(v + 4 * VERTEX_FLOATS, tw, th, x1, y0, c, u1, 0.0f);
    put_pixel_vertex(v + 5 * VERTEX_FLOATS, tw, th, x1, y1, c, u1, v1);
    __asm__ volatile("sync" ::: "memory");
    xsdk_gpu_invalidate(address, 6 * VERTEX_BYTES);
    xsdk_gpu_set_vertices(0, address, 6 * VERTEX_BYTES);
    xsdk_gpu_set_texture(0, tex, sampler);
    xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, 6);
}

static void set_scissor_all(XenosData *data)
{
    int tw, th;
    target_size(data, &tw, &th);
    xsdk_gpu_set_scissor(0, 0, (uint32_t)tw, (uint32_t)th);
}

/* Sets eDRAM up for the current target, after a resolve or a change. */
static void bind(XenosData *data)
{
    static const float white[4] = { 1, 1, 1, 1 };
    XsdkBlendState opaque;
    int tw, th;
    if (data->bound) return;
    target_size(data, &tw, &th);
    if (data->target) {
        xsdk_gpu_set_target_at((uint32_t)tw, (uint32_t)th, data->target_tile, data->target_tile);
    } else {
        xsdk_gpu_set_target_at((uint32_t)tw, (uint32_t)th, 0, 0);
    }
    xsdk_gpu_set_shaders(&data->vs, &data->ps);
    data->edge_on = SDL_FALSE;
    xsdk_gpu_set_depth_stencil(&data->depth_off);
    xsdk_gpu_set_raster(&data->raster);
    data->bound = SDL_TRUE;
    make_blend(SDL_BLENDMODE_NONE, &opaque);
    if (data->target && data->pending_drawback) {
        XenosTexture *t = (XenosTexture *)data->target->driverdata;
        xsdk_gpu_set_blend(&opaque);
        set_scissor_all(data);
        draw_quad(data, &t->tex, &data->point, 0, 0, (float)tw, (float)th, 1.0f, 1.0f, white);
    } else if (!data->target && data->window_spilled) {
        xsdk_gpu_set_blend(&opaque);
        set_scissor_all(data);
        draw_quad(data, &data->backup, &data->point, 0, 0, (float)tw, (float)th, 1.0f, 1.0f, white);
    }
    data->pending_drawback = SDL_FALSE;
    data->window_spilled = SDL_FALSE;
}

/* Resolves the current texture target into its texture. */
static void leave_target(XenosData *data)
{
    XenosTexture *t;
    if (!data->target) return;
    t = (XenosTexture *)data->target->driverdata;
    if (data->bound && data->target_drawn) {
        xsdk_gpu_resolve_texture(&t->tex, 0);
        t->has_content = SDL_TRUE;
    }
    data->bound = SDL_FALSE;
    data->target_drawn = SDL_FALSE;
}

/* ---- the renderer's entry points --------------------------------------- */

static void XENOS_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event) { (void)renderer; (void)event; }

static int XENOS_GetOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    XenosData *data = (XenosData *)renderer->driverdata;
    if (w) *w = data->out_w;
    if (h) *h = data->out_h;
    return 0;
}

static SDL_bool XENOS_SupportsBlendMode(SDL_Renderer *renderer, SDL_BlendMode mode)
{
    (void)renderer;
    return make_blend(mode, NULL);
}

static int XENOS_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    XenosData *data = (XenosData *)renderer->driverdata;
    XenosTexture *t;
    const SDL_bool target = texture->access == SDL_TEXTUREACCESS_TARGET;
    (void)data;
    if (texture->format != SDL_PIXELFORMAT_ARGB8888) return SDL_SetError("xenos: ARGB8888 textures only");
    t = (XenosTexture *)SDL_calloc(1, sizeof *t);
    if (!t) return SDL_OutOfMemory();
    t->target = target;
    /* Targets: tiled, as resolves write, at least 32 a side (smaller ones'
     * base level would sit in the packed tail) and a whole 32 wide. */
    t->alloc_w = target ? SDL_max(32, round_up(texture->w, 32)) : texture->w;
    t->alloc_h = target ? SDL_max(32, round_up(texture->h, 8)) : texture->h;
    if (target && xsdk_gpu_color_target_tiles((uint32_t)t->alloc_w, (uint32_t)t->alloc_h, XSDK_TARGET_8_8_8_8) >
                      XSDK_EDRAM_TILES) {
        SDL_free(t);
        return SDL_SetError("xenos: render target %dx%d is bigger than eDRAM", texture->w, texture->h);
    }
    if (!NT_SUCCESS(xsdk_texture_create(&t->tex, XSDK_FORMAT_A8R8G8B8, (uint32_t)t->alloc_w, (uint32_t)t->alloc_h, 1,
                                        target ? true : false))) {
        SDL_free(t);
        return SDL_SetError("xenos: no GPU memory for a %dx%d texture", texture->w, texture->h);
    }
    t->u_scale = (float)texture->w / (float)t->alloc_w;
    t->v_scale = (float)texture->h / (float)t->alloc_h;
    if (!target) {
        /* Starts transparent black, as SDL's do. */
        Uint32 *row = (Uint32 *)SDL_calloc((size_t)texture->w, 4);
        int y;
        if (!row) {
            xsdk_texture_free(&t->tex);
            SDL_free(t);
            return SDL_OutOfMemory();
        }
        for (y = 0; y < texture->h; ++y)
            xsdk_texture_upload_rows(&t->tex, 0, (uint32_t)y, 1, row, (uint32_t)texture->w * 4);
        SDL_free(row);
        xsdk_gpu_invalidate(t->tex.address, t->tex.size_bytes);
    }
    texture->driverdata = t;
    return 0;
}

/* The shadow, for partial updates and locks, filled from the texture if it
 * went out of date (slow, but the game updates whole textures). */
static SDL_bool need_shadow(XenosTexture *t, SDL_Texture *texture)
{
    if (!t->shadow) {
        t->shadow = (Uint32 *)SDL_calloc((size_t)texture->w * (size_t)texture->h, 4);
        if (!t->shadow) return SDL_FALSE;
        t->shadow_valid = SDL_FALSE;
    }
    if (!t->shadow_valid) {
        wait_fence(t->fence);
        xsdk_texture_download_rows(&t->tex, 0, 0, (uint32_t)texture->h, t->shadow, (uint32_t)texture->w * 4);
        t->shadow_valid = SDL_TRUE;
    }
    return SDL_TRUE;
}

static void upload_from(XenosTexture *t, int first, int count, const void *rows, int pitch)
{
    wait_fence(t->fence);
    xsdk_texture_upload_rows(&t->tex, 0, (uint32_t)first, (uint32_t)count, rows, (uint32_t)pitch);
    xsdk_gpu_invalidate(t->tex.address, t->tex.size_bytes);
}

static int XENOS_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *rect,
                               const void *pixels, int pitch)
{
    XenosTexture *t = (XenosTexture *)texture->driverdata;
    int y;
    (void)renderer;
    if (t->target) return SDL_SetError("xenos: can't update a render target");
    if (rect->x == 0 && rect->w == texture->w && (pitch & 3) == 0) {
        /* Whole rows: straight to the GPU's copy, no shadow. */
        upload_from(t, rect->y, rect->h, pixels, pitch);
        t->shadow_valid = SDL_FALSE;
        return 0;
    }
    if (!need_shadow(t, texture)) return SDL_OutOfMemory();
    for (y = 0; y < rect->h; ++y)
        SDL_memcpy(t->shadow + (size_t)(rect->y + y) * (size_t)texture->w + rect->x,
                   (const Uint8 *)pixels + (size_t)y * (size_t)pitch, (size_t)rect->w * 4);
    upload_from(t, rect->y, rect->h, t->shadow + (size_t)rect->y * (size_t)texture->w, texture->w * 4);
    return 0;
}

static int XENOS_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *rect,
                             void **pixels, int *pitch)
{
    XenosTexture *t = (XenosTexture *)texture->driverdata;
    (void)renderer;
    if (t->target) return SDL_SetError("xenos: can't lock a render target");
    if (!need_shadow(t, texture)) return SDL_OutOfMemory();
    *pixels = t->shadow + (size_t)rect->y * (size_t)texture->w + rect->x;
    *pitch = texture->w * 4;
    return 0;
}

static void XENOS_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    XenosTexture *t = (XenosTexture *)texture->driverdata;
    const int first = texture->locked_rect.y, count = texture->locked_rect.h;
    (void)renderer;
    if (t && t->shadow)
        upload_from(t, first, count, t->shadow + (size_t)first * (size_t)texture->w, texture->w * 4);
}

static void XENOS_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture, SDL_ScaleMode mode)
{
    (void)renderer; (void)texture; (void)mode;   /* read from texture->scaleMode at each draw */
}

static int XENOS_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    XenosData *data = (XenosData *)renderer->driverdata;
    XenosTexture *t = texture ? (XenosTexture *)texture->driverdata : NULL;
    leave_target(data);
    data->bound = SDL_FALSE;
    data->target = texture;
    if (!t) return 0;
    {
        const uint32_t tiles = xsdk_gpu_color_target_tiles((uint32_t)t->alloc_w, (uint32_t)t->alloc_h, XSDK_TARGET_8_8_8_8);
        if (data->window_tiles + tiles <= XSDK_EDRAM_TILES) {
            data->target_tile = data->window_tiles;
        } else {
            /* Too big to sit beside the window: save the window's pixels. */
            if (data->window_drawn && !data->window_spilled) {
                if (!data->backup_ok) {
                    data->target = NULL;
                    return SDL_SetError("xenos: no room in eDRAM for a %dx%d target", texture->w, texture->h);
                }
                data->target = NULL;
                bind(data);
                xsdk_gpu_resolve_texture(&data->backup, 0);
                data->bound = SDL_FALSE;
                data->window_spilled = SDL_TRUE;
                data->target = texture;
            }
            data->target_tile = 0;
        }
    }
    data->pending_drawback = t->has_content;
    return 0;
}

static int XENOS_QueueNoOp(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    (void)renderer; (void)cmd;
    return 0;
}

/* SDL's vertex data for a command: x, y, r, g, b, a (0-1), u, v. */
#define QV_FLOATS 8

static int XENOS_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    float *v = (float *)SDL_AllocateRenderVertices(renderer, (size_t)count * 6 * QV_FLOATS * sizeof(float), 4,
                                                   &cmd->data.draw.first);
    const float r = cmd->data.draw.r / 255.0f, g = cmd->data.draw.g / 255.0f, b = cmd->data.draw.b / 255.0f,
                a = cmd->data.draw.a / 255.0f;
    int i, k;
    if (!v) return -1;
    cmd->data.draw.count = (size_t)count * 6;
    for (i = 0; i < count; ++i) {
        const float x0 = points[i].x, y0 = points[i].y, x1 = x0 + 1.0f, y1 = y0 + 1.0f;
        const float xy[6][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x0, y1 }, { x1, y0 }, { x1, y1 } };
        for (k = 0; k < 6; ++k, v += QV_FLOATS) {
            v[0] = xy[k][0]; v[1] = xy[k][1];
            v[2] = r; v[3] = g; v[4] = b; v[5] = a;
            v[6] = 0.0f; v[7] = 0.0f;
        }
    }
    return 0;
}

/* Lines as thin quads along each segment (the game draws none). */
static int XENOS_QueueDrawLines(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    const int segments = count > 1 ? count - 1 : 0;
    float *v = (float *)SDL_AllocateRenderVertices(renderer, (size_t)(segments ? segments : 1) * 6 * QV_FLOATS * sizeof(float),
                                                   4, &cmd->data.draw.first);
    const float r = cmd->data.draw.r / 255.0f, g = cmd->data.draw.g / 255.0f, b = cmd->data.draw.b / 255.0f,
                a = cmd->data.draw.a / 255.0f;
    int i, k;
    if (!v) return -1;
    cmd->data.draw.count = (size_t)segments * 6;
    for (i = 0; i < segments; ++i) {
        const float ax = points[i].x + 0.5f, ay = points[i].y + 0.5f;
        const float bx = points[i + 1].x + 0.5f, by = points[i + 1].y + 0.5f;
        float dx = bx - ax, dy = by - ay, len = SDL_sqrtf(dx * dx + dy * dy);
        float nx, ny;
        if (len < 0.001f) { dx = 1.0f; dy = 0.0f; len = 1.0f; }
        nx = -dy / len * 0.5f; ny = dx / len * 0.5f;
        {
            const float xy[6][2] = { { ax + nx, ay + ny }, { bx + nx, by + ny }, { ax - nx, ay - ny },
                                     { ax - nx, ay - ny }, { bx + nx, by + ny }, { bx - nx, by - ny } };
            for (k = 0; k < 6; ++k, v += QV_FLOATS) {
                v[0] = xy[k][0]; v[1] = xy[k][1];
                v[2] = r; v[3] = g; v[4] = b; v[5] = a;
                v[6] = 0.0f; v[7] = 0.0f;
            }
        }
    }
    return 0;
}

static int XENOS_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                               const float *xy, int xy_stride, const SDL_Color *color, int color_stride,
                               const float *uv, int uv_stride, int num_vertices, const void *indices,
                               int num_indices, int size_indices, float scale_x, float scale_y)
{
    const int count = indices ? num_indices : num_vertices;
    XenosTexture *t = texture ? (XenosTexture *)texture->driverdata : NULL;
    float *v = (float *)SDL_AllocateRenderVertices(renderer, (size_t)count * QV_FLOATS * sizeof(float), 4,
                                                   &cmd->data.draw.first);
    int i;
    if (!v) return -1;
    cmd->data.draw.count = (size_t)count;
    size_indices = indices ? size_indices : 0;
    for (i = 0; i < count; ++i, v += QV_FLOATS) {
        int j;
        const float *p;
        SDL_Color c;
        if (size_indices == 4) j = (int)((const Uint32 *)indices)[i];
        else if (size_indices == 2) j = ((const Uint16 *)indices)[i];
        else if (size_indices == 1) j = ((const Uint8 *)indices)[i];
        else j = i;
        p = (const float *)((const char *)xy + j * xy_stride);
        c = *(const SDL_Color *)((const char *)color + j * color_stride);
        v[0] = p[0] * scale_x;
        v[1] = p[1] * scale_y;
        v[2] = c.r / 255.0f; v[3] = c.g / 255.0f; v[4] = c.b / 255.0f; v[5] = c.a / 255.0f;
        if (t) {
            const float *q = (const float *)((const char *)uv + j * uv_stride);
            v[6] = q[0] * t->u_scale;
            v[7] = q[1] * t->v_scale;
        } else {
            v[6] = 0.0f; v[7] = 0.0f;
        }
    }
    return 0;
}

static void apply_scissor(XenosData *data)
{
    int tw, th, x0, y0, x1, y1;
    target_size(data, &tw, &th);
    x0 = data->viewport.x; y0 = data->viewport.y;
    x1 = x0 + data->viewport.w; y1 = y0 + data->viewport.h;
    if (data->clip_on) {
        x0 = SDL_max(x0, data->viewport.x + data->clip.x);
        y0 = SDL_max(y0, data->viewport.y + data->clip.y);
        x1 = SDL_min(x1, data->viewport.x + data->clip.x + data->clip.w);
        y1 = SDL_min(y1, data->viewport.y + data->clip.y + data->clip.h);
    }
    x0 = SDL_max(x0, 0); y0 = SDL_max(y0, 0);
    x1 = SDL_min(x1, tw); y1 = SDL_min(y1, th);
    if (x1 <= x0 || y1 <= y0) { x0 = y0 = 0; x1 = y1 = 0; }
    xsdk_gpu_set_scissor((uint32_t)x0, (uint32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0));
}

static void mark_drawn(XenosData *data)
{
    if (data->target) data->target_drawn = SDL_TRUE;
    else data->window_drawn = SDL_TRUE;
}

static void run_triangles(XenosData *data, const SDL_RenderCommand *cmd, const void *vertices, SDL_Texture *texture,
                          SDL_Texture **used, int *nused)
{
    const float *src = (const float *)((const char *)vertices + cmd->data.draw.first);
    const uint32_t count = (uint32_t)cmd->data.draw.count;
    XenosTexture *t = texture ? (XenosTexture *)texture->driverdata : NULL;
    XsdkBlendState blend;
    uint32_t address, i;
    volatile uint32_t *dst;
    int tw, th;
    if (!count) return;
    const SDL_bool edge = t && data->edge_ok && texture->scaleMode == SDL_ScaleModeBest;
    if (!make_blend(cmd->data.draw.blend, &blend)) make_blend(SDL_BLENDMODE_BLEND, &blend);
    bind(data);
    target_size(data, &tw, &th);
    apply_scissor(data);
    xsdk_gpu_set_blend(&blend);
    /* ANTI-ALIAS (SDL_ScaleModeBest): the edge-smoothing pair, its literals
     * uploaded; the quad pair back for everything else. */
    if (edge && !data->edge_on) {
        int k;
        xsdk_gpu_set_shaders(&data->edge_vs, &data->edge_ps);
        for (k = 0; k < EDGE_PIXEL_LITERALS; ++k)
            xsdk_gpu_set_pixel_constants((uint32_t)edge_pixel_literals[k][0], &edge_pixel_literals[k][1], 1);
        data->edge_on = SDL_TRUE;
    } else if (!edge && data->edge_on) {
        xsdk_gpu_set_shaders(&data->vs, &data->ps);
        data->edge_on = SDL_FALSE;
    }
    vertex_size_w = t ? (float)t->alloc_w : 1.0f;
    vertex_size_h = t ? (float)t->alloc_h : 1.0f;
    dst = ring_take(data, count * VERTEX_BYTES, &address);
    for (i = 0; i < count; ++i, src += QV_FLOATS, dst += VERTEX_FLOATS)
        put_pixel_vertex(dst, tw, th, (float)data->viewport.x + src[0], (float)data->viewport.y + src[1], src + 2,
                         src[6], src[7]);
    __asm__ volatile("sync" ::: "memory");
    xsdk_gpu_invalidate(address, count * VERTEX_BYTES);
    xsdk_gpu_set_vertices(0, address, count * VERTEX_BYTES);
    xsdk_gpu_set_texture(0, t ? &t->tex : &data->white,
                         t && !edge && texture->scaleMode != SDL_ScaleModeNearest ? &data->linear : &data->point);
    xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, count);
    mark_drawn(data);
    if (texture && *nused < 64) used[(*nused)++] = texture;
}

static int XENOS_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
    XenosData *data = (XenosData *)renderer->driverdata;
    SDL_Texture *used[64];
    int nused = 0, i;
    (void)vertsize;
    for (; cmd; cmd = cmd->next) {
        switch (cmd->command) {
        case SDL_RENDERCMD_SETVIEWPORT:
            data->viewport = cmd->data.viewport.rect;
            break;
        case SDL_RENDERCMD_SETCLIPRECT:
            data->clip_on = cmd->data.cliprect.enabled;
            data->clip = cmd->data.cliprect.rect;
            break;
        case SDL_RENDERCMD_SETDRAWCOLOR:
            break;
        case SDL_RENDERCMD_CLEAR: {
            const float c[4] = { cmd->data.color.r / 255.0f, cmd->data.color.g / 255.0f, cmd->data.color.b / 255.0f,
                                 cmd->data.color.a / 255.0f };
            XsdkBlendState opaque;
            int tw, th;
            bind(data);
            target_size(data, &tw, &th);
            make_blend(SDL_BLENDMODE_NONE, &opaque);
            xsdk_gpu_set_blend(&opaque);
            set_scissor_all(data);
            draw_quad(data, &data->white, &data->point, 0, 0, (float)tw, (float)th, 1.0f, 1.0f, c);
            mark_drawn(data);
            break;
        }
        case SDL_RENDERCMD_DRAW_POINTS:
        case SDL_RENDERCMD_DRAW_LINES:
            run_triangles(data, cmd, vertices, NULL, used, &nused);
            break;
        case SDL_RENDERCMD_GEOMETRY:
            run_triangles(data, cmd, vertices, cmd->data.draw.texture, used, &nused);
            break;
        case SDL_RENDERCMD_FILL_RECTS:   /* SDL makes these geometry */
        case SDL_RENDERCMD_COPY:
        case SDL_RENDERCMD_COPY_EX:
        case SDL_RENDERCMD_NO_OP:
            break;
        }
    }
    if (nused) {
        const uint32_t fence = xsdk_gpu_fence();
        for (i = 0; i < nused; ++i) ((XenosTexture *)used[i]->driverdata)->fence = fence;
    }
    return 0;
}

static int XENOS_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect, Uint32 format, void *pixels, int pitch)
{
    (void)renderer; (void)rect; (void)format; (void)pixels; (void)pitch;
    return SDL_Unsupported();
}

static int XENOS_RenderPresent(SDL_Renderer *renderer)
{
    XenosData *data = (XenosData *)renderer->driverdata;
    SDL_Texture *was = data->target;
    uint32_t address;
    if (was) XENOS_SetRenderTarget(renderer, NULL);
    bind(data);
    /* Waits for a buffer off screen (the TV's 60 Hz paces the game), then
     * queues the frame to show at a vblank once the GPU has drawn it; the
     * CPU goes on to the next frame meanwhile. */
    address = xp_gpu_begin_frame();
    if (address) {
        xsdk_gpu_resolve(address, (uint32_t)data->out_w, (uint32_t)data->out_h, 0);
        data->bound = SDL_FALSE;
        data->part_fence[data->ring_part] = xsdk_gpu_fence();
        xp_gpu_end_frame();
    } else {
        data->part_fence[data->ring_part] = xsdk_gpu_fence();
    }
    data->ring_part = (data->ring_part + 1) % RING_PARTS;
    wait_fence(data->part_fence[data->ring_part]);
    data->ring_used = 0;
    data->window_drawn = SDL_FALSE;
    if (was) XENOS_SetRenderTarget(renderer, was);
    return 0;
}

static void XENOS_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    XenosData *data = (XenosData *)renderer->driverdata;
    XenosTexture *t = (XenosTexture *)texture->driverdata;
    if (!t) return;
    if (data && data->target == texture) {
        data->target = NULL;
        data->bound = SDL_FALSE;
    }
    wait_fence(xsdk_gpu_fence());
    xsdk_texture_free(&t->tex);
    SDL_free(t->shadow);
    SDL_free(t);
    texture->driverdata = NULL;
}

static void XENOS_DestroyRenderer(SDL_Renderer *renderer)
{
    XenosData *data = (XenosData *)renderer->driverdata;
    if (data) {
        wait_fence(xsdk_gpu_fence());
        xsdk_texture_free(&data->white);
        if (data->backup_ok) xsdk_texture_free(&data->backup);
        if (data->ring) xsdk_gpu_free(data->ring);
        xsdk_shader_free(&data->vs);
        xsdk_shader_free(&data->ps);
        if (data->edge_ok) {
            xsdk_shader_free(&data->edge_vs);
            xsdk_shader_free(&data->edge_ps);
        }
        SDL_free(data);
    }
    SDL_free(renderer);
}

static int XENOS_SetVSync(SDL_Renderer *renderer, int vsync)
{
    ((XenosData *)renderer->driverdata)->vsync = vsync;
    return 0;
}

static SDL_Renderer *XENOS_CreateRenderer(SDL_Window *window, Uint32 flags)
{
    static const XsdkVertexElement layout[] = {
        { .usage = XSDK_USAGE_POSITION, .usage_index = 0, .format = XSDK_VERTEX_FLOAT4, .offset_bytes = 0 },
        { .usage = XSDK_USAGE_COLOR, .usage_index = 0, .format = XSDK_VERTEX_FLOAT4, .offset_bytes = 16 },
        { .usage = XSDK_USAGE_TEXCOORD, .usage_index = 0, .format = XSDK_VERTEX_FLOAT2, .offset_bytes = 32 },
        { .usage = XSDK_USAGE_TEXCOORD, .usage_index = 1, .format = XSDK_VERTEX_FLOAT2, .offset_bytes = 40 },
    };
    const uint32_t stride = VERTEX_BYTES;
    const XsdkSampler point = { XSDK_FILTER_POINT, XSDK_FILTER_POINT, XSDK_FILTER_POINT, XSDK_ADDRESS_CLAMP,
                                XSDK_ADDRESS_CLAMP, 0.0f, 0, 0, 0, XSDK_ADDRESS_CLAMP, XSDK_BORDER_BLACK };
    const XsdkSampler linear = { XSDK_FILTER_LINEAR, XSDK_FILTER_LINEAR, XSDK_FILTER_POINT, XSDK_ADDRESS_CLAMP,
                                 XSDK_ADDRESS_CLAMP, 0.0f, 0, 0, 0, XSDK_ADDRESS_CLAMP, XSDK_BORDER_BLACK };
    const XsdkDepthStencil off = { .depth_compare = XSDK_COMPARE_ALWAYS, .stencil_compare = XSDK_COMPARE_ALWAYS };
    const XsdkRaster raster = { .cull = XSDK_CULL_NONE, .fill = XSDK_FILL_SOLID };
    uint32_t whites[32 * 32];
    SDL_Renderer *renderer;
    XenosData *data;
    int i;

    if (!xp_gpu_ready()) {
        SDL_SetError("xenos: the GPU didn't start");
        return NULL;
    }
    renderer = (SDL_Renderer *)SDL_calloc(1, sizeof *renderer);
    data = (XenosData *)SDL_calloc(1, sizeof *data);
    if (!renderer || !data) {
        SDL_free(renderer);
        SDL_free(data);
        SDL_OutOfMemory();
        return NULL;
    }
    xp_display_size(&data->out_w, &data->out_h);
    data->window_tiles = xsdk_gpu_color_target_tiles((uint32_t)data->out_w, (uint32_t)data->out_h, XSDK_TARGET_8_8_8_8);
    if (!NT_SUCCESS(xsdk_shader_load(&data->vs, quad_vertex_shader, sizeof quad_vertex_shader)) ||
        !NT_SUCCESS(xsdk_shader_set_vertex_layout(&data->vs, layout, 4, &stride)) ||
        !NT_SUCCESS(xsdk_shader_load(&data->ps, quad_pixel_shader, sizeof quad_pixel_shader))) {
        xsdk_log_write("xenos: the shaders didn't load");
        SDL_free(data);
        SDL_free(renderer);
        SDL_SetError("xenos: shaders");
        return NULL;
    }
    /* ANTI-ALIAS's pair; without it, those textures are drawn linear. */
    data->edge_ok = NT_SUCCESS(xsdk_shader_load(&data->edge_vs, edge_vertex_shader, sizeof edge_vertex_shader)) &&
                    NT_SUCCESS(xsdk_shader_set_vertex_layout(&data->edge_vs, layout, 4, &stride)) &&
                    NT_SUCCESS(xsdk_shader_load(&data->edge_ps, edge_pixel_shader, sizeof edge_pixel_shader));
    if (!data->edge_ok) xsdk_log_write("xenos: the anti-alias shaders didn't load; drawing those linear");
    xsdk_texture_sampler_state(&data->point, &point);
    xsdk_texture_sampler_state(&data->linear, &linear);
    xsdk_gpu_depth_stencil_state(&data->depth_off, &off);
    xsdk_gpu_raster_state(&data->raster, &raster);
    for (i = 0; i < 32 * 32; ++i) whites[i] = 0xFFFFFFFFu;
    data->ring = xsdk_gpu_alloc(RING_BYTES);
    if (!data->ring || !NT_SUCCESS(xsdk_texture_create(&data->white, XSDK_FORMAT_A8R8G8B8, 32, 32, 1, false))) {
        xsdk_log_write("xenos: no GPU memory");
        if (data->ring) xsdk_gpu_free(data->ring);
        xsdk_shader_free(&data->vs);
        xsdk_shader_free(&data->ps);
        SDL_free(data);
        SDL_free(renderer);
        SDL_OutOfMemory();
        return NULL;
    }
    data->ring_address = xsdk_gpu_address(data->ring);
    xsdk_texture_upload(&data->white, 0, whites, 32 * 4);
    xsdk_gpu_invalidate(data->white.address, data->white.size_bytes);
    data->backup_ok = NT_SUCCESS(xsdk_texture_create(&data->backup, XSDK_FORMAT_A8R8G8B8, (uint32_t)data->out_w,
                                                     (uint32_t)data->out_h, 1, true));
    xsdk_gpu_set_resolve_clears(false, false);
    data->viewport.w = data->out_w;
    data->viewport.h = data->out_h;
    data->vsync = (flags & SDL_RENDERER_PRESENTVSYNC) ? 1 : 0;

    renderer->WindowEvent = XENOS_WindowEvent;
    renderer->GetOutputSize = XENOS_GetOutputSize;
    renderer->SupportsBlendMode = XENOS_SupportsBlendMode;
    renderer->CreateTexture = XENOS_CreateTexture;
    renderer->UpdateTexture = XENOS_UpdateTexture;
    renderer->LockTexture = XENOS_LockTexture;
    renderer->UnlockTexture = XENOS_UnlockTexture;
    renderer->SetTextureScaleMode = XENOS_SetTextureScaleMode;
    renderer->SetRenderTarget = XENOS_SetRenderTarget;
    renderer->QueueSetViewport = XENOS_QueueNoOp;
    renderer->QueueSetDrawColor = XENOS_QueueNoOp;
    renderer->QueueDrawPoints = XENOS_QueueDrawPoints;
    renderer->QueueDrawLines = XENOS_QueueDrawLines;
    renderer->QueueGeometry = XENOS_QueueGeometry;
    renderer->RunCommandQueue = XENOS_RunCommandQueue;
    renderer->RenderReadPixels = XENOS_RenderReadPixels;
    renderer->RenderPresent = XENOS_RenderPresent;
    renderer->DestroyTexture = XENOS_DestroyTexture;
    renderer->DestroyRenderer = XENOS_DestroyRenderer;
    renderer->SetVSync = XENOS_SetVSync;
    renderer->info = VITA_GXM_RenderDriver.info;
    renderer->driverdata = data;
    renderer->window = window;
    xsdk_log_write("xenos: GPU renderer up, %dx%d", data->out_w, data->out_h);
    return renderer;
}

SDL_RenderDriver VITA_GXM_RenderDriver = {
    XENOS_CreateRenderer,
    {
        "xenos",
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE,
        1,
        { SDL_PIXELFORMAT_ARGB8888 },
        4096,
        4096,
    }
};

#endif /* SDL_VIDEO_RENDER_VITA_GXM */
