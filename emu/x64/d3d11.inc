/* d3d11.inc — Direct3D 11 / DXGI front end (the "DXVK direction" for the browser: D3D11 calls → host graphics).
 *
 *  Stage 1 (this file): COM plumbing + swap chain + render-target clears, presented through the window framebuffer
 *  (WebGL2 canvas in Nebula OS, PPM frame dumps in the native runner).  Every interface vtable is built from the real
 *  method order of d3d11.h / dxgi.h (emu/x64/d3d_vtables.h); each slot is a named shim ("ID3D11DeviceContext::DrawIndexed"),
 *  so a method that is not implemented yet stops the program with its exact name instead of crashing or silently lying.
 *
 *  COM object in guest memory: [0] vtable pointer, [8] object id (index into g_dobj), [12] reference count.
 *  Objects created by the device (buffers, shaders, states, layouts, views) keep their descriptions / bytecode so that
 *  stage 2 (DXBC → GLSL translation + WebGL2 draw calls) can be layered on top without changing the ABI. */

#include "d3d_vtables.h"

enum { DK_DEVICE = 1, DK_CONTEXT, DK_SWAPCHAIN, DK_TEX2D, DK_RTV, DK_DSV, DK_BUFFER, DK_SHADER, DK_STATE, DK_LAYOUT, DK_SRV, DK_SAMPLER, DK_QUERY, DK_FACTORY, DK_ADAPTER, DK_DXGIDEV, DK_OUTPUT };
#define DOBJ_MAX 2048
typedef struct {
  int used, kind, iface; u64 addr; Win32 *w;
  int w_, h_, fmt, mips, bind, usage;          /* textures */
  u32 *px;                                     /* texture pixels (BGRA, host memory) — stage 1 keeps render targets in software */
  int res;                                     /* views: id of the resource */
  u64 data; u32 size; int extra;               /* buffers / shaders: guest copy of the initial data or bytecode */
  int sc_bb, sc_w, sc_h; u64 sc_hwnd;          /* swap chain: back buffer texture, size, window */
  float clear_depth; u32 flags;
} DObj;
static DObj g_dobj[DOBJ_MAX];
static u64 g_com_vt[IF_COUNT];                 /* guest vtables, built lazily per Win32 */
static Win32 *g_com_vt_owner;

static u64 com_vtable(Cpu *c, int iface) {
  Win32 *w = W; if (g_com_vt_owner != w) { e_memset(g_com_vt, 0, sizeof g_com_vt); g_com_vt_owner = w; }
  if (g_com_vt[iface]) return g_com_vt[iface];
  const ComIface *f = &g_com_ifaces[iface]; u64 vt = w32_heap_alloc(w, (u64)f->n * 8, 1); if (!vt) return 0;
  for (int i = 0; i < f->n; i++) { char nm[96]; e_snprintf(nm, sizeof nm, "%s::%s", f->name, f->methods[i]); wr64(c, vt + 8 * i, w32_shim_slot(w, "d3d11", nm)); }
  g_com_vt[iface] = vt; return vt;
}
static int dobj_new(Cpu *c, int kind, int iface, u64 *addr_out) {
  int id; for (id = 1; id < DOBJ_MAX && g_dobj[id].used; id++) { } if (id == DOBJ_MAX) return -1;
  u64 vt = com_vtable(c, iface); u64 a = w32_heap_alloc(W, 32, 1); if (!vt || !a) return -1;
  wr64(c, a, vt); wr32(c, a + 8, (u32)id); wr32(c, a + 12, 1);
  e_memset(&g_dobj[id], 0, sizeof(DObj)); g_dobj[id].used = 1; g_dobj[id].kind = kind; g_dobj[id].iface = iface; g_dobj[id].addr = a; g_dobj[id].w = W;
  *addr_out = a; return id;
}
static DObj *dobj_of(Cpu *c, u64 self, int kind) {
  if (!self || !mem_ptr(c->mem, self, 16, 0)) return 0; u32 id = rd32(c, self + 8); if (id >= DOBJ_MAX || !g_dobj[id].used || g_dobj[id].addr != self) return 0;
  if (kind && g_dobj[id].kind != kind) return 0; return &g_dobj[id];
}
static int dobj_id(DObj *o) { return (int)(o - g_dobj); }
static void dobj_free(DObj *o) { if (o->px) { emu_free(o->px); o->px = 0; } o->used = 0;
  if (o->kind == DK_SWAPCHAIN && o->sc_bb > 0 && g_dobj[o->sc_bb].used) { DObj *t = &g_dobj[o->sc_bb]; u32 r = rd32(o->w->cpu, t->addr + 12); if (r) r--; wr32(o->w->cpu, t->addr + 12, r); if (!r) dobj_free(t); } }
static void d3d_log(const char *fmt, ...) { char m[300]; __builtin_va_list ap; __builtin_va_start(ap, fmt); e_vsnprintf(m, sizeof m, fmt, ap); __builtin_va_end(ap); host_write(3, m, e_strlen(m)); }

#define HR_OK 0u
#define E_INVALIDARG_ 0x80070057u
#define E_NOINTERFACE_ 0x80004002u
#define E_OUTOFMEMORY_ 0x8007000Eu
#define DXGI_ERROR_INVALID_CALL_ 0x887A0001u

/* ---- IUnknown for every object ---- */
static void com_QueryInterface(Cpu *c) {
  DObj *o = dobj_of(c, ARG(0), 0); u64 riid = ARG(1), out = ARG(2); if (!o || !out) { RET(E_INVALIDARG_); return; }
  u32 d1 = rd32(c, riid);   /* first GUID dword is enough to tell the interfaces we hand out apart */
  int same = d1 == 0x00000000u;   /* IUnknown */
  /* ID3D11Device {db6f6ddb}, ID3D11DeviceContext {c0bfa96c}, IDXGISwapChain {310d36a0}, ID3D11Texture2D {6f15aaf2}, IDXGIDevice {54ec77fa}, IDXGIObject {aec22fb8}, ID3D11DeviceChild {1841e5c8} */
  switch (o->kind) {
    case DK_DEVICE: same |= d1 == 0xdb6f6ddbu; break;
    case DK_CONTEXT: same |= d1 == 0xc0bfa96cu || d1 == 0x1841e5c8u; break;
    case DK_SWAPCHAIN: same |= d1 == 0x310d36a0u || d1 == 0xaec22fb8u; break;
    case DK_TEX2D: same |= d1 == 0x6f15aaf2u || d1 == 0x1841e5c8u || d1 == 0xdc8e63f3u /* ID3D11Resource */; break;
    default: break;
  }
  if (!same) { wr64(c, out, 0); d3d_log("[d3d11] QueryInterface on %s for {%08x-…}: E_NOINTERFACE\n", g_com_ifaces[o->iface].name, d1); RET(E_NOINTERFACE_); return; }
  wr32(c, o->addr + 12, rd32(c, o->addr + 12) + 1); wr64(c, out, o->addr); RET(HR_OK);
}
static void com_AddRef(Cpu *c) { DObj *o = dobj_of(c, ARG(0), 0); if (!o) { RET(0); return; } u32 r = rd32(c, o->addr + 12) + 1; wr32(c, o->addr + 12, r); RET(r); }
static void com_Release(Cpu *c) { DObj *o = dobj_of(c, ARG(0), 0); if (!o) { RET(0); return; } u32 r = rd32(c, o->addr + 12); if (r) r--; wr32(c, o->addr + 12, r);
  if (!r && o->kind != DK_DEVICE && o->kind != DK_CONTEXT) dobj_free(o);   /* device + immediate context live until process exit (apps often over-release them) */
  RET(r); }
static void com_GetDevice(Cpu *c) { /* ID3D11DeviceChild::GetDevice(this, ID3D11Device**) */ u64 out = ARG(1); if (!out) return; for (int i = 1; i < DOBJ_MAX; i++) if (g_dobj[i].used && g_dobj[i].kind == DK_DEVICE && g_dobj[i].w == W) { wr32(c, g_dobj[i].addr + 12, rd32(c, g_dobj[i].addr + 12) + 1); wr64(c, out, g_dobj[i].addr); return; } wr64(c, out, 0); }
static void com_SetPrivateData(Cpu *c) { RET(HR_OK); }   /* debug names */

/* ---- textures / views ---- */
static int fmt_bpp(int fmt) { switch (fmt) { case 28: case 29: case 87: case 88: case 91: case 24: case 10: return 4; case 2: return 16; case 6: return 12; case 45: case 40: case 20: return 4; case 56: case 54: return 2; case 61: case 65: return 1; default: return 4; } }
static int tex_create(Cpu *c, int w_, int h_, int fmt, int bind, u64 *addr) {
  u64 a; int id = dobj_new(c, DK_TEX2D, IF_ID3D11TEXTURE2D, &a); if (id < 0) return -1; DObj *o = &g_dobj[id];
  o->w_ = w_; o->h_ = h_; o->fmt = fmt; o->bind = bind; o->mips = 1;
  if (bind & 0x20) { o->px = emu_alloc((size_t)w_ * h_ * 4); if (!o->px) { dobj_free(o); return -1; } e_memset(o->px, 0, (size_t)w_ * h_ * 4); }   /* D3D11_BIND_RENDER_TARGET: software surface */
  *addr = a; return id;
}
/* D3D11_TEXTURE2D_DESC: Width@0 Height@4 MipLevels@8 ArraySize@12 Format@16 SampleDesc{Count@20,Quality@24} Usage@28 BindFlags@32 CPUAccessFlags@36 MiscFlags@40 (44 B) */
static void dev_CreateTexture2D(Cpu *c) {
  u64 desc = ARG(1), init = ARG(2), out = ARG(3); if (!desc) { RET(E_INVALIDARG_); return; }
  int w_ = (int)rd32(c, desc), h_ = (int)rd32(c, desc + 4), fmt = (int)rd32(c, desc + 16), bind = (int)rd32(c, desc + 32);
  if (!out) { RET(1); return; }   /* S_FALSE: validation-only call */
  u64 a; int id = tex_create(c, w_, h_, fmt, bind, &a); if (id < 0) { RET(E_OUTOFMEMORY_); return; }
  g_dobj[id].mips = (int)rd32(c, desc + 8); g_dobj[id].usage = (int)rd32(c, desc + 28);
  if (init) { /* D3D11_SUBRESOURCE_DATA: pSysMem@0 SysMemPitch@8 */ u64 src = rd64(c, init); u32 pitch = rd32(c, init + 8); u32 bpp = (u32)fmt_bpp(fmt); u64 n = (u64)h_ * (pitch ? pitch : (u32)w_ * bpp);
    u64 g = w32_heap_alloc(W, n, 0); if (g) { const u8 *s = mem_ptr(c->mem, src, n, 0); u8 *d = mem_ptr(c->mem, g, n, 1); if (s && d) e_memcpy(d, s, (size_t)n); g_dobj[id].data = g; g_dobj[id].size = (u32)n; g_dobj[id].extra = (int)pitch; } }
  wr64(c, out, a); RET(HR_OK);
}
static void tex_GetDesc(Cpu *c) { DObj *o = dobj_of(c, ARG(0), DK_TEX2D); u64 d = ARG(1); if (!o || !d) return;
  wr32(c, d, (u32)o->w_); wr32(c, d + 4, (u32)o->h_); wr32(c, d + 8, (u32)o->mips); wr32(c, d + 12, 1); wr32(c, d + 16, (u32)o->fmt); wr32(c, d + 20, 1); wr32(c, d + 24, 0); wr32(c, d + 28, (u32)o->usage); wr32(c, d + 32, (u32)o->bind); wr32(c, d + 36, 0); wr32(c, d + 40, 0); }
static void tex_GetType(Cpu *c) { if (ARG(1)) wr32(c, ARG(1), 3); }   /* D3D11_RESOURCE_DIMENSION_TEXTURE2D */
static void view_create(Cpu *c, int kind, int iface, int need_bind) {
  u64 res = ARG(1), out = ARG(3); DObj *r = dobj_of(c, res, 0); if (!r) { RET(E_INVALIDARG_); return; }
  if (r->kind == DK_TEX2D && need_bind && !(r->bind & need_bind)) { d3d_log("[d3d11] view on a texture created without bind flag 0x%x\n", need_bind); RET(E_INVALIDARG_); return; }
  if (!out) { RET(1); return; }   /* S_FALSE: validation only */
  u64 a; int id = dobj_new(c, kind, iface, &a); if (id < 0) { RET(E_OUTOFMEMORY_); return; }
  g_dobj[id].res = dobj_id(r); g_dobj[id].fmt = r->fmt; wr64(c, out, a); RET(HR_OK);
}
static void dev_CreateRenderTargetView(Cpu *c) { view_create(c, DK_RTV, IF_ID3D11RENDERTARGETVIEW, 0x20); }
static void dev_CreateDepthStencilView(Cpu *c) { view_create(c, DK_DSV, IF_ID3D11DEPTHSTENCILVIEW, 0x40); }
static void dev_CreateShaderResourceView(Cpu *c) { view_create(c, DK_SRV, IF_ID3D11SHADERRESOURCEVIEW, 0); }
static void view_GetResource(Cpu *c) { DObj *o = dobj_of(c, ARG(0), 0); u64 out = ARG(1); if (!o || !out) return; DObj *r = &g_dobj[o->res]; if (r->used) { wr32(c, r->addr + 12, rd32(c, r->addr + 12) + 1); wr64(c, out, r->addr); } else wr64(c, out, 0); }
static void rtv_GetDesc(Cpu *c) { DObj *o = dobj_of(c, ARG(0), DK_RTV); u64 d = ARG(1); if (!o || !d) return; wr32(c, d, (u32)o->fmt); wr32(c, d + 4, 4 /* TEXTURE2D */); wr32(c, d + 8, 0); }

/* ---- generic "keep the description" objects: buffers, shaders, states, layouts, samplers, queries ---- */
static int blob_obj(Cpu *c, int kind, int iface, u64 src, u64 n, u64 out, int extra) {
  if (!out) return 1;   /* S_FALSE validation call */
  u64 a; int id = dobj_new(c, kind, iface, &a); if (id < 0) return (int)E_OUTOFMEMORY_;
  if (src && n) { u64 g = w32_heap_alloc(W, n, 0); if (!g) { dobj_free(&g_dobj[id]); return (int)E_OUTOFMEMORY_; } const u8 *s = mem_ptr(c->mem, src, n, 0); u8 *d = mem_ptr(c->mem, g, n, 1); if (s && d) e_memcpy(d, s, (size_t)n); g_dobj[id].data = g; }
  g_dobj[id].size = (u32)n; g_dobj[id].extra = extra; wr64(c, out, a); return 0;
}
/* D3D11_BUFFER_DESC: ByteWidth@0 Usage@4 BindFlags@8 CPUAccessFlags@12 MiscFlags@16 StructureByteStride@20 */
static void dev_CreateBuffer(Cpu *c) { u64 desc = ARG(1), init = ARG(2), out = ARG(3); if (!desc) { RET(E_INVALIDARG_); return; } u32 n = rd32(c, desc); u64 src = init ? rd64(c, init) : 0;
  int r = blob_obj(c, DK_BUFFER, IF_ID3D11BUFFER, src, n, out, (int)rd32(c, desc + 8)); if (r == 0 && out) { DObj *o = dobj_of(c, rd64(c, out), DK_BUFFER); if (o) { o->usage = (int)rd32(c, desc + 4); o->bind = (int)rd32(c, desc + 8); if (!o->data) { o->data = w32_heap_alloc(W, n ? n : 16, 1); } } } RET((u32)r); }
static void dev_CreateVertexShader(Cpu *c) { RET((u32)blob_obj(c, DK_SHADER, IF_ID3D11VERTEXSHADER, ARG(1), ARG(2), ARG(4), 'v')); }
static void dev_CreatePixelShader(Cpu *c) { RET((u32)blob_obj(c, DK_SHADER, IF_ID3D11PIXELSHADER, ARG(1), ARG(2), ARG(4), 'p')); }
static void dev_CreateInputLayout(Cpu *c) { /* (this, descs, n, bytecode, len, out) */ RET((u32)blob_obj(c, DK_LAYOUT, IF_ID3D11INPUTLAYOUT, ARG(1), (u64)ARG(2) * 32, ARG(5), (int)ARG(2))); }
static void dev_CreateBlendState(Cpu *c) { RET((u32)blob_obj(c, DK_STATE, IF_ID3D11BLENDSTATE, ARG(1), 200, ARG(2), 'b')); }
static void dev_CreateDepthStencilState(Cpu *c) { RET((u32)blob_obj(c, DK_STATE, IF_ID3D11DEPTHSTENCILSTATE, ARG(1), 52, ARG(2), 'd')); }
static void dev_CreateRasterizerState(Cpu *c) { RET((u32)blob_obj(c, DK_STATE, IF_ID3D11RASTERIZERSTATE, ARG(1), 40, ARG(2), 'r')); }
static void dev_CreateSamplerState(Cpu *c) { RET((u32)blob_obj(c, DK_SAMPLER, IF_ID3D11SAMPLERSTATE, ARG(1), 52, ARG(2), 's')); }
static void dev_CreateQuery(Cpu *c) { RET((u32)blob_obj(c, DK_QUERY, IF_ID3D11QUERY, ARG(1), 8, ARG(2), 'q')); }
static void dev_GetImmediateContext(Cpu *c) { u64 out = ARG(1); if (!out) return; for (int i = 1; i < DOBJ_MAX; i++) if (g_dobj[i].used && g_dobj[i].kind == DK_CONTEXT && g_dobj[i].w == W) { wr32(c, g_dobj[i].addr + 12, rd32(c, g_dobj[i].addr + 12) + 1); wr64(c, out, g_dobj[i].addr); return; } wr64(c, out, 0); }
static void dev_GetFeatureLevel(Cpu *c) { RET(0xb000); }   /* D3D_FEATURE_LEVEL_11_0 */
static void dev_GetCreationFlags(Cpu *c) { RET(0); }
static void dev_GetDeviceRemovedReason(Cpu *c) { RET(HR_OK); }
static void dev_CheckMultisampleQualityLevels(Cpu *c) { if (ARG(3)) wr32(c, ARG(3), (u32)ARG(2) == 1 ? 1 : 0); RET(HR_OK); }
static void dev_CheckFormatSupport(Cpu *c) { if (ARG(2)) wr32(c, ARG(2), 0x1 | 0x2 | 0x20 | 0x4000 | 0x8000 | 0x10000); RET(HR_OK); }   /* buffer, IA vertex, texture2D, RT, blendable, DS */

/* ---- immediate context: state is recorded; clears run in software on the target surface ---- */
static struct { int rtv[8], nrtv, dsv; float vp[6]; int vs, ps, layout, ib, vb[4]; u32 topo; } g_ctx;
static void ctx_OMSetRenderTargets(Cpu *c) { u32 n = (u32)ARG(1); u64 arr = ARG(2); g_ctx.nrtv = 0; for (u32 i = 0; i < n && i < 8; i++) { DObj *v = arr ? dobj_of(c, rd64(c, arr + 8 * i), DK_RTV) : 0; g_ctx.rtv[g_ctx.nrtv++] = v ? dobj_id(v) : 0; } DObj *d = ARG(3) ? dobj_of(c, ARG(3), DK_DSV) : 0; g_ctx.dsv = d ? dobj_id(d) : 0; }
static void ctx_ClearRenderTargetView(Cpu *c) {
  DObj *v = dobj_of(c, ARG(1), DK_RTV); if (!v) { d3d_log("[d3d11] ClearRenderTargetView: bad view\n"); return; }
  DObj *t = &g_dobj[v->res]; if (!t->used || !t->px) return;
  const u8 *fp = mem_ptr(c->mem, ARG(2), 16, 0); if (!fp) return; float col[4]; e_memcpy(col, fp, 16);
  u32 px = 0; for (int i = 0; i < 4; i++) { float f = col[i]; if (!(f > 0)) f = 0; if (f > 1) f = 1; u32 b = (u32)(f * 255.0f + 0.5f); px |= b << (i == 0 ? 16 : i == 1 ? 8 : i == 2 ? 0 : 24); }   /* RGBA floats → BGRA pixel */
  size_t n = (size_t)t->w_ * t->h_; for (size_t i = 0; i < n; i++) t->px[i] = px;
}
static void ctx_ClearDepthStencilView(Cpu *c) { DObj *v = dobj_of(c, ARG(1), DK_DSV); if (!v) return; g_dobj[v->res].clear_depth = 1.0f; }
static void ctx_RSSetViewports(Cpu *c) { u32 n = (u32)ARG(1); u64 vp = ARG(2); if (n && vp) { const u8 *p = mem_ptr(c->mem, vp, 24, 0); if (p) e_memcpy(g_ctx.vp, p, 24); } }
static void ctx_RSSetScissorRects(Cpu *c) { (void)c; }
static void ctx_IASetVertexBuffers(Cpu *c) { u32 start = (u32)ARG(1), n = (u32)ARG(2); u64 arr = ARG(3); for (u32 i = 0; i < n && start + i < 4; i++) { DObj *b = arr ? dobj_of(c, rd64(c, arr + 8 * i), DK_BUFFER) : 0; g_ctx.vb[start + i] = b ? dobj_id(b) : 0; } }
static void ctx_IASetIndexBuffer(Cpu *c) { DObj *b = ARG(1) ? dobj_of(c, ARG(1), DK_BUFFER) : 0; g_ctx.ib = b ? dobj_id(b) : 0; }
static void ctx_IASetInputLayout(Cpu *c) { DObj *l = ARG(1) ? dobj_of(c, ARG(1), DK_LAYOUT) : 0; g_ctx.layout = l ? dobj_id(l) : 0; }
static void ctx_IASetPrimitiveTopology(Cpu *c) { g_ctx.topo = (u32)ARG(1); }
static void ctx_VSSetShader(Cpu *c) { DObj *s = ARG(1) ? dobj_of(c, ARG(1), DK_SHADER) : 0; g_ctx.vs = s ? dobj_id(s) : 0; }
static void ctx_PSSetShader(Cpu *c) { DObj *s = ARG(1) ? dobj_of(c, ARG(1), DK_SHADER) : 0; g_ctx.ps = s ? dobj_id(s) : 0; }
static void ctx_SetNothing(Cpu *c) { (void)c; }   /* constant buffers / samplers / SRVs / blend / depth / rasterizer state: recorded by stage 2 */
static void ctx_Flush(Cpu *c) { (void)c; }
static void ctx_ClearState(Cpu *c) { (void)c; e_memset(&g_ctx, 0, sizeof g_ctx); }
/* Map(this, resource, sub, type, flags, D3D11_MAPPED_SUBRESOURCE*): buffers map to their guest storage; textures with initial data likewise */
static void ctx_Map(Cpu *c) { DObj *o = dobj_of(c, ARG(1), 0); u64 out = ARG(5); if (!o || !out) { RET(E_INVALIDARG_); return; }
  if (!o->data) { u64 n = o->kind == DK_TEX2D ? (u64)o->w_ * o->h_ * 4 : (o->size ? o->size : 16); o->data = w32_heap_alloc(W, n, 1); if (o->kind == DK_TEX2D) o->extra = o->w_ * 4; }
  wr64(c, out, o->data); wr32(c, out + 8, o->kind == DK_TEX2D ? (u32)o->extra : o->size); wr32(c, out + 12, o->kind == DK_TEX2D ? (u32)o->extra * (u32)o->h_ : o->size); RET(HR_OK); }
static void ctx_Unmap(Cpu *c) { (void)c; }
/* UpdateSubresource(this, resource, sub, box, data, rowpitch, depthpitch) */
static void ctx_UpdateSubresource(Cpu *c) { DObj *o = dobj_of(c, ARG(1), 0); if (!o) return; u64 src = ARG(4); u64 n = o->kind == DK_TEX2D ? (u64)o->h_ * (u64)(u32)ARG(5) : o->size;
  if (!o->data) { o->data = w32_heap_alloc(W, n ? n : 16, 1); if (o->kind == DK_TEX2D) o->extra = (int)(u32)ARG(5); }
  const u8 *s = mem_ptr(c->mem, src, n, 0); u8 *d = mem_ptr(c->mem, o->data, n, 1); if (s && d && n) e_memcpy(d, s, (size_t)n); }
static void ctx_GetDevice(Cpu *c) { com_GetDevice(c); }

/* ---- swap chain ---- */
static void sc_GetBuffer(Cpu *c) { DObj *s = dobj_of(c, ARG(0), DK_SWAPCHAIN); u64 out = ARG(3); if (!s || !out) { RET(E_INVALIDARG_); return; } DObj *t = &g_dobj[s->sc_bb]; if (!t->used) { RET(DXGI_ERROR_INVALID_CALL_); return; } wr32(c, t->addr + 12, rd32(c, t->addr + 12) + 1); wr64(c, out, t->addr); RET(HR_OK); }
static void sc_present_pixels(Win32 *w, DObj *t) {
  if (!w->screen || !t->px) return; int cw = t->w_ < w->screen_w ? t->w_ : w->screen_w, ch = t->h_ < w->screen_h ? t->h_ : w->screen_h;
  for (int y = 0; y < ch; y++) e_memcpy(w->screen + (size_t)y * w->screen_w, t->px + (size_t)y * t->w_, (size_t)cw * 4);
  w->dirty = 1; w->blits_since_yield++;
}
static void sc_Present(Cpu *c) { DObj *s = dobj_of(c, ARG(0), DK_SWAPCHAIN); if (!s) { RET(DXGI_ERROR_INVALID_CALL_); return; } DObj *t = &g_dobj[s->sc_bb]; if (t->used) sc_present_pixels(W, t); RET(HR_OK); }
static void sc_GetDesc(Cpu *c) { DObj *s = dobj_of(c, ARG(0), DK_SWAPCHAIN); u64 d = ARG(1); if (!s || !d) { RET(E_INVALIDARG_); return; } u8 *p = mem_ptr(c->mem, d, 72, 1); if (!p) { RET(E_INVALIDARG_); return; } e_memset(p, 0, 72);
  wr32(c, d, (u32)s->sc_w); wr32(c, d + 4, (u32)s->sc_h); wr32(c, d + 8, 60); wr32(c, d + 12, 1); wr32(c, d + 16, (u32)g_dobj[s->sc_bb].fmt); wr32(c, d + 28, 1); wr32(c, d + 36, 0x20); wr32(c, d + 40, 1); wr64(c, d + 48, s->sc_hwnd); wr32(c, d + 56, 1); RET(HR_OK); }
static void sc_ResizeBuffers(Cpu *c) { DObj *s = dobj_of(c, ARG(0), DK_SWAPCHAIN); if (!s) { RET(DXGI_ERROR_INVALID_CALL_); return; } u32 nw = (u32)ARG(2), nh = (u32)ARG(3), fmt = (u32)ARG(4);
  if (!nw) nw = (u32)W->screen_w; if (!nh) nh = (u32)W->screen_h; DObj *t = &g_dobj[s->sc_bb];
  if (t->used) { if (rd32(c, t->addr + 12) > 1) { d3d_log("[d3d11] ResizeBuffers while the back buffer is still referenced\n"); RET(DXGI_ERROR_INVALID_CALL_); return; } dobj_free(t); }
  u64 a; int id = tex_create(c, (int)nw, (int)nh, fmt ? (int)fmt : 87, 0x20, &a); if (id < 0) { RET(E_OUTOFMEMORY_); return; } s->sc_bb = id; s->sc_w = (int)nw; s->sc_h = (int)nh; RET(HR_OK); }
static void sc_SetFullscreenState(Cpu *c) { RET(HR_OK); }
static void sc_GetFullscreenState(Cpu *c) { if (ARG(1)) wr32(c, ARG(1), 0); if (ARG(2)) wr64(c, ARG(2), 0); RET(HR_OK); }
static void sc_GetParent(Cpu *c) { if (ARG(2)) wr64(c, ARG(2), 0); RET(E_NOINTERFACE_); }
static void sc_GetDevice(Cpu *c) { u64 out = ARG(2); if (!out) { RET(E_INVALIDARG_); return; } wr64(c, out, 0); for (int i = 1; i < DOBJ_MAX; i++) if (g_dobj[i].used && g_dobj[i].kind == DK_DEVICE && g_dobj[i].w == W) { wr32(c, g_dobj[i].addr + 12, rd32(c, g_dobj[i].addr + 12) + 1); wr64(c, out, g_dobj[i].addr); } RET(rd64(c, out) ? HR_OK : E_NOINTERFACE_); }

/* ---- entry points ---- */
/* D3D11CreateDevice(adapter, driverType, software, flags, levels, nlevels, sdk, ppDevice, pLevel, ppContext) */
static int d3d_make_device(Cpu *c, u64 pp_dev, u64 p_level, u64 pp_ctx) {
  u64 dev = 0, ctx = 0; for (int i = 1; i < DOBJ_MAX; i++) if (g_dobj[i].used && g_dobj[i].w == W) { if (g_dobj[i].kind == DK_DEVICE) dev = g_dobj[i].addr; if (g_dobj[i].kind == DK_CONTEXT) ctx = g_dobj[i].addr; }
  if (!dev && dobj_new(c, DK_DEVICE, IF_ID3D11DEVICE, &dev) < 0) return -1;
  if (!ctx && dobj_new(c, DK_CONTEXT, IF_ID3D11DEVICECONTEXT, &ctx) < 0) return -1;
  if (pp_dev) { wr32(c, dev + 12, rd32(c, dev + 12) + 1); wr64(c, pp_dev, dev); }
  if (pp_ctx) { wr32(c, ctx + 12, rd32(c, ctx + 12) + 1); wr64(c, pp_ctx, ctx); }
  if (p_level) wr32(c, p_level, 0xb000);
  return 0;
}
static void s_D3D11CreateDevice(Cpu *c) { if (d3d_make_device(c, ARG(7), ARG(8), ARG(9)) < 0) { RET(E_OUTOFMEMORY_); return; } d3d_log("[d3d11] D3D11CreateDevice: feature level 11_0 (Nebula x64 software/WebGL2 device)\n"); RET(HR_OK); }
static void s_D3D11CreateDeviceAndSwapChain(Cpu *c) {
  u64 scd = ARG(7), pp_sc = ARG(8), pp_dev = ARG(9), p_level = ARG(10), pp_ctx = ARG(11);
  if (d3d_make_device(c, pp_dev, p_level, pp_ctx) < 0) { RET(E_OUTOFMEMORY_); return; }
  if (scd && pp_sc) {
    u32 bw = rd32(c, scd), bh = rd32(c, scd + 4), fmt = rd32(c, scd + 16); u64 hwnd = rd64(c, scd + 48);
    if (!W->win.created) { d3d_log("[d3d11] swap chain requested before any window exists\n"); RET(DXGI_ERROR_INVALID_CALL_); return; }
    if (!bw) bw = (u32)W->screen_w; if (!bh) bh = (u32)W->screen_h; if (bw > 4096) bw = 4096; if (bh > 4096) bh = 4096;
    u64 sc; int sid = dobj_new(c, DK_SWAPCHAIN, IF_IDXGISWAPCHAIN, &sc); if (sid < 0) { RET(E_OUTOFMEMORY_); return; }
    u64 bb; int tid = tex_create(c, (int)bw, (int)bh, fmt ? (int)fmt : 87, 0x20, &bb); if (tid < 0) { RET(E_OUTOFMEMORY_); return; }
    g_dobj[sid].sc_bb = tid; g_dobj[sid].sc_w = (int)bw; g_dobj[sid].sc_h = (int)bh; g_dobj[sid].sc_hwnd = hwnd; wr64(c, pp_sc, sc);
    d3d_log("[d3d11] D3D11CreateDeviceAndSwapChain: %ux%u format %u, window %ux%u\n", bw, bh, fmt, W->screen_w, W->screen_h);
  }
  RET(HR_OK);
}
static void s_CreateDXGIFactory(Cpu *c) { d3d_log("[d3d11] CreateDXGIFactory: the adapter/factory enumeration path is not implemented yet — use D3D11CreateDeviceAndSwapChain\n"); if (ARG(1)) wr64(c, ARG(1), 0); RET(E_NOINTERFACE_); }

#define DS(api, fn) { "", api, fn }
#define D3D_SHIMS \
  S(D3D11CreateDevice), S(D3D11CreateDeviceAndSwapChain), S(CreateDXGIFactory), SA("CreateDXGIFactory1", CreateDXGIFactory), SA("CreateDXGIFactory2", CreateDXGIFactory), \
  DS("ID3D11Device::QueryInterface", com_QueryInterface), DS("ID3D11Device::AddRef", com_AddRef), DS("ID3D11Device::Release", com_Release), \
  DS("ID3D11Device::CreateBuffer", dev_CreateBuffer), DS("ID3D11Device::CreateTexture2D", dev_CreateTexture2D), DS("ID3D11Device::CreateRenderTargetView", dev_CreateRenderTargetView), \
  DS("ID3D11Device::CreateDepthStencilView", dev_CreateDepthStencilView), DS("ID3D11Device::CreateShaderResourceView", dev_CreateShaderResourceView), DS("ID3D11Device::CreateInputLayout", dev_CreateInputLayout), \
  DS("ID3D11Device::CreateVertexShader", dev_CreateVertexShader), DS("ID3D11Device::CreatePixelShader", dev_CreatePixelShader), DS("ID3D11Device::CreateBlendState", dev_CreateBlendState), \
  DS("ID3D11Device::CreateDepthStencilState", dev_CreateDepthStencilState), DS("ID3D11Device::CreateRasterizerState", dev_CreateRasterizerState), DS("ID3D11Device::CreateSamplerState", dev_CreateSamplerState), \
  DS("ID3D11Device::CreateQuery", dev_CreateQuery), DS("ID3D11Device::GetImmediateContext", dev_GetImmediateContext), DS("ID3D11Device::GetFeatureLevel", dev_GetFeatureLevel), DS("ID3D11Device::GetCreationFlags", dev_GetCreationFlags), \
  DS("ID3D11Device::GetDeviceRemovedReason", dev_GetDeviceRemovedReason), DS("ID3D11Device::CheckMultisampleQualityLevels", dev_CheckMultisampleQualityLevels), DS("ID3D11Device::CheckFormatSupport", dev_CheckFormatSupport), \
  DS("ID3D11Device::SetPrivateData", com_SetPrivateData), DS("ID3D11Device::SetPrivateDataInterface", com_SetPrivateData), \
  DS("ID3D11DeviceContext::QueryInterface", com_QueryInterface), DS("ID3D11DeviceContext::AddRef", com_AddRef), DS("ID3D11DeviceContext::Release", com_Release), DS("ID3D11DeviceContext::GetDevice", ctx_GetDevice), \
  DS("ID3D11DeviceContext::SetPrivateData", com_SetPrivateData), DS("ID3D11DeviceContext::OMSetRenderTargets", ctx_OMSetRenderTargets), DS("ID3D11DeviceContext::ClearRenderTargetView", ctx_ClearRenderTargetView), \
  DS("ID3D11DeviceContext::ClearDepthStencilView", ctx_ClearDepthStencilView), DS("ID3D11DeviceContext::RSSetViewports", ctx_RSSetViewports), DS("ID3D11DeviceContext::RSSetScissorRects", ctx_RSSetScissorRects), \
  DS("ID3D11DeviceContext::IASetVertexBuffers", ctx_IASetVertexBuffers), DS("ID3D11DeviceContext::IASetIndexBuffer", ctx_IASetIndexBuffer), DS("ID3D11DeviceContext::IASetInputLayout", ctx_IASetInputLayout), \
  DS("ID3D11DeviceContext::IASetPrimitiveTopology", ctx_IASetPrimitiveTopology), DS("ID3D11DeviceContext::VSSetShader", ctx_VSSetShader), DS("ID3D11DeviceContext::PSSetShader", ctx_PSSetShader), \
  DS("ID3D11DeviceContext::VSSetConstantBuffers", ctx_SetNothing), DS("ID3D11DeviceContext::PSSetConstantBuffers", ctx_SetNothing), DS("ID3D11DeviceContext::PSSetSamplers", ctx_SetNothing), DS("ID3D11DeviceContext::PSSetShaderResources", ctx_SetNothing), \
  DS("ID3D11DeviceContext::OMSetBlendState", ctx_SetNothing), DS("ID3D11DeviceContext::OMSetDepthStencilState", ctx_SetNothing), DS("ID3D11DeviceContext::RSSetState", ctx_SetNothing), DS("ID3D11DeviceContext::Flush", ctx_Flush), \
  DS("ID3D11DeviceContext::ClearState", ctx_ClearState), DS("ID3D11DeviceContext::Map", ctx_Map), DS("ID3D11DeviceContext::Unmap", ctx_Unmap), DS("ID3D11DeviceContext::UpdateSubresource", ctx_UpdateSubresource), \
  DS("IDXGISwapChain::QueryInterface", com_QueryInterface), DS("IDXGISwapChain::AddRef", com_AddRef), DS("IDXGISwapChain::Release", com_Release), DS("IDXGISwapChain::SetPrivateData", com_SetPrivateData), \
  DS("IDXGISwapChain::Present", sc_Present), DS("IDXGISwapChain::GetBuffer", sc_GetBuffer), DS("IDXGISwapChain::GetDesc", sc_GetDesc), DS("IDXGISwapChain::ResizeBuffers", sc_ResizeBuffers), \
  DS("IDXGISwapChain::SetFullscreenState", sc_SetFullscreenState), DS("IDXGISwapChain::GetFullscreenState", sc_GetFullscreenState), DS("IDXGISwapChain::GetParent", sc_GetParent), DS("IDXGISwapChain::GetDevice", sc_GetDevice), \
  DS("ID3D11Texture2D::QueryInterface", com_QueryInterface), DS("ID3D11Texture2D::AddRef", com_AddRef), DS("ID3D11Texture2D::Release", com_Release), DS("ID3D11Texture2D::GetDevice", com_GetDevice), DS("ID3D11Texture2D::SetPrivateData", com_SetPrivateData), \
  DS("ID3D11Texture2D::GetDesc", tex_GetDesc), DS("ID3D11Texture2D::GetType", tex_GetType), \
  DS("ID3D11RenderTargetView::QueryInterface", com_QueryInterface), DS("ID3D11RenderTargetView::AddRef", com_AddRef), DS("ID3D11RenderTargetView::Release", com_Release), DS("ID3D11RenderTargetView::GetDevice", com_GetDevice), \
  DS("ID3D11RenderTargetView::GetResource", view_GetResource), DS("ID3D11RenderTargetView::GetDesc", rtv_GetDesc), DS("ID3D11RenderTargetView::SetPrivateData", com_SetPrivateData), \
  DS("ID3D11DepthStencilView::QueryInterface", com_QueryInterface), DS("ID3D11DepthStencilView::AddRef", com_AddRef), DS("ID3D11DepthStencilView::Release", com_Release), DS("ID3D11DepthStencilView::GetResource", view_GetResource), \
  DS("ID3D11ShaderResourceView::QueryInterface", com_QueryInterface), DS("ID3D11ShaderResourceView::AddRef", com_AddRef), DS("ID3D11ShaderResourceView::Release", com_Release), DS("ID3D11ShaderResourceView::GetResource", view_GetResource), \
  DS("ID3D11Buffer::QueryInterface", com_QueryInterface), DS("ID3D11Buffer::AddRef", com_AddRef), DS("ID3D11Buffer::Release", com_Release), DS("ID3D11Buffer::SetPrivateData", com_SetPrivateData), \
  DS("ID3D11VertexShader::QueryInterface", com_QueryInterface), DS("ID3D11VertexShader::AddRef", com_AddRef), DS("ID3D11VertexShader::Release", com_Release), \
  DS("ID3D11PixelShader::QueryInterface", com_QueryInterface), DS("ID3D11PixelShader::AddRef", com_AddRef), DS("ID3D11PixelShader::Release", com_Release), \
  DS("ID3D11InputLayout::QueryInterface", com_QueryInterface), DS("ID3D11InputLayout::AddRef", com_AddRef), DS("ID3D11InputLayout::Release", com_Release), \
  DS("ID3D11RasterizerState::Release", com_Release), DS("ID3D11DepthStencilState::Release", com_Release), DS("ID3D11BlendState::Release", com_Release), DS("ID3D11SamplerState::Release", com_Release), DS("ID3D11Query::Release", com_Release), \
  DS("ID3D11RasterizerState::AddRef", com_AddRef), DS("ID3D11DepthStencilState::AddRef", com_AddRef), DS("ID3D11BlendState::AddRef", com_AddRef), DS("ID3D11SamplerState::AddRef", com_AddRef), DS("ID3D11Query::AddRef", com_AddRef),
