/**
 * glff.ts — the OpenGL 1.x fixed-function pipeline on top of WebGL2, for the opengl32.dll bridge (gl.ts).
 *
 * WebGL2 has no glBegin/glEnd, matrix stacks, lighting, texture environments, display lists, alpha test, fog, raster
 * positions or GLU. Games written against OpenGL 1.1–2.1 (Quake-era engines, SDL 1.2 titles, most freeware GL games,
 * teaching demos) use nothing else, so this file implements that pipeline the way a real driver does it internally:
 *   • the state machine (current color/normal/texcoords, matrices, lights, materials, fog, texenv, texgen, clip planes,
 *     alpha test, polygon mode, point size, client arrays, display lists, raster position) lives here;
 *   • every draw — immediate mode, client arrays, display-list replay, glDrawPixels/glBitmap — is turned into a WebGL2
 *     draw with a generated shader pair selected by the state that affects the shading (textures, lighting, fog, alpha
 *     test, flat shading); everything else is passed as uniforms;
 *   • GLU (gluPerspective/gluLookAt/gluOrtho2D/gluPickMatrix/gluProject/gluUnProject/gluBuild*Mipmaps/gluScaleImage/
 *     quadrics/gluErrorString/gluGetString) is implemented on the same state.
 * Honest limits: no selection/feedback render modes (glRenderMode(GL_SELECT) returns 0 hits), no evaluators (glMap/glEval),
 * no accumulation buffer, no line/polygon stipple, no logic ops, no GLU tessellator/NURBS (callbacks into the program
 * cannot be made from the host), fog is computed per vertex, texture combiners support the common sources/operands.
 */
type Impl = (a: BigUint64Array) => number | bigint | void;

export interface FFHost {
  gl(): WebGL2RenderingContext;
  def(name: string, f: Impl): void;
  warn(key: string, msg: string): void;
  setErr(e: number): void;
  f32(addr: bigint, n: number): Float32Array | null;
  f64(addr: bigint, n: number): Float64Array | null;
  i32(addr: bigint, n: number): Int32Array | null;
  u8(addr: bigint, len: number, write?: boolean): Uint8Array | null;
  view(addr: bigint, len: number, type: number): ArrayBufferView | null;
  wrf32(addr: bigint, v: number): void; wrf64(addr: bigint, v: number): void; wr32(addr: bigint, v: number): void; wrp(addr: bigint, v: bigint): void;
  gstr(s: string): bigint;
  rdstr(addr: bigint): string;
  alloc(len: number): bigint; free(addr: bigint): void;
  pixels(ptr: bigint, w: number, h: number, format: number, type: number): { data: ArrayBufferView | null; format: number; type: number } | null;
  texIdOn(unit: number, target: number): number;          // GL name bound on a unit for a target (0 = none)
  texHasAlpha(id: number): boolean;
  bound(): { array: number; element: number; program: number; vao: number; unit: number };
  bufObj(id: number): WebGLBuffer | null;
  vaoObj(id: number): WebGLVertexArrayObject | null;
  progObj(id: number): WebGLProgram | null;
  callImpl(name: string, args: bigint[]): number | bigint | void;
  unpackAlign(): number;
  viewport(): number[];
}

const u32f = new Uint32Array(1), f32v = new Float32Array(u32f.buffer);
const u64f = new BigUint64Array(1), f64v = new Float64Array(u64f.buffer);
const I = (v: bigint) => Number(BigInt.asIntN(32, v));
const U = (v: bigint) => Number(v & 0xffffffffn);
const F = (v: bigint) => { u32f[0] = Number(v & 0xffffffffn); return f32v[0]; };
const D = (v: bigint) => { u64f[0] = v; return f64v[0]; };
const S16 = (v: bigint) => (Number(v & 0xffffn) << 16) >> 16;
const UB = (v: bigint) => Number(v & 0xffn) / 255;
const SB = (v: bigint) => Math.max(-1, ((Number(v & 0xffn) << 24) >> 24) / 127);
const US = (v: bigint) => Number(v & 0xffffn) / 65535;
const SS = (v: bigint) => Math.max(-1, S16(v) / 32767);
const UI = (v: bigint) => U(v) / 4294967295;
const SI = (v: bigint) => Math.max(-1, I(v) / 2147483647);

const E = {
  POINTS: 0, LINES: 1, LINE_LOOP: 2, LINE_STRIP: 3, TRIANGLES: 4, TRIANGLE_STRIP: 5, TRIANGLE_FAN: 6, QUADS: 7, QUAD_STRIP: 8, POLYGON: 9,
  MODELVIEW: 0x1700, PROJECTION: 0x1701, TEXTURE: 0x1702, COLOR: 0x1800,
  MODELVIEW_MATRIX: 0x0BA6, PROJECTION_MATRIX: 0x0BA7, TEXTURE_MATRIX: 0x0BA8, MATRIX_MODE: 0x0BA0, MODELVIEW_STACK_DEPTH: 0x0BA3, PROJECTION_STACK_DEPTH: 0x0BA4, TEXTURE_STACK_DEPTH: 0x0BA5,
  CURRENT_COLOR: 0x0B00, CURRENT_NORMAL: 0x0B02, CURRENT_TEXTURE_COORDS: 0x0B03, CURRENT_RASTER_COLOR: 0x0B04, CURRENT_RASTER_POSITION: 0x0B07, CURRENT_RASTER_POSITION_VALID: 0x0B08, CURRENT_RASTER_TEXTURE_COORDS: 0x0B06,
  LIGHTING: 0x0B50, LIGHT0: 0x4000, COLOR_MATERIAL: 0x0B57, COLOR_MATERIAL_FACE: 0x0B55, COLOR_MATERIAL_PARAMETER: 0x0B56, NORMALIZE: 0x0BA1, RESCALE_NORMAL: 0x803A, FOG: 0x0B60, ALPHA_TEST: 0x0BC0, ALPHA_TEST_FUNC: 0x0BC1, ALPHA_TEST_REF: 0x0BC2,
  TEXTURE_1D: 0x0DE0, TEXTURE_2D: 0x0DE1, TEXTURE_GEN_S: 0x0C60, TEXTURE_GEN_T: 0x0C61, TEXTURE_GEN_R: 0x0C62, TEXTURE_GEN_Q: 0x0C63, CLIP_PLANE0: 0x3000,
  POINT_SMOOTH: 0x0B10, LINE_SMOOTH: 0x0B20, POLYGON_SMOOTH: 0x0B41, LINE_STIPPLE: 0x0B24, POLYGON_STIPPLE: 0x0B42, COLOR_LOGIC_OP: 0x0BF2, INDEX_LOGIC_OP: 0x0BF1, AUTO_NORMAL: 0x0D80, COLOR_SUM: 0x8458, MAP1_VERTEX_3: 0x0D97, MAP2_VERTEX_3: 0x0DB7,
  SHADE_MODEL: 0x0B54, FLAT: 0x1D00, SMOOTH: 0x1D01, POLYGON_MODE: 0x0B40, FILL: 0x1B02, LINE: 0x1B01, POINT: 0x1B00, FRONT: 0x0404, BACK: 0x0405, FRONT_AND_BACK: 0x0408, POINT_SIZE: 0x0B11,
  AMBIENT: 0x1200, DIFFUSE: 0x1201, SPECULAR: 0x1202, POSITION: 0x1203, SPOT_DIRECTION: 0x1204, SPOT_EXPONENT: 0x1205, SPOT_CUTOFF: 0x1206, CONSTANT_ATTENUATION: 0x1207, LINEAR_ATTENUATION: 0x1208, QUADRATIC_ATTENUATION: 0x1209,
  EMISSION: 0x1600, SHININESS: 0x1601, AMBIENT_AND_DIFFUSE: 0x1602, COLOR_INDEXES: 0x1603, LIGHT_MODEL_AMBIENT: 0x0B53, LIGHT_MODEL_LOCAL_VIEWER: 0x0B51, LIGHT_MODEL_TWO_SIDE: 0x0B52, LIGHT_MODEL_COLOR_CONTROL: 0x81F8,
  FOG_MODE: 0x0B65, FOG_DENSITY: 0x0B62, FOG_START: 0x0B63, FOG_END: 0x0B64, FOG_COLOR: 0x0B66, FOG_INDEX: 0x0B61, FOG_COORD_SRC: 0x8450, FOG_HINT: 0x0C54, LINEAR: 0x2601, EXP: 0x0800, EXP2: 0x0801,
  TEXTURE_ENV: 0x2300, TEXTURE_ENV_MODE: 0x2200, TEXTURE_ENV_COLOR: 0x2201, MODULATE: 0x2100, DECAL: 0x2101, BLEND: 0x0BE2, REPLACE: 0x1E01, ADD: 0x0104, COMBINE: 0x8570, COMBINE_RGB: 0x8571, COMBINE_ALPHA: 0x8572, RGB_SCALE: 0x8573, ALPHA_SCALE: 0x0D1C,
  SOURCE0_RGB: 0x8580, SOURCE0_ALPHA: 0x8588, OPERAND0_RGB: 0x8590, OPERAND0_ALPHA: 0x8598, TEXTURE_FILTER_CONTROL: 0x8500, TEXTURE_LOD_BIAS: 0x8501, POINT_SPRITE: 0x8861, COORD_REPLACE: 0x8862,
  TEXTURE_GEN_MODE: 0x2500, OBJECT_PLANE: 0x2501, EYE_PLANE: 0x2502, EYE_LINEAR: 0x2400, OBJECT_LINEAR: 0x2401, SPHERE_MAP: 0x2402, NORMAL_MAP: 0x8511, REFLECTION_MAP: 0x8512, S: 0x2000, T: 0x2001, R: 0x2002, Q: 0x2003,
  NEVER: 0x0200, LESS: 0x0201, EQUAL: 0x0202, LEQUAL: 0x0203, GREATER: 0x0204, NOTEQUAL: 0x0205, GEQUAL: 0x0206, ALWAYS: 0x0207,
  VERTEX_ARRAY: 0x8074, NORMAL_ARRAY: 0x8075, COLOR_ARRAY: 0x8076, INDEX_ARRAY: 0x8077, TEXTURE_COORD_ARRAY: 0x8078, EDGE_FLAG_ARRAY: 0x8079, SECONDARY_COLOR_ARRAY: 0x845E, FOG_COORD_ARRAY: 0x8457,
  VERTEX_ARRAY_SIZE: 0x807A, VERTEX_ARRAY_TYPE: 0x807B, VERTEX_ARRAY_STRIDE: 0x807C, NORMAL_ARRAY_TYPE: 0x807E, NORMAL_ARRAY_STRIDE: 0x807F, COLOR_ARRAY_SIZE: 0x8081, COLOR_ARRAY_TYPE: 0x8082, COLOR_ARRAY_STRIDE: 0x8083, TEXTURE_COORD_ARRAY_SIZE: 0x8088, TEXTURE_COORD_ARRAY_TYPE: 0x8089, TEXTURE_COORD_ARRAY_STRIDE: 0x808A,
  VERTEX_ARRAY_POINTER: 0x808E, NORMAL_ARRAY_POINTER: 0x808F, COLOR_ARRAY_POINTER: 0x8090, TEXTURE_COORD_ARRAY_POINTER: 0x8092, CLIENT_ACTIVE_TEXTURE: 0x84E1, ACTIVE_TEXTURE: 0x84E0, TEXTURE0: 0x84C0,
  BYTE: 0x1400, UNSIGNED_BYTE: 0x1401, SHORT: 0x1402, UNSIGNED_SHORT: 0x1403, INT: 0x1404, UNSIGNED_INT: 0x1405, FLOAT: 0x1406, DOUBLE: 0x140A,
  COMPILE: 0x1300, COMPILE_AND_EXECUTE: 0x1301, LIST_BASE: 0x0B32, LIST_INDEX: 0x0B33, LIST_MODE: 0x0B30, RENDER: 0x1C00, FEEDBACK: 0x1C01, SELECT: 0x1C02, RENDER_MODE: 0x0C40, ZOOM_X: 0x0D16, ZOOM_Y: 0x0D17,
  INVALID_ENUM: 0x500, INVALID_VALUE: 0x501, INVALID_OPERATION: 0x502, STACK_OVERFLOW: 0x503, STACK_UNDERFLOW: 0x504, OUT_OF_MEMORY: 0x505,
  CURRENT_BIT: 1, POINT_BIT: 2, LINE_BIT: 4, POLYGON_BIT: 8, POLYGON_STIPPLE_BIT: 0x10, PIXEL_MODE_BIT: 0x20, LIGHTING_BIT: 0x40, FOG_BIT: 0x80, DEPTH_BUFFER_BIT: 0x100, ACCUM_BUFFER_BIT: 0x200, STENCIL_BUFFER_BIT: 0x400, VIEWPORT_BIT: 0x800, TRANSFORM_BIT: 0x1000, ENABLE_BIT: 0x2000, COLOR_BUFFER_BIT: 0x4000, HINT_BIT: 0x8000, EVAL_BIT: 0x10000, LIST_BIT: 0x20000, TEXTURE_BIT: 0x40000, SCISSOR_BIT: 0x80000, ALL_ATTRIB_BITS: 0xFFFFF, CLIENT_VERTEX_ARRAY_BIT: 2,
  RGBA: 0x1908, RGB: 0x1907, LUMINANCE: 0x1909, LUMINANCE_ALPHA: 0x190A, ALPHA: 0x1906, BGRA: 0x80E1, BGR: 0x80E0, DEPTH_COMPONENT: 0x1902, STENCIL_INDEX: 0x1901, COLOR_INDEX: 0x1900, RED: 0x1903, GREEN: 0x1904, BLUE: 0x1905,
  TEXTURE_BINDING_2D: 0x8069, DEPTH_TEST: 0x0B71, CULL_FACE: 0x0B44, SCISSOR_TEST: 0x0C11, STENCIL_TEST: 0x0B90, DITHER: 0x0BD0, VIEWPORT: 0x0BA2, DEPTH_RANGE: 0x0B70,
  EDGE_FLAG: 0x0B43, CURRENT_INDEX: 0x0B01, UNPACK_ALIGNMENT: 0x0CF5, LINE_WIDTH: 0x0B21,
} as const;
const SHADE_FLAT = 1;
const VSTRIDE = 19;   // floats per immediate-mode vertex: pos4 color4 normal3 tc0 tc1 (vec4 each)

type Mat = Float32Array;
const identity = (): Mat => new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
function mul(a: Mat, b: Mat, out?: Mat): Mat {   // out = a * b (column-major)
  const o = out ?? new Float32Array(16);
  for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) o[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
  return o; }
function invert(m: Mat): Mat | null {
  const a = m, inv = new Float32Array(16);
  inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
  inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
  inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
  inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
  inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
  inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
  inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
  inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
  inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
  inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
  inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
  inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
  inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
  inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
  inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
  inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
  let det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12]; if (!det) return null; det = 1 / det;
  for (let i = 0; i < 16; i++) inv[i] *= det; return inv; }
function xform(m: Mat, v: ArrayLike<number>, out = new Float32Array(4)): Float32Array {
  const x = v[0], y = v[1], z = v[2], w = v[3];
  out[0] = m[0] * x + m[4] * y + m[8] * z + m[12] * w; out[1] = m[1] * x + m[5] * y + m[9] * z + m[13] * w; out[2] = m[2] * x + m[6] * y + m[10] * z + m[14] * w; out[3] = m[3] * x + m[7] * y + m[11] * z + m[15] * w; return out; }
function normalMatrix(mv: Mat): Float32Array {   // inverse-transpose of the upper 3×3
  const inv = invert(mv); const o = new Float32Array(9); if (!inv) { o[0] = o[4] = o[8] = 1; return o; }
  o[0] = inv[0]; o[1] = inv[4]; o[2] = inv[8]; o[3] = inv[1]; o[4] = inv[5]; o[5] = inv[9]; o[6] = inv[2]; o[7] = inv[6]; o[8] = inv[10]; return o; }

interface Light { amb: Float32Array; dif: Float32Array; spec: Float32Array; pos: Float32Array; spotDir: Float32Array; spotExp: number; spotCut: number; att: Float32Array; on: boolean }
interface TexUnit { on2d: boolean; on1d: boolean; env: number; envColor: Float32Array; comb: Int32Array /* rgb, a, src rgb×3, op rgb×3, src a×3, op a×3 */; scaleRgb: number; scaleA: number; gen: Int32Array /* s t r q mode or 0 */; genObj: Float32Array[]; genEye: Float32Array[]; lodBias: number }
interface ClientArray { on: boolean; size: number; type: number; stride: number; ptr: bigint; buffer: number }
interface ClientArrays { vertex: ClientArray; normal: ClientArray; color: ClientArray; tc: ClientArray[] }
interface Program { p: WebGLProgram; u: Map<string, WebGLUniformLocation | null>; ver: Record<string, number> }
interface ListOp { kind: 'call'; id: number; name: string; a: BigUint64Array; snaps: { idx: number; bytes: Uint8Array }[] }
interface ListBatch { kind: 'batch'; mode: number; verts: Float32Array; n: number; fromCurrent: number /* bit0 color, bit1 normal, bit2 tc0, bit3 tc1 */ }
type ListItem = ListOp | ListBatch;
interface Quadric { drawStyle: number; normals: number; texture: boolean; orientation: number }

const fnBytes: Record<string, (a: BigUint64Array, ps: number) => { idx: number; n: number }[]> = {
  glVertex2fv: () => [{ idx: 0, n: 8 }], glVertex3fv: () => [{ idx: 0, n: 12 }], glVertex4fv: () => [{ idx: 0, n: 16 }], glVertex2dv: () => [{ idx: 0, n: 16 }], glVertex3dv: () => [{ idx: 0, n: 24 }], glVertex4dv: () => [{ idx: 0, n: 32 }],
  glVertex2iv: () => [{ idx: 0, n: 8 }], glVertex3iv: () => [{ idx: 0, n: 12 }], glVertex4iv: () => [{ idx: 0, n: 16 }], glVertex2sv: () => [{ idx: 0, n: 4 }], glVertex3sv: () => [{ idx: 0, n: 6 }], glVertex4sv: () => [{ idx: 0, n: 8 }],
  glColor3fv: () => [{ idx: 0, n: 12 }], glColor4fv: () => [{ idx: 0, n: 16 }], glColor3dv: () => [{ idx: 0, n: 24 }], glColor4dv: () => [{ idx: 0, n: 32 }], glColor3ubv: () => [{ idx: 0, n: 3 }], glColor4ubv: () => [{ idx: 0, n: 4 }], glColor3bv: () => [{ idx: 0, n: 3 }], glColor4bv: () => [{ idx: 0, n: 4 }],
  glColor3sv: () => [{ idx: 0, n: 6 }], glColor4sv: () => [{ idx: 0, n: 8 }], glColor3usv: () => [{ idx: 0, n: 6 }], glColor4usv: () => [{ idx: 0, n: 8 }], glColor3iv: () => [{ idx: 0, n: 12 }], glColor4iv: () => [{ idx: 0, n: 16 }], glColor3uiv: () => [{ idx: 0, n: 12 }], glColor4uiv: () => [{ idx: 0, n: 16 }],
  glNormal3fv: () => [{ idx: 0, n: 12 }], glNormal3dv: () => [{ idx: 0, n: 24 }], glNormal3bv: () => [{ idx: 0, n: 3 }], glNormal3sv: () => [{ idx: 0, n: 6 }], glNormal3iv: () => [{ idx: 0, n: 12 }],
  glTexCoord1fv: () => [{ idx: 0, n: 4 }], glTexCoord2fv: () => [{ idx: 0, n: 8 }], glTexCoord3fv: () => [{ idx: 0, n: 12 }], glTexCoord4fv: () => [{ idx: 0, n: 16 }], glTexCoord1dv: () => [{ idx: 0, n: 8 }], glTexCoord2dv: () => [{ idx: 0, n: 16 }], glTexCoord3dv: () => [{ idx: 0, n: 24 }], glTexCoord4dv: () => [{ idx: 0, n: 32 }],
  glTexCoord2iv: () => [{ idx: 0, n: 8 }], glTexCoord2sv: () => [{ idx: 0, n: 4 }], glMultiTexCoord2fv: () => [{ idx: 1, n: 8 }], glMultiTexCoord2fvARB: () => [{ idx: 1, n: 8 }], glMultiTexCoord4fv: () => [{ idx: 1, n: 16 }], glMultiTexCoord4fvARB: () => [{ idx: 1, n: 16 }],
  glRasterPos2fv: () => [{ idx: 0, n: 8 }], glRasterPos3fv: () => [{ idx: 0, n: 12 }], glRasterPos4fv: () => [{ idx: 0, n: 16 }], glRasterPos2dv: () => [{ idx: 0, n: 16 }], glRasterPos3dv: () => [{ idx: 0, n: 24 }], glRasterPos2iv: () => [{ idx: 0, n: 8 }], glRasterPos3iv: () => [{ idx: 0, n: 12 }],
  glLoadMatrixf: () => [{ idx: 0, n: 64 }], glMultMatrixf: () => [{ idx: 0, n: 64 }], glLoadMatrixd: () => [{ idx: 0, n: 128 }], glMultMatrixd: () => [{ idx: 0, n: 128 }], glLoadTransposeMatrixf: () => [{ idx: 0, n: 64 }], glMultTransposeMatrixf: () => [{ idx: 0, n: 64 }],
  glLightfv: () => [{ idx: 2, n: 16 }], glLightiv: () => [{ idx: 2, n: 16 }], glMaterialfv: () => [{ idx: 2, n: 16 }], glMaterialiv: () => [{ idx: 2, n: 16 }], glLightModelfv: () => [{ idx: 1, n: 16 }], glLightModeliv: () => [{ idx: 1, n: 16 }], glFogfv: () => [{ idx: 1, n: 16 }], glFogiv: () => [{ idx: 1, n: 16 }],
  glTexEnvfv: () => [{ idx: 2, n: 16 }], glTexEnviv: () => [{ idx: 2, n: 16 }], glTexGenfv: () => [{ idx: 2, n: 16 }], glTexGeniv: () => [{ idx: 2, n: 16 }], glTexGendv: () => [{ idx: 2, n: 32 }], glClipPlane: () => [{ idx: 1, n: 32 }], glTexParameterfv: () => [{ idx: 2, n: 16 }], glTexParameteriv: () => [{ idx: 2, n: 16 }],
  glBitmap: (a) => [{ idx: 6, n: Math.ceil(I(a[0]) / 8) * I(a[1]) }],
  glCallLists: (a, ps) => { void ps; const t = U(a[1]); const tb = t === E.UNSIGNED_BYTE || t === E.BYTE || t === 0x1407 ? 1 : t === E.SHORT || t === E.UNSIGNED_SHORT || t === 0x1408 ? 2 : t === 0x1409 ? 3 : 4; return [{ idx: 2, n: I(a[0]) * tb }]; },
};
const GLU_LISTED = new Set(['gluPerspective', 'gluLookAt', 'gluOrtho2D', 'gluPickMatrix']);
const NOT_LISTED = new Set(['glGenLists', 'glDeleteLists', 'glIsList', 'glEndList', 'glNewList', 'glGenTextures', 'glDeleteTextures', 'glIsTexture', 'glIsEnabled', 'glFlush', 'glFinish', 'glGetError', 'glReadPixels', 'glRenderMode', 'glFeedbackBuffer', 'glSelectBuffer',
  'glPixelStorei', 'glPixelStoref', 'glEnableClientState', 'glDisableClientState', 'glVertexPointer', 'glColorPointer', 'glNormalPointer', 'glTexCoordPointer', 'glIndexPointer', 'glEdgeFlagPointer', 'glInterleavedArrays', 'glClientActiveTexture', 'glClientActiveTextureARB', 'glPushClientAttrib', 'glPopClientAttrib',
  'glGetString', 'glAreTexturesResident', 'glBindBuffer', 'glBindBufferARB', 'glBufferData', 'glBufferDataARB', 'glBufferSubData', 'glGenBuffers', 'glGenBuffersARB', 'glDeleteBuffers', 'glDeleteBuffersARB', 'glMapBuffer', 'glUnmapBuffer', 'glVertexAttribPointer', 'glEnableVertexAttribArray', 'glDisableVertexAttribArray',
  'glBindVertexArray', 'glGenVertexArrays', 'glDeleteVertexArrays', 'glCreateShader', 'glShaderSource', 'glCompileShader', 'glCreateProgram', 'glAttachShader', 'glLinkProgram', 'glDeleteShader', 'glDeleteProgram', 'glGetUniformLocation', 'glGetAttribLocation', 'glBindAttribLocation', 'glGenFramebuffers', 'glDeleteFramebuffers', 'glGenRenderbuffers', 'glDeleteRenderbuffers', 'glGenQueries', 'glDeleteQueries']);

export class FixedFunction {
  private h: FFHost;
  // matrices
  private mode: number = E.MODELVIEW; private mv: Mat[] = [identity()]; private pr: Mat[] = [identity()]; private tx: Mat[][] = [[identity()], [identity()], [identity()], [identity()]];
  // current vertex attributes
  private color: Float32Array = new Float32Array([1, 1, 1, 1]); private normal: Float32Array = new Float32Array([0, 0, 1]); private tc: Float32Array[] = [new Float32Array([0, 0, 0, 1]), new Float32Array([0, 0, 0, 1]), new Float32Array([0, 0, 0, 1]), new Float32Array([0, 0, 0, 1])];
  // immediate mode
  private inBegin = false; private prim = 0; private verts = new Float32Array(VSTRIDE * 4096); private nv = 0; private setMask = 0;
  // enables / shading state
  lighting = false; private lights: Light[] = []; private colorMaterial = false; private cmFace: number = E.FRONT_AND_BACK; private cmMode: number = E.AMBIENT_AND_DIFFUSE; private normalize = false; private rescale = false;
  private lmAmbient: Float32Array = new Float32Array([0.2, 0.2, 0.2, 1]); private localViewer = false; private twoSide = false;
  private mat: { amb: Float32Array; dif: Float32Array; spec: Float32Array; emis: Float32Array; shin: number } = { amb: new Float32Array([0.2, 0.2, 0.2, 1]), dif: new Float32Array([0.8, 0.8, 0.8, 1]), spec: new Float32Array([0, 0, 0, 1]), emis: new Float32Array([0, 0, 0, 1]), shin: 0 };
  private fog = false; private fogMode: number = E.EXP; private fogDensity = 1; private fogStart = 0; private fogEnd = 1; private fogColor: Float32Array = new Float32Array([0, 0, 0, 0]);
  private alphaTest = false; private alphaFunc: number = E.ALWAYS; private alphaRef = 0;
  private units: TexUnit[] = []; private clientUnit = 0;
  private clip: (Float32Array | null)[] = [null, null, null, null, null, null]; private clipOn = 0;
  private shade: number = E.SMOOTH; private polyFront: number = E.FILL; private polyBack: number = E.FILL; private pointSize = 1;
  // client arrays
  private arr: ClientArrays = { vertex: this.ca(4), normal: this.ca(3), color: this.ca(4), tc: [this.ca(4), this.ca(4), this.ca(4), this.ca(4)] };
  // display lists
  private lists = new Map<number, ListItem[]>(); private nextList = 1; private listBase = 0; private rec: ListItem[] | null = null; private recId = 0; private recMode: number = E.COMPILE; private recSet = 0; private recSaved: { color: Float32Array; normal: Float32Array; tc: Float32Array[] } | null = null;
  private renderMode: number = E.RENDER;
  // raster position
  private raster: { pos: Float32Array; win: Float32Array; valid: boolean; color: Float32Array } = { pos: new Float32Array([0, 0, 0, 1]), win: new Float32Array([0, 0, 0]), valid: true, color: new Float32Array([1, 1, 1, 1]) }; private zoomX = 1; private zoomY = 1;
  // attribute stack
  private attribStack: { mask: number; state: Record<string, unknown> }[] = []; private clientStack: { arr: ClientArrays; unit: number }[] = [];
  // GL objects
  private vao: WebGLVertexArrayObject | null = null; private vbo: WebGLBuffer | null = null; private ibo: WebGLBuffer | null = null; private progs = new Map<string, Program>(); private blitProg: Program | null = null; private blitTex: WebGLTexture | null = null;
  private ver = { light: 1, mat: 1, fog: 1, env: 1, gen: 1, clip: 1, misc: 1 };
  private quadrics = new Map<number, Quadric>(); private nextQuadric = 1; private tessWarned = false;
  private gluStrings = new Map<number, bigint>();

  constructor(h: FFHost) {
    this.h = h;
    for (let i = 0; i < 8; i++) this.lights.push({ amb: new Float32Array([0, 0, 0, 1]), dif: new Float32Array(i === 0 ? [1, 1, 1, 1] : [0, 0, 0, 1]), spec: new Float32Array(i === 0 ? [1, 1, 1, 1] : [0, 0, 0, 1]), pos: new Float32Array([0, 0, 1, 0]), spotDir: new Float32Array([0, 0, -1]), spotExp: 0, spotCut: 180, att: new Float32Array([1, 0, 0]), on: false });
    for (let u = 0; u < 4; u++) this.units.push({ on2d: false, on1d: false, env: E.MODULATE, envColor: new Float32Array([0, 0, 0, 0]), comb: new Int32Array([E.MODULATE, E.MODULATE, 0x1702, 0x8578, 0x8577, 0x0300, 0x0300, 0x0302, 0x1702, 0x8578, 0x8577, 0x0302, 0x0302, 0x0302]), scaleRgb: 1, scaleA: 1, gen: new Int32Array(4), genObj: [new Float32Array([1, 0, 0, 0]), new Float32Array([0, 1, 0, 0]), new Float32Array(4), new Float32Array(4)], genEye: [new Float32Array([1, 0, 0, 0]), new Float32Array([0, 1, 0, 0]), new Float32Array(4), new Float32Array(4)], lodBias: 0 });
  }
  private ca(size: number): ClientArray { return { on: false, size, type: E.FLOAT, stride: 0, ptr: 0n, buffer: 0 }; }
  private err(e: number) { this.h.setErr(e); }
  private warn(k: string, m: string) { this.h.warn(k, m); }
  private cur(): Mat[] { return this.mode === E.PROJECTION ? this.pr : this.mode === E.TEXTURE ? this.tx[this.h.bound().unit & 3] : this.mv; }
  private top(): Mat { const s = this.cur(); return s[s.length - 1]; }
  private setTop(m: Mat) { const s = this.cur(); s[s.length - 1] = m; }
  private mulTop(m: Mat) { this.setTop(mul(this.top(), m)); }
  get recording(): boolean { return this.rec !== null; }
  /** true when fixed-function client arrays drive glDrawArrays/glDrawElements (no user program bound) */
  ownsDraw(): boolean { return this.arr.vertex.on && !this.h.bound().program; }

  // ---------------- display lists: recording ----------------
  /** called by the bridge before executing an entry point while a list is being compiled; true = captured (do not execute now) */
  record(id: number, name: string, a: BigUint64Array, psize: number): boolean {
    if (!this.rec || NOT_LISTED.has(name) || name.startsWith('glGet') || name.startsWith('glIs') || name.startsWith('wgl') || (name.startsWith('glu') && !GLU_LISTED.has(name))) return false;
    if (this.inBegin && name !== 'glEnd' && (name.startsWith('glVertex') || name.startsWith('glColor') || name.startsWith('glNormal') || name.startsWith('glTexCoord') || name.startsWith('glMultiTexCoord') || name === 'glArrayElement' || name === 'glEdgeFlag' || name.startsWith('glSecondaryColor') || name.startsWith('glFogCoord') || name.startsWith('glIndex'))) {
      // inside glBegin/glEnd: execute now — glEnd compiles the whole primitive into one batch
      return false; }
    if (name === 'glBegin' || name === 'glEnd') return false;   // handled by begin()/end() (they know about recording)
    const sig = fnBytes[name]; const snaps: { idx: number; bytes: Uint8Array }[] = [];
    if (sig) { for (const s of sig(a, psize)) { if (!a[s.idx]) continue; const b = this.h.u8(a[s.idx], s.n); if (!b) return false; snaps.push({ idx: s.idx, bytes: new Uint8Array(b) }); } }
    else if (/^gl(TexImage|TexSubImage|DrawPixels|DrawArrays|DrawElements|DrawRangeElements|PolygonStipple|PixelMap|Map[12]|CompressedTex|TexImage3D|Uniform|VertexAttrib)/.test(name) || name === 'glCallLists') { this.warn('list:' + name, `${name} inside a display list: executed at compile time instead of being stored (pointer data would have to be copied)`); return false; }
    this.rec.push({ kind: 'call', id, name, a: new BigUint64Array(a), snaps });
    let attr = false;
    if (name.startsWith('glColor') && !name.startsWith('glColorMa') && name !== 'glColorMask') { this.recSet |= 1; attr = true; } else if (name.startsWith('glNormal')) { this.recSet |= 2; attr = true; } else if (name.startsWith('glTexCoord')) { this.recSet |= 4; attr = true; } else if (name.startsWith('glMultiTexCoord')) { this.recSet |= 4 | 8; attr = true; }
    if (attr) return false;   // current-attribute calls also execute while compiling so that later primitives in the list bake the right values (glEndList restores the pre-list state)
    return this.recMode === E.COMPILE;   // COMPILE_AND_EXECUTE: fall through and execute too
  }
  private callList(id: number, depth: number) {
    const l = this.lists.get(id); if (!l || depth > 64) return;
    for (const it of l) {
      if (it.kind === 'batch') { this.drawBatch(it.mode, it.verts, it.n, it.fromCurrent); continue; }
      if (it.name === 'glCallList') { this.callList(U(it.a[0]), depth + 1); continue; }
      if (!it.snaps.length) { this.h.callImpl(it.name, Array.from(it.a)); continue; }
      const a = Array.from(it.a); const allocs: bigint[] = [];
      for (const s of it.snaps) { const p = this.h.alloc(s.bytes.length); const d = this.h.u8(p, s.bytes.length, true); if (d) d.set(s.bytes); a[s.idx] = p; allocs.push(p); }
      this.h.callImpl(it.name, a); for (const p of allocs) this.h.free(p);
    }
  }

  // ---------------- registration ----------------
  register() {
    const h = this.h, d = (n: string, f: Impl) => h.def(n, f);
    // -- matrices --
    d('glMatrixMode', (a) => { const m = U(a[0]); if (m === E.MODELVIEW || m === E.PROJECTION || m === E.TEXTURE) this.mode = m; else if (m === E.COLOR) this.warn('colormatrix', 'glMatrixMode(GL_COLOR): the color matrix is not applied'); else this.err(E.INVALID_ENUM); });
    d('glLoadIdentity', () => this.setTop(identity()));
    d('glLoadMatrixf', (a) => { const m = h.f32(a[0], 16); if (m) this.setTop(new Float32Array(m)); }); d('glLoadMatrixd', (a) => { const m = h.f64(a[0], 16); if (m) this.setTop(Float32Array.from(m)); });
    d('glMultMatrixf', (a) => { const m = h.f32(a[0], 16); if (m) this.mulTop(m); }); d('glMultMatrixd', (a) => { const m = h.f64(a[0], 16); if (m) this.mulTop(Float32Array.from(m)); });
    const tr = (m: Float32Array) => { const o = new Float32Array(16); for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) o[c * 4 + r] = m[r * 4 + c]; return o; };
    d('glLoadTransposeMatrixf', (a) => { const m = h.f32(a[0], 16); if (m) this.setTop(tr(m)); }); d('glMultTransposeMatrixf', (a) => { const m = h.f32(a[0], 16); if (m) this.mulTop(tr(m)); });
    d('glLoadTransposeMatrixd', (a) => { const m = h.f64(a[0], 16); if (m) this.setTop(tr(Float32Array.from(m))); }); d('glMultTransposeMatrixd', (a) => { const m = h.f64(a[0], 16); if (m) this.mulTop(tr(Float32Array.from(m))); });
    d('glPushMatrix', () => { const s = this.cur(); const max = this.mode === E.MODELVIEW ? 32 : this.mode === E.PROJECTION ? 4 : 4; if (s.length >= max) { this.err(E.STACK_OVERFLOW); return; } s.push(new Float32Array(this.top())); });
    d('glPopMatrix', () => { const s = this.cur(); if (s.length <= 1) { this.err(E.STACK_UNDERFLOW); return; } s.pop(); });
    d('glTranslatef', (a) => this.translate(F(a[0]), F(a[1]), F(a[2]))); d('glTranslated', (a) => this.translate(D(a[0]), D(a[1]), D(a[2])));
    d('glScalef', (a) => this.scale(F(a[0]), F(a[1]), F(a[2]))); d('glScaled', (a) => this.scale(D(a[0]), D(a[1]), D(a[2])));
    d('glRotatef', (a) => this.rotate(F(a[0]), F(a[1]), F(a[2]), F(a[3]))); d('glRotated', (a) => this.rotate(D(a[0]), D(a[1]), D(a[2]), D(a[3])));
    d('glOrtho', (a) => this.ortho(D(a[0]), D(a[1]), D(a[2]), D(a[3]), D(a[4]), D(a[5]))); d('glFrustum', (a) => this.frustum(D(a[0]), D(a[1]), D(a[2]), D(a[3]), D(a[4]), D(a[5])));
    // -- current attributes --
    const col = (r: number, g: number, b: number, al: number) => { this.color[0] = r; this.color[1] = g; this.color[2] = b; this.color[3] = al; this.setMask |= 1; if (this.colorMaterial) this.ver.mat++; };
    d('glColor3f', (a) => col(F(a[0]), F(a[1]), F(a[2]), 1)); d('glColor4f', (a) => col(F(a[0]), F(a[1]), F(a[2]), F(a[3]))); d('glColor3d', (a) => col(D(a[0]), D(a[1]), D(a[2]), 1)); d('glColor4d', (a) => col(D(a[0]), D(a[1]), D(a[2]), D(a[3])));
    d('glColor3ub', (a) => col(UB(a[0]), UB(a[1]), UB(a[2]), 1)); d('glColor4ub', (a) => col(UB(a[0]), UB(a[1]), UB(a[2]), UB(a[3]))); d('glColor3b', (a) => col(SB(a[0]), SB(a[1]), SB(a[2]), 1)); d('glColor4b', (a) => col(SB(a[0]), SB(a[1]), SB(a[2]), SB(a[3])));
    d('glColor3us', (a) => col(US(a[0]), US(a[1]), US(a[2]), 1)); d('glColor4us', (a) => col(US(a[0]), US(a[1]), US(a[2]), US(a[3]))); d('glColor3s', (a) => col(SS(a[0]), SS(a[1]), SS(a[2]), 1)); d('glColor4s', (a) => col(SS(a[0]), SS(a[1]), SS(a[2]), SS(a[3])));
    d('glColor3ui', (a) => col(UI(a[0]), UI(a[1]), UI(a[2]), 1)); d('glColor4ui', (a) => col(UI(a[0]), UI(a[1]), UI(a[2]), UI(a[3]))); d('glColor3i', (a) => col(SI(a[0]), SI(a[1]), SI(a[2]), 1)); d('glColor4i', (a) => col(SI(a[0]), SI(a[1]), SI(a[2]), SI(a[3])));
    const colv = (n: number, type: number) => (a: BigUint64Array) => { const v = this.readVec(a[0], n, type); if (v) col(v[0], v[1], v[2], n === 4 ? v[3] : 1); };
    d('glColor3fv', colv(3, E.FLOAT)); d('glColor4fv', colv(4, E.FLOAT)); d('glColor3dv', colv(3, E.DOUBLE)); d('glColor4dv', colv(4, E.DOUBLE)); d('glColor3ubv', colv(3, E.UNSIGNED_BYTE)); d('glColor4ubv', colv(4, E.UNSIGNED_BYTE)); d('glColor3bv', colv(3, E.BYTE)); d('glColor4bv', colv(4, E.BYTE));
    d('glColor3sv', colv(3, E.SHORT)); d('glColor4sv', colv(4, E.SHORT)); d('glColor3usv', colv(3, E.UNSIGNED_SHORT)); d('glColor4usv', colv(4, E.UNSIGNED_SHORT)); d('glColor3iv', colv(3, E.INT)); d('glColor4iv', colv(4, E.INT)); d('glColor3uiv', colv(3, E.UNSIGNED_INT)); d('glColor4uiv', colv(4, E.UNSIGNED_INT));
    const nrm = (x: number, y: number, z: number) => { this.normal[0] = x; this.normal[1] = y; this.normal[2] = z; this.setMask |= 2; };
    d('glNormal3f', (a) => nrm(F(a[0]), F(a[1]), F(a[2]))); d('glNormal3d', (a) => nrm(D(a[0]), D(a[1]), D(a[2]))); d('glNormal3b', (a) => nrm(SB(a[0]), SB(a[1]), SB(a[2]))); d('glNormal3s', (a) => nrm(SS(a[0]), SS(a[1]), SS(a[2]))); d('glNormal3i', (a) => nrm(SI(a[0]), SI(a[1]), SI(a[2])));
    const nrmv = (type: number) => (a: BigUint64Array) => { const v = this.readVec(a[0], 3, type); if (v) nrm(v[0], v[1], v[2]); };
    d('glNormal3fv', nrmv(E.FLOAT)); d('glNormal3dv', nrmv(E.DOUBLE)); d('glNormal3bv', nrmv(E.BYTE)); d('glNormal3sv', nrmv(E.SHORT)); d('glNormal3iv', nrmv(E.INT));
    const tcs = (u: number, s: number, t: number, r: number, q: number) => { const v = this.tc[u & 3]; v[0] = s; v[1] = t; v[2] = r; v[3] = q; this.setMask |= u ? 8 : 4; };
    d('glTexCoord1f', (a) => tcs(0, F(a[0]), 0, 0, 1)); d('glTexCoord2f', (a) => tcs(0, F(a[0]), F(a[1]), 0, 1)); d('glTexCoord3f', (a) => tcs(0, F(a[0]), F(a[1]), F(a[2]), 1)); d('glTexCoord4f', (a) => tcs(0, F(a[0]), F(a[1]), F(a[2]), F(a[3])));
    d('glTexCoord1d', (a) => tcs(0, D(a[0]), 0, 0, 1)); d('glTexCoord2d', (a) => tcs(0, D(a[0]), D(a[1]), 0, 1)); d('glTexCoord3d', (a) => tcs(0, D(a[0]), D(a[1]), D(a[2]), 1)); d('glTexCoord4d', (a) => tcs(0, D(a[0]), D(a[1]), D(a[2]), D(a[3])));
    d('glTexCoord1i', (a) => tcs(0, I(a[0]), 0, 0, 1)); d('glTexCoord2i', (a) => tcs(0, I(a[0]), I(a[1]), 0, 1)); d('glTexCoord3i', (a) => tcs(0, I(a[0]), I(a[1]), I(a[2]), 1)); d('glTexCoord4i', (a) => tcs(0, I(a[0]), I(a[1]), I(a[2]), I(a[3])));
    d('glTexCoord1s', (a) => tcs(0, S16(a[0]), 0, 0, 1)); d('glTexCoord2s', (a) => tcs(0, S16(a[0]), S16(a[1]), 0, 1)); d('glTexCoord3s', (a) => tcs(0, S16(a[0]), S16(a[1]), S16(a[2]), 1)); d('glTexCoord4s', (a) => tcs(0, S16(a[0]), S16(a[1]), S16(a[2]), S16(a[3])));
    const tcv = (n: number, type: number) => (a: BigUint64Array) => { const v = this.readVec(a[0], n, type, true); if (v) tcs(0, v[0], n > 1 ? v[1] : 0, n > 2 ? v[2] : 0, n > 3 ? v[3] : 1); };
    d('glTexCoord1fv', tcv(1, E.FLOAT)); d('glTexCoord2fv', tcv(2, E.FLOAT)); d('glTexCoord3fv', tcv(3, E.FLOAT)); d('glTexCoord4fv', tcv(4, E.FLOAT)); d('glTexCoord1dv', tcv(1, E.DOUBLE)); d('glTexCoord2dv', tcv(2, E.DOUBLE)); d('glTexCoord3dv', tcv(3, E.DOUBLE)); d('glTexCoord4dv', tcv(4, E.DOUBLE));
    d('glTexCoord2iv', tcv(2, E.INT)); d('glTexCoord2sv', tcv(2, E.SHORT)); d('glTexCoord1iv', tcv(1, E.INT)); d('glTexCoord1sv', tcv(1, E.SHORT)); d('glTexCoord3iv', tcv(3, E.INT)); d('glTexCoord3sv', tcv(3, E.SHORT)); d('glTexCoord4iv', tcv(4, E.INT)); d('glTexCoord4sv', tcv(4, E.SHORT));
    const mtc = (u: bigint, s: number, t: number, r: number, q: number) => tcs(U(u) - E.TEXTURE0, s, t, r, q);
    d('glMultiTexCoord1f', (a) => mtc(a[0], F(a[1]), 0, 0, 1)); d('glMultiTexCoord2f', (a) => mtc(a[0], F(a[1]), F(a[2]), 0, 1)); d('glMultiTexCoord3f', (a) => mtc(a[0], F(a[1]), F(a[2]), F(a[3]), 1)); d('glMultiTexCoord4f', (a) => mtc(a[0], F(a[1]), F(a[2]), F(a[3]), F(a[4])));
    d('glMultiTexCoord1fARB', (a) => mtc(a[0], F(a[1]), 0, 0, 1)); d('glMultiTexCoord2fARB', (a) => mtc(a[0], F(a[1]), F(a[2]), 0, 1)); d('glMultiTexCoord3fARB', (a) => mtc(a[0], F(a[1]), F(a[2]), F(a[3]), 1)); d('glMultiTexCoord4fARB', (a) => mtc(a[0], F(a[1]), F(a[2]), F(a[3]), F(a[4])));
    d('glMultiTexCoord2d', (a) => mtc(a[0], D(a[1]), D(a[2]), 0, 1)); d('glMultiTexCoord2dARB', (a) => mtc(a[0], D(a[1]), D(a[2]), 0, 1)); d('glMultiTexCoord2i', (a) => mtc(a[0], I(a[1]), I(a[2]), 0, 1)); d('glMultiTexCoord2iARB', (a) => mtc(a[0], I(a[1]), I(a[2]), 0, 1));
    const mtcv = (n: number) => (a: BigUint64Array) => { const v = h.f32(a[1], n); if (v) mtc(a[0], v[0], n > 1 ? v[1] : 0, n > 2 ? v[2] : 0, n > 3 ? v[3] : 1); };
    d('glMultiTexCoord2fv', mtcv(2)); d('glMultiTexCoord2fvARB', mtcv(2)); d('glMultiTexCoord4fv', mtcv(4)); d('glMultiTexCoord4fvARB', mtcv(4)); d('glMultiTexCoord1fv', mtcv(1)); d('glMultiTexCoord3fv', mtcv(3));
    d('glEdgeFlag', () => {}); d('glEdgeFlagv', () => {}); d('glIndexi', () => {}); d('glIndexf', () => {}); d('glIndexd', () => {}); d('glIndexs', () => {}); d('glIndexub', () => {}); d('glIndexiv', () => {}); d('glIndexfv', () => {}); d('glIndexdv', () => {}); d('glIndexsv', () => {}); d('glIndexubv', () => {});
    d('glSecondaryColor3f', () => this.warn('seccolor', 'glSecondaryColor*: the secondary (specular) color is not applied')); d('glFogCoordf', () => this.warn('fogcoord', 'glFogCoord*: per-vertex fog coordinates are not applied (fog uses the eye distance)')); d('glFogCoordd', () => {}); d('glFogCoordfv', () => {});
    // -- vertices --
    const vtx = (x: number, y: number, z: number, w: number) => this.vertex(x, y, z, w);
    d('glVertex2f', (a) => vtx(F(a[0]), F(a[1]), 0, 1)); d('glVertex3f', (a) => vtx(F(a[0]), F(a[1]), F(a[2]), 1)); d('glVertex4f', (a) => vtx(F(a[0]), F(a[1]), F(a[2]), F(a[3])));
    d('glVertex2d', (a) => vtx(D(a[0]), D(a[1]), 0, 1)); d('glVertex3d', (a) => vtx(D(a[0]), D(a[1]), D(a[2]), 1)); d('glVertex4d', (a) => vtx(D(a[0]), D(a[1]), D(a[2]), D(a[3])));
    d('glVertex2i', (a) => vtx(I(a[0]), I(a[1]), 0, 1)); d('glVertex3i', (a) => vtx(I(a[0]), I(a[1]), I(a[2]), 1)); d('glVertex4i', (a) => vtx(I(a[0]), I(a[1]), I(a[2]), I(a[3])));
    d('glVertex2s', (a) => vtx(S16(a[0]), S16(a[1]), 0, 1)); d('glVertex3s', (a) => vtx(S16(a[0]), S16(a[1]), S16(a[2]), 1)); d('glVertex4s', (a) => vtx(S16(a[0]), S16(a[1]), S16(a[2]), S16(a[3])));
    const vv = (n: number, type: number) => (a: BigUint64Array) => { const v = this.readVec(a[0], n, type, true); if (v) vtx(v[0], v[1], n > 2 ? v[2] : 0, n > 3 ? v[3] : 1); };
    d('glVertex2fv', vv(2, E.FLOAT)); d('glVertex3fv', vv(3, E.FLOAT)); d('glVertex4fv', vv(4, E.FLOAT)); d('glVertex2dv', vv(2, E.DOUBLE)); d('glVertex3dv', vv(3, E.DOUBLE)); d('glVertex4dv', vv(4, E.DOUBLE));
    d('glVertex2iv', vv(2, E.INT)); d('glVertex3iv', vv(3, E.INT)); d('glVertex4iv', vv(4, E.INT)); d('glVertex2sv', vv(2, E.SHORT)); d('glVertex3sv', vv(3, E.SHORT)); d('glVertex4sv', vv(4, E.SHORT));
    d('glBegin', (a) => this.begin(U(a[0]))); d('glEnd', () => this.end());
    d('glRectf', (a) => this.rect(F(a[0]), F(a[1]), F(a[2]), F(a[3]))); d('glRectd', (a) => this.rect(D(a[0]), D(a[1]), D(a[2]), D(a[3]))); d('glRecti', (a) => this.rect(I(a[0]), I(a[1]), I(a[2]), I(a[3]))); d('glRects', (a) => this.rect(S16(a[0]), S16(a[1]), S16(a[2]), S16(a[3])));
    d('glRectfv', (a) => { const p = h.f32(a[0], 2), q = h.f32(a[1], 2); if (p && q) this.rect(p[0], p[1], q[0], q[1]); }); d('glRectdv', (a) => { const p = h.f64(a[0], 2), q = h.f64(a[1], 2); if (p && q) this.rect(p[0], p[1], q[0], q[1]); }); d('glRectiv', (a) => { const p = h.i32(a[0], 2), q = h.i32(a[1], 2); if (p && q) this.rect(p[0], p[1], q[0], q[1]); });
    d('glArrayElement', (a) => this.arrayElement(I(a[0])));
    // -- shading state --
    d('glShadeModel', (a) => { const m = U(a[0]); if (m === E.FLAT || m === E.SMOOTH) this.shade = m; else this.err(E.INVALID_ENUM); });
    d('glPolygonMode', (a) => { const f = U(a[0]), m = U(a[1]); if (m !== E.FILL && m !== E.LINE && m !== E.POINT) { this.err(E.INVALID_ENUM); return; } if (f === E.FRONT || f === E.FRONT_AND_BACK) this.polyFront = m; if (f === E.BACK || f === E.FRONT_AND_BACK) this.polyBack = m; if (f !== E.FRONT && f !== E.BACK && f !== E.FRONT_AND_BACK) this.err(E.INVALID_ENUM); });
    d('glPointSize', (a) => { const s = F(a[0]); if (s <= 0) { this.err(E.INVALID_VALUE); return; } this.pointSize = s; });
    d('glAlphaFunc', (a) => { const f = U(a[0]); if (f < E.NEVER || f > E.ALWAYS) { this.err(E.INVALID_ENUM); return; } this.alphaFunc = f; this.alphaRef = Math.min(1, Math.max(0, F(a[1]))); this.ver.misc++; });
    d('glLightModelf', (a) => this.lightModel(U(a[0]), [F(a[1])])); d('glLightModeli', (a) => this.lightModel(U(a[0]), [I(a[1])]));
    d('glLightModelfv', (a) => { const v = h.f32(a[1], U(a[0]) === E.LIGHT_MODEL_AMBIENT ? 4 : 1); if (v) this.lightModel(U(a[0]), Array.from(v)); }); d('glLightModeliv', (a) => { const v = h.i32(a[1], U(a[0]) === E.LIGHT_MODEL_AMBIENT ? 4 : 1); if (v) this.lightModel(U(a[0]), U(a[0]) === E.LIGHT_MODEL_AMBIENT ? Array.from(v, (x) => x / 2147483647) : Array.from(v)); });
    d('glLightf', (a) => this.light(U(a[0]), U(a[1]), [F(a[2])])); d('glLighti', (a) => this.light(U(a[0]), U(a[1]), [I(a[2])]));
    d('glLightfv', (a) => { const p = U(a[1]); const n = p === E.SPOT_DIRECTION ? 3 : p >= E.SPOT_EXPONENT ? 1 : 4; const v = h.f32(a[2], n); if (v) this.light(U(a[0]), p, Array.from(v)); });
    d('glLightiv', (a) => { const p = U(a[1]); const n = p === E.SPOT_DIRECTION ? 3 : p >= E.SPOT_EXPONENT ? 1 : 4; const v = h.i32(a[2], n); if (v) this.light(U(a[0]), p, p <= E.SPECULAR ? Array.from(v, (x) => x / 2147483647) : Array.from(v)); });
    d('glMaterialf', (a) => this.material(U(a[0]), U(a[1]), [F(a[2])])); d('glMateriali', (a) => this.material(U(a[0]), U(a[1]), [I(a[2])]));
    d('glMaterialfv', (a) => { const p = U(a[1]); const v = h.f32(a[2], p === E.SHININESS ? 1 : p === E.COLOR_INDEXES ? 3 : 4); if (v) this.material(U(a[0]), p, Array.from(v)); });
    d('glMaterialiv', (a) => { const p = U(a[1]); const v = h.i32(a[2], p === E.SHININESS ? 1 : 4); if (v) this.material(U(a[0]), p, p === E.SHININESS ? Array.from(v) : Array.from(v, (x) => x / 2147483647)); });
    d('glColorMaterial', (a) => { this.cmFace = U(a[0]); this.cmMode = U(a[1]); this.ver.mat++; });
    d('glGetMaterialfv', (a) => { const p = U(a[1]); const v = p === E.AMBIENT ? this.mat.amb : p === E.DIFFUSE ? this.mat.dif : p === E.SPECULAR ? this.mat.spec : p === E.EMISSION ? this.mat.emis : p === E.SHININESS ? [this.mat.shin] : null; if (!v) { this.err(E.INVALID_ENUM); return; } for (let i = 0; i < v.length; i++) h.wrf32(a[2] + BigInt(i * 4), v[i]); });
    d('glGetMaterialiv', (a) => { const p = U(a[1]); const v = p === E.AMBIENT ? this.mat.amb : p === E.DIFFUSE ? this.mat.dif : p === E.SPECULAR ? this.mat.spec : p === E.EMISSION ? this.mat.emis : p === E.SHININESS ? [this.mat.shin] : null; if (!v) { this.err(E.INVALID_ENUM); return; } for (let i = 0; i < v.length; i++) h.wr32(a[2] + BigInt(i * 4), p === E.SHININESS ? Math.round(v[i]) : Math.round(v[i] * 2147483647)); });
    d('glGetLightfv', (a) => { const v = this.getLight(U(a[0]), U(a[1])); if (v) for (let i = 0; i < v.length; i++) h.wrf32(a[2] + BigInt(i * 4), v[i]); });
    d('glGetLightiv', (a) => { const v = this.getLight(U(a[0]), U(a[1])); if (v) for (let i = 0; i < v.length; i++) h.wr32(a[2] + BigInt(i * 4), Math.round(v[i])); });
    d('glFogf', (a) => this.fogParam(U(a[0]), [F(a[1])])); d('glFogi', (a) => this.fogParam(U(a[0]), [I(a[1])]));
    d('glFogfv', (a) => { const v = h.f32(a[1], U(a[0]) === E.FOG_COLOR ? 4 : 1); if (v) this.fogParam(U(a[0]), Array.from(v)); }); d('glFogiv', (a) => { const v = h.i32(a[1], U(a[0]) === E.FOG_COLOR ? 4 : 1); if (v) this.fogParam(U(a[0]), U(a[0]) === E.FOG_COLOR ? Array.from(v, (x) => x / 2147483647) : Array.from(v)); });
    d('glTexEnvf', (a) => this.texEnv(U(a[0]), U(a[1]), [F(a[2])])); d('glTexEnvi', (a) => this.texEnv(U(a[0]), U(a[1]), [I(a[2])]));
    d('glTexEnvfv', (a) => { const v = h.f32(a[2], U(a[1]) === E.TEXTURE_ENV_COLOR ? 4 : 1); if (v) this.texEnv(U(a[0]), U(a[1]), Array.from(v)); }); d('glTexEnviv', (a) => { const v = h.i32(a[2], U(a[1]) === E.TEXTURE_ENV_COLOR ? 4 : 1); if (v) this.texEnv(U(a[0]), U(a[1]), U(a[1]) === E.TEXTURE_ENV_COLOR ? Array.from(v, (x) => x / 2147483647) : Array.from(v)); });
    d('glGetTexEnvfv', (a) => { const v = this.getTexEnv(U(a[0]), U(a[1])); if (v) v.forEach((x, i) => h.wrf32(a[2] + BigInt(i * 4), x)); }); d('glGetTexEnviv', (a) => { const v = this.getTexEnv(U(a[0]), U(a[1])); if (v) v.forEach((x, i) => h.wr32(a[2] + BigInt(i * 4), Math.round(x))); });
    d('glTexGeni', (a) => this.texGen(U(a[0]), U(a[1]), [I(a[2])])); d('glTexGenf', (a) => this.texGen(U(a[0]), U(a[1]), [F(a[2])])); d('glTexGend', (a) => this.texGen(U(a[0]), U(a[1]), [D(a[2])]));
    d('glTexGeniv', (a) => { const v = h.i32(a[2], U(a[1]) === E.TEXTURE_GEN_MODE ? 1 : 4); if (v) this.texGen(U(a[0]), U(a[1]), Array.from(v)); }); d('glTexGenfv', (a) => { const v = h.f32(a[2], U(a[1]) === E.TEXTURE_GEN_MODE ? 1 : 4); if (v) this.texGen(U(a[0]), U(a[1]), Array.from(v)); }); d('glTexGendv', (a) => { const v = h.f64(a[2], U(a[1]) === E.TEXTURE_GEN_MODE ? 1 : 4); if (v) this.texGen(U(a[0]), U(a[1]), Array.from(v)); });
    d('glGetTexGeniv', (a) => { const v = this.getTexGen(U(a[0]), U(a[1])); if (v) v.forEach((x, i) => h.wr32(a[2] + BigInt(i * 4), Math.round(x))); }); d('glGetTexGenfv', (a) => { const v = this.getTexGen(U(a[0]), U(a[1])); if (v) v.forEach((x, i) => h.wrf32(a[2] + BigInt(i * 4), x)); }); d('glGetTexGendv', (a) => { const v = this.getTexGen(U(a[0]), U(a[1])); if (v) v.forEach((x, i) => h.wrf64(a[2] + BigInt(i * 8), x)); });
    d('glClipPlane', (a) => { const i = U(a[0]) - E.CLIP_PLANE0; const v = h.f64(a[1], 4); if (i < 0 || i >= 6 || !v) { this.err(E.INVALID_ENUM); return; } const inv = invert(this.mv[this.mv.length - 1]); const p = Float32Array.from(v); this.clip[i] = inv ? xformT(inv, p) : p; this.ver.clip++; });
    d('glGetClipPlane', (a) => { const i = U(a[0]) - E.CLIP_PLANE0; const p = i >= 0 && i < 6 ? this.clip[i] : null; for (let k = 0; k < 4; k++) h.wrf64(a[1] + BigInt(k * 8), p ? p[k] : 0); });
    // -- client arrays --
    d('glEnableClientState', (a) => this.clientState(U(a[0]), true)); d('glDisableClientState', (a) => this.clientState(U(a[0]), false));
    d('glClientActiveTexture', (a) => { const u = U(a[0]) - E.TEXTURE0; if (u < 0 || u >= 8) { this.err(E.INVALID_ENUM); return; } this.clientUnit = u & 3; }); d('glClientActiveTextureARB', (a) => { this.clientUnit = (U(a[0]) - E.TEXTURE0) & 3; });
    const ptr = (arr: ClientArray, size: number, type: number, stride: number, p: bigint) => { arr.size = size; arr.type = type; arr.stride = stride; arr.ptr = p; arr.buffer = h.bound().array; };
    d('glVertexPointer', (a) => ptr(this.arr.vertex, I(a[0]), U(a[1]), I(a[2]), a[3])); d('glNormalPointer', (a) => ptr(this.arr.normal, 3, U(a[0]), I(a[1]), a[2])); d('glColorPointer', (a) => ptr(this.arr.color, I(a[0]), U(a[1]), I(a[2]), a[3])); d('glTexCoordPointer', (a) => ptr(this.arr.tc[this.clientUnit], I(a[0]), U(a[1]), I(a[2]), a[3]));
    d('glIndexPointer', () => {}); d('glEdgeFlagPointer', () => {}); d('glSecondaryColorPointer', () => this.warn('seccolor', 'glSecondaryColorPointer: secondary colors are not applied')); d('glFogCoordPointer', () => this.warn('fogcoord', 'glFogCoordPointer: fog coordinates are not applied'));
    d('glInterleavedArrays', (a) => this.interleaved(U(a[0]), I(a[1]), a[2]));
    d('glPushClientAttrib', (a) => { if (U(a[0]) & E.CLIENT_VERTEX_ARRAY_BIT) this.clientStack.push({ arr: { vertex: { ...this.arr.vertex }, normal: { ...this.arr.normal }, color: { ...this.arr.color }, tc: this.arr.tc.map((t) => ({ ...t })) }, unit: this.clientUnit }); else this.clientStack.push({ arr: this.arr, unit: this.clientUnit }); });
    d('glPopClientAttrib', () => { const s = this.clientStack.pop(); if (!s) { this.err(E.STACK_UNDERFLOW); return; } this.arr = s.arr; this.clientUnit = s.unit; });
    d('glGetPointerv', (a) => { const p = U(a[0]); const arr = p === E.VERTEX_ARRAY_POINTER ? this.arr.vertex : p === E.NORMAL_ARRAY_POINTER ? this.arr.normal : p === E.COLOR_ARRAY_POINTER ? this.arr.color : p === E.TEXTURE_COORD_ARRAY_POINTER ? this.arr.tc[this.clientUnit] : null; if (!arr) { this.err(E.INVALID_ENUM); return; } h.wrp(a[1], arr.ptr); });
    // -- display lists --
    d('glGenLists', (a) => { const n = I(a[0]); if (n < 0) { this.err(E.INVALID_VALUE); return 0; } if (!n) return 0; const base = this.nextList; this.nextList += n; for (let i = 0; i < n; i++) this.lists.set(base + i, []); return base; });
    d('glDeleteLists', (a) => { const b = U(a[0]), n = I(a[1]); for (let i = 0; i < n; i++) this.lists.delete(b + i); });
    d('glIsList', (a) => this.lists.has(U(a[0])) ? 1 : 0);
    d('glNewList', (a) => { const id = U(a[0]), m = U(a[1]); if (!id) { this.err(E.INVALID_VALUE); return; } if (this.rec || this.inBegin) { this.err(E.INVALID_OPERATION); return; } if (m !== E.COMPILE && m !== E.COMPILE_AND_EXECUTE) { this.err(E.INVALID_ENUM); return; } this.rec = []; this.recId = id; this.recMode = m; this.recSet = 0; if (id >= this.nextList) this.nextList = id + 1;
      this.recSaved = { color: new Float32Array(this.color), normal: new Float32Array(this.normal), tc: this.tc.map((t) => new Float32Array(t)) }; });
    d('glEndList', () => { if (!this.rec) { this.err(E.INVALID_OPERATION); return; } this.lists.set(this.recId, this.rec); this.rec = null; if (this.recMode === E.COMPILE && this.recSaved) { this.color = this.recSaved.color; this.normal = this.recSaved.normal; this.tc = this.recSaved.tc; } this.recSaved = null; });
    d('glCallList', (a) => { if (this.inBegin) { this.err(E.INVALID_OPERATION); return; } this.callList(U(a[0]), 0); });
    d('glListBase', (a) => { this.listBase = U(a[0]); });
    d('glCallLists', (a) => { const n = I(a[0]), t = U(a[1]); if (n < 0) { this.err(E.INVALID_VALUE); return; } const tb = t === E.UNSIGNED_BYTE || t === E.BYTE ? 1 : t === E.SHORT || t === E.UNSIGNED_SHORT ? 2 : t === 0x1407 ? 1 : t === 0x1408 ? 2 : t === 0x1409 ? 3 : 4; const b = h.u8(a[2], n * tb); if (!b) return; const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
      for (let i = 0; i < n; i++) { let id: number; switch (t) { case E.BYTE: id = dv.getInt8(i); break; case E.UNSIGNED_BYTE: id = dv.getUint8(i); break; case E.SHORT: id = dv.getInt16(i * 2, true); break; case E.UNSIGNED_SHORT: id = dv.getUint16(i * 2, true); break; case E.INT: id = dv.getInt32(i * 4, true); break; case E.UNSIGNED_INT: id = dv.getUint32(i * 4, true); break; case E.FLOAT: id = Math.trunc(dv.getFloat32(i * 4, true)); break; case 0x1407: id = dv.getUint8(i); break; case 0x1408: id = dv.getUint8(i * 2) * 256 + dv.getUint8(i * 2 + 1); break; case 0x1409: id = dv.getUint8(i * 3) * 65536 + dv.getUint8(i * 3 + 1) * 256 + dv.getUint8(i * 3 + 2); break; default: id = dv.getUint8(i * 4) * 16777216 + dv.getUint8(i * 4 + 1) * 65536 + dv.getUint8(i * 4 + 2) * 256 + dv.getUint8(i * 4 + 3); } this.callList(this.listBase + id, 0); } });
    // -- render modes / selection / feedback / accum / evaluators: honest stubs --
    d('glRenderMode', (a) => { const m = U(a[0]); const prev = this.renderMode; if (m === E.SELECT || m === E.FEEDBACK) this.warn('rendermode', 'glRenderMode(GL_SELECT/GL_FEEDBACK): selection and feedback are not implemented — 0 hits are returned (picking by name stack does not work; programs with glReadPixels-based picking are fine)'); this.renderMode = m; return prev === E.RENDER ? 0 : 0; });
    d('glSelectBuffer', () => {}); d('glFeedbackBuffer', () => {}); d('glInitNames', () => {}); d('glLoadName', () => {}); d('glPushName', () => {}); d('glPopName', () => {}); d('glPassThrough', () => {});
    d('glAccum', () => this.warn('accum', 'glAccum: there is no accumulation buffer (motion blur / antialiasing through GL_ACCUM is skipped)')); d('glClearAccum', () => {}); d('glClearIndex', () => {}); d('glIndexMask', () => {});
    for (const n of ['glMap1f', 'glMap1d', 'glMap2f', 'glMap2d', 'glEvalCoord1f', 'glEvalCoord1d', 'glEvalCoord2f', 'glEvalCoord2d', 'glEvalCoord1fv', 'glEvalCoord1dv', 'glEvalCoord2fv', 'glEvalCoord2dv', 'glMapGrid1f', 'glMapGrid1d', 'glMapGrid2f', 'glMapGrid2d', 'glEvalMesh1', 'glEvalMesh2', 'glEvalPoint1', 'glEvalPoint2']) d(n, () => this.warn('eval', 'glMap/glEvalCoord/glEvalMesh (Bézier evaluators) are not implemented — curved surfaces built with them are not drawn'));
    d('glLineStipple', () => this.warn('stipple', 'glLineStipple/glPolygonStipple: stipple patterns are not available (lines and polygons are drawn solid)')); d('glPolygonStipple', () => this.warn('stipple', 'glLineStipple/glPolygonStipple: stipple patterns are not available (lines and polygons are drawn solid)')); d('glGetPolygonStipple', () => {});
    d('glLogicOp', (a) => { if (U(a[0]) !== 0x1503 /* COPY */) this.warn('logicop', 'glLogicOp: logical pixel operations (XOR drawing etc.) are not available in WebGL2 — drawn as plain copies'); });
    d('glPixelTransferf', () => {}); d('glPixelTransferi', () => {}); d('glPixelMapfv', () => {}); d('glPixelMapuiv', () => {}); d('glPixelMapusv', () => {}); d('glGetPixelMapfv', () => {}); d('glGetPixelMapuiv', () => {}); d('glGetPixelMapusv', () => {});
    d('glPushAttrib', (a) => this.pushAttrib(U(a[0]))); d('glPopAttrib', () => this.popAttrib());
    // -- raster position, bitmaps, pixel rectangles --
    const rp = (x: number, y: number, z: number, w: number) => this.rasterPos(x, y, z, w);
    d('glRasterPos2f', (a) => rp(F(a[0]), F(a[1]), 0, 1)); d('glRasterPos3f', (a) => rp(F(a[0]), F(a[1]), F(a[2]), 1)); d('glRasterPos4f', (a) => rp(F(a[0]), F(a[1]), F(a[2]), F(a[3]))); d('glRasterPos2d', (a) => rp(D(a[0]), D(a[1]), 0, 1)); d('glRasterPos3d', (a) => rp(D(a[0]), D(a[1]), D(a[2]), 1)); d('glRasterPos4d', (a) => rp(D(a[0]), D(a[1]), D(a[2]), D(a[3])));
    d('glRasterPos2i', (a) => rp(I(a[0]), I(a[1]), 0, 1)); d('glRasterPos3i', (a) => rp(I(a[0]), I(a[1]), I(a[2]), 1)); d('glRasterPos4i', (a) => rp(I(a[0]), I(a[1]), I(a[2]), I(a[3]))); d('glRasterPos2s', (a) => rp(S16(a[0]), S16(a[1]), 0, 1)); d('glRasterPos3s', (a) => rp(S16(a[0]), S16(a[1]), S16(a[2]), 1));
    const rpv = (n: number, type: number) => (a: BigUint64Array) => { const v = this.readVec(a[0], n, type, true); if (v) rp(v[0], v[1], n > 2 ? v[2] : 0, n > 3 ? v[3] : 1); };
    d('glRasterPos2fv', rpv(2, E.FLOAT)); d('glRasterPos3fv', rpv(3, E.FLOAT)); d('glRasterPos4fv', rpv(4, E.FLOAT)); d('glRasterPos2dv', rpv(2, E.DOUBLE)); d('glRasterPos3dv', rpv(3, E.DOUBLE)); d('glRasterPos4dv', rpv(4, E.DOUBLE)); d('glRasterPos2iv', rpv(2, E.INT)); d('glRasterPos3iv', rpv(3, E.INT)); d('glRasterPos2sv', rpv(2, E.SHORT)); d('glRasterPos3sv', rpv(3, E.SHORT));
    d('glWindowPos2i', (a) => this.windowPos(I(a[0]), I(a[1]), 0)); d('glWindowPos2f', (a) => this.windowPos(F(a[0]), F(a[1]), 0)); d('glWindowPos3f', (a) => this.windowPos(F(a[0]), F(a[1]), F(a[2]))); d('glWindowPos2iARB', (a) => this.windowPos(I(a[0]), I(a[1]), 0)); d('glWindowPos2fARB', (a) => this.windowPos(F(a[0]), F(a[1]), 0));
    d('glPixelZoom', (a) => { this.zoomX = F(a[0]); this.zoomY = F(a[1]); });
    d('glBitmap', (a) => this.bitmap(I(a[0]), I(a[1]), F(a[2]), F(a[3]), F(a[4]), F(a[5]), a[6]));
    d('glDrawPixels', (a) => this.drawPixels(I(a[0]), I(a[1]), U(a[2]), U(a[3]), a[4]));
    d('glCopyPixels', (a) => this.copyPixels(I(a[0]), I(a[1]), I(a[2]), I(a[3]), U(a[4])));
    // -- GLU --
    d('gluPerspective', (a) => { const fovy = D(a[0]), aspect = D(a[1]), zn = D(a[2]), zf = D(a[3]); const f = 1 / Math.tan(fovy * Math.PI / 360); const m = new Float32Array(16); m[0] = f / aspect; m[5] = f; m[10] = (zf + zn) / (zn - zf); m[11] = -1; m[14] = 2 * zf * zn / (zn - zf); this.mulTop(m); });
    d('gluOrtho2D', (a) => this.ortho(D(a[0]), D(a[1]), D(a[2]), D(a[3]), -1, 1));
    d('gluLookAt', (a) => this.lookAt(D(a[0]), D(a[1]), D(a[2]), D(a[3]), D(a[4]), D(a[5]), D(a[6]), D(a[7]), D(a[8])));
    d('gluPickMatrix', (a) => { const x = D(a[0]), y = D(a[1]), w = D(a[2]), hh = D(a[3]); const vp = h.i32(a[4], 4); if (!vp || w <= 0 || hh <= 0) return; this.translate((vp[2] - 2 * (x - vp[0])) / w, (vp[3] - 2 * (y - vp[1])) / hh, 0); this.scale(vp[2] / w, vp[3] / hh, 1); });
    d('gluProject', (a) => { const mv = h.f64(a[3], 16), p = h.f64(a[4], 16), vp = h.i32(a[5], 4); if (!mv || !p || !vp) return 0; const v = xform(Float32Array.from(p), xform(Float32Array.from(mv), [D(a[0]), D(a[1]), D(a[2]), 1])); if (!v[3]) return 0; const x = v[0] / v[3], y = v[1] / v[3], z = v[2] / v[3]; h.wrf64(a[6], vp[0] + (x + 1) * vp[2] / 2); h.wrf64(a[7], vp[1] + (y + 1) * vp[3] / 2); h.wrf64(a[8], (z + 1) / 2); return 1; });
    d('gluUnProject', (a) => { const mv = h.f64(a[3], 16), p = h.f64(a[4], 16), vp = h.i32(a[5], 4); if (!mv || !p || !vp) return 0; const inv = invert(mul(Float32Array.from(p), Float32Array.from(mv))); if (!inv) return 0; const v = xform(inv, [(D(a[0]) - vp[0]) * 2 / vp[2] - 1, (D(a[1]) - vp[1]) * 2 / vp[3] - 1, D(a[2]) * 2 - 1, 1]); if (!v[3]) return 0; h.wrf64(a[6], v[0] / v[3]); h.wrf64(a[7], v[1] / v[3]); h.wrf64(a[8], v[2] / v[3]); return 1; });
    d('gluErrorString', (a) => { const e = U(a[0]); const s = e === 0 ? 'no error' : e === E.INVALID_ENUM ? 'invalid enumerant' : e === E.INVALID_VALUE ? 'invalid value' : e === E.INVALID_OPERATION ? 'invalid operation' : e === E.STACK_OVERFLOW ? 'stack overflow' : e === E.STACK_UNDERFLOW ? 'stack underflow' : e === E.OUT_OF_MEMORY ? 'out of memory' : e === 100900 ? 'invalid enumerant' : e === 100901 ? 'invalid value' : e === 100902 ? 'out of memory' : `unknown error ${e}`; return this.gluStr(e, s); });
    d('gluErrorUnicodeStringEXT', (a) => { const r = h.callImpl('gluErrorString', [a[0]]); return typeof r === 'bigint' ? r : 0n; });
    d('gluGetString', (a) => { const n = U(a[0]); if (n === 100800) return this.gluStr(-1, '1.2.2.0 Microsoft Corporation'); if (n === 100801) return this.gluStr(-2, 'GL_EXT_bgra '); return 0n; });
    d('gluCheckExtension', (a) => { const ext = h.rdstr(a[0]), list = h.rdstr(a[1]); return list.split(' ').includes(ext) ? 1 : 0; });
    d('gluBuild2DMipmaps', (a) => this.buildMipmaps(U(a[0]), I(a[1]), I(a[2]), I(a[3]), U(a[4]), U(a[5]), a[6]));
    d('gluBuild1DMipmaps', (a) => this.buildMipmaps(U(a[0]), I(a[1]), I(a[2]), 1, U(a[3]), U(a[4]), a[5]));
    d('gluScaleImage', (a) => this.scaleImage(U(a[0]), I(a[1]), I(a[2]), U(a[3]), a[4], I(a[5]), I(a[6]), U(a[7]), a[8]));
    d('gluNewQuadric', () => { const id = this.nextQuadric++; this.quadrics.set(id, { drawStyle: 100012, normals: 100000, texture: false, orientation: 100020 }); return BigInt(0x51000 + id * 16); });
    const q = (p: bigint) => this.quadrics.get((Number(p) - 0x51000) / 16) ?? null;
    d('gluDeleteQuadric', (a) => { this.quadrics.delete((Number(a[0]) - 0x51000) / 16); });
    d('gluQuadricDrawStyle', (a) => { const o = q(a[0]); if (o) o.drawStyle = U(a[1]); }); d('gluQuadricNormals', (a) => { const o = q(a[0]); if (o) o.normals = U(a[1]); }); d('gluQuadricTexture', (a) => { const o = q(a[0]); if (o) o.texture = U(a[1]) !== 0; }); d('gluQuadricOrientation', (a) => { const o = q(a[0]); if (o) o.orientation = U(a[1]); }); d('gluQuadricCallback', () => {});
    d('gluSphere', (a) => { const o = q(a[0]); if (o) this.sphere(o, D(a[1]), I(a[2]), I(a[3])); });
    d('gluCylinder', (a) => { const o = q(a[0]); if (o) this.cylinder(o, D(a[1]), D(a[2]), D(a[3]), I(a[4]), I(a[5])); });
    d('gluDisk', (a) => { const o = q(a[0]); if (o) this.disk(o, D(a[1]), D(a[2]), I(a[3]), I(a[4]), 0, 360); });
    d('gluPartialDisk', (a) => { const o = q(a[0]); if (o) this.disk(o, D(a[1]), D(a[2]), I(a[3]), I(a[4]), D(a[5]), D(a[6])); });
    for (const n of ['gluNewTess', 'gluNewNurbsRenderer']) d(n, () => { if (!this.tessWarned) { this.tessWarned = true; this.warn('glutess', 'GLU tessellator / NURBS: not available (their vertex callbacks would have to call back into the program) — gluNewTess returns NULL, so programs take their fallback path'); } return 0n; });
    for (const n of ['gluDeleteTess', 'gluTessBeginPolygon', 'gluTessBeginContour', 'gluTessEndContour', 'gluTessEndPolygon', 'gluTessVertex', 'gluTessCallback', 'gluTessNormal', 'gluTessProperty', 'gluGetTessProperty', 'gluBeginPolygon', 'gluEndPolygon', 'gluNextContour', 'gluDeleteNurbsRenderer', 'gluBeginCurve', 'gluEndCurve', 'gluBeginSurface', 'gluEndSurface', 'gluBeginTrim', 'gluEndTrim', 'gluNurbsCallback', 'gluNurbsCurve', 'gluNurbsProperty', 'gluGetNurbsProperty', 'gluNurbsSurface', 'gluPwlCurve', 'gluLoadSamplingMatrices']) d(n, () => {});
  }
  private gluStr(code: number, s: string): bigint { let p = this.gluStrings.get(code); if (!p) { p = this.h.gstr(s); this.gluStrings.set(code, p); } return p; }

  // ---------------- enables / getters used by the bridge ----------------
  /** true when the capability belongs to the fixed-function pipeline (handled here) */
  enable(cap: number, on: boolean): boolean {
    const unit = this.units[this.h.bound().unit & 3];
    switch (cap) {
      case E.TEXTURE_2D: unit.on2d = on; return true; case E.TEXTURE_1D: unit.on1d = on; return true;
      case E.LIGHTING: this.lighting = on; return true; case E.COLOR_MATERIAL: this.colorMaterial = on; this.ver.mat++; return true; case E.NORMALIZE: this.normalize = on; return true; case E.RESCALE_NORMAL: this.rescale = on; return true;
      case E.FOG: this.fog = on; return true; case E.ALPHA_TEST: this.alphaTest = on; return true;
      case E.TEXTURE_GEN_S: case E.TEXTURE_GEN_T: case E.TEXTURE_GEN_R: case E.TEXTURE_GEN_Q: { const c = cap - E.TEXTURE_GEN_S; unit.gen[c] = on ? (unit.gen[c] & 0xffff) | 0x10000 : unit.gen[c] & 0xffff; this.ver.gen++; return true; }
      case E.POINT_SMOOTH: case E.LINE_SMOOTH: case E.POLYGON_SMOOTH: case E.AUTO_NORMAL: case E.COLOR_SUM: case E.INDEX_LOGIC_OP: case E.POINT_SPRITE: return true;
      case E.LINE_STIPPLE: case E.POLYGON_STIPPLE: if (on) this.warn('stipple', 'glLineStipple/glPolygonStipple: stipple patterns are not available (lines and polygons are drawn solid)'); return true;
      case E.COLOR_LOGIC_OP: if (on) this.warn('logicop', 'GL_COLOR_LOGIC_OP: logical pixel operations are not available in WebGL2 — drawn as plain copies'); return true;
      case E.MAP1_VERTEX_3: case E.MAP2_VERTEX_3: if (on) this.warn('eval', 'glMap/glEvalCoord/glEvalMesh (Bézier evaluators) are not implemented — curved surfaces built with them are not drawn'); return true;
    }
    if (cap >= E.LIGHT0 && cap < E.LIGHT0 + 8) { this.lights[cap - E.LIGHT0].on = on; this.ver.light++; return true; }
    if (cap >= E.CLIP_PLANE0 && cap < E.CLIP_PLANE0 + 6) { const b = 1 << (cap - E.CLIP_PLANE0); this.clipOn = on ? this.clipOn | b : this.clipOn & ~b; this.ver.clip++; return true; }
    if (cap >= 0x0D90 && cap <= 0x0DBF) { if (on) this.warn('eval', 'glMap/glEvalCoord/glEvalMesh (Bézier evaluators) are not implemented'); return true; }   // GL_MAP1_*/GL_MAP2_*
    return false;
  }
  isEnabled(cap: number): number | undefined {
    const unit = this.units[this.h.bound().unit & 3];
    switch (cap) {
      case E.TEXTURE_2D: return unit.on2d ? 1 : 0; case E.TEXTURE_1D: return unit.on1d ? 1 : 0; case E.LIGHTING: return this.lighting ? 1 : 0; case E.COLOR_MATERIAL: return this.colorMaterial ? 1 : 0; case E.NORMALIZE: return this.normalize ? 1 : 0; case E.RESCALE_NORMAL: return this.rescale ? 1 : 0;
      case E.FOG: return this.fog ? 1 : 0; case E.ALPHA_TEST: return this.alphaTest ? 1 : 0; case E.TEXTURE_GEN_S: case E.TEXTURE_GEN_T: case E.TEXTURE_GEN_R: case E.TEXTURE_GEN_Q: return unit.gen[cap - E.TEXTURE_GEN_S] & 0x10000 ? 1 : 0;
      case E.VERTEX_ARRAY: return this.arr.vertex.on ? 1 : 0; case E.NORMAL_ARRAY: return this.arr.normal.on ? 1 : 0; case E.COLOR_ARRAY: return this.arr.color.on ? 1 : 0; case E.TEXTURE_COORD_ARRAY: return this.arr.tc[this.clientUnit].on ? 1 : 0;
      case E.POINT_SMOOTH: case E.LINE_SMOOTH: case E.POLYGON_SMOOTH: case E.LINE_STIPPLE: case E.POLYGON_STIPPLE: case E.COLOR_LOGIC_OP: case E.INDEX_LOGIC_OP: case E.AUTO_NORMAL: case E.INDEX_ARRAY: case E.EDGE_FLAG_ARRAY: return 0;
    }
    if (cap >= E.LIGHT0 && cap < E.LIGHT0 + 8) return this.lights[cap - E.LIGHT0].on ? 1 : 0;
    if (cap >= E.CLIP_PLANE0 && cap < E.CLIP_PLANE0 + 6) return (this.clipOn >> (cap - E.CLIP_PLANE0)) & 1;
    return undefined;
  }
  getParam(p: number): number | number[] | undefined {
    const unit = this.units[this.h.bound().unit & 3];
    switch (p) {
      case E.MODELVIEW_MATRIX: return Array.from(this.mv[this.mv.length - 1]); case E.PROJECTION_MATRIX: return Array.from(this.pr[this.pr.length - 1]); case E.TEXTURE_MATRIX: { const s = this.tx[this.h.bound().unit & 3]; return Array.from(s[s.length - 1]); }
      case E.MATRIX_MODE: return this.mode; case E.MODELVIEW_STACK_DEPTH: return this.mv.length; case E.PROJECTION_STACK_DEPTH: return this.pr.length; case E.TEXTURE_STACK_DEPTH: return this.tx[this.h.bound().unit & 3].length;
      case E.CURRENT_COLOR: return Array.from(this.color); case E.CURRENT_NORMAL: return Array.from(this.normal); case E.CURRENT_TEXTURE_COORDS: return Array.from(this.tc[0]); case E.CURRENT_INDEX: return 1;
      case E.CURRENT_RASTER_POSITION: return [this.raster.win[0], this.raster.win[1], this.raster.win[2], 1]; case E.CURRENT_RASTER_POSITION_VALID: return this.raster.valid ? 1 : 0; case E.CURRENT_RASTER_COLOR: return Array.from(this.raster.color); case E.CURRENT_RASTER_TEXTURE_COORDS: return [0, 0, 0, 1];
      case E.LIGHT_MODEL_AMBIENT: return Array.from(this.lmAmbient); case E.LIGHT_MODEL_LOCAL_VIEWER: return this.localViewer ? 1 : 0; case E.LIGHT_MODEL_TWO_SIDE: return this.twoSide ? 1 : 0; case E.COLOR_MATERIAL_FACE: return this.cmFace; case E.COLOR_MATERIAL_PARAMETER: return this.cmMode;
      case E.FOG_MODE: return this.fogMode; case E.FOG_DENSITY: return this.fogDensity; case E.FOG_START: return this.fogStart; case E.FOG_END: return this.fogEnd; case E.FOG_COLOR: return Array.from(this.fogColor); case E.FOG_INDEX: return 0;
      case E.ALPHA_TEST_FUNC: return this.alphaFunc; case E.ALPHA_TEST_REF: return this.alphaRef; case E.SHADE_MODEL: return this.shade; case E.POLYGON_MODE: return [this.polyFront, this.polyBack]; case E.POINT_SIZE: return this.pointSize;
      case E.RENDER_MODE: return this.renderMode; case E.LIST_BASE: return this.listBase; case E.LIST_INDEX: return this.rec ? this.recId : 0; case E.LIST_MODE: return this.rec ? this.recMode : 0; case E.ZOOM_X: return this.zoomX; case E.ZOOM_Y: return this.zoomY;
      case E.CLIENT_ACTIVE_TEXTURE: return E.TEXTURE0 + this.clientUnit; case E.ACTIVE_TEXTURE: return E.TEXTURE0 + this.h.bound().unit;
      case E.VERTEX_ARRAY: return this.arr.vertex.on ? 1 : 0; case E.NORMAL_ARRAY: return this.arr.normal.on ? 1 : 0; case E.COLOR_ARRAY: return this.arr.color.on ? 1 : 0; case E.TEXTURE_COORD_ARRAY: return this.arr.tc[this.clientUnit].on ? 1 : 0; case E.INDEX_ARRAY: case E.EDGE_FLAG_ARRAY: return 0;
      case E.VERTEX_ARRAY_SIZE: return this.arr.vertex.size; case E.VERTEX_ARRAY_TYPE: return this.arr.vertex.type; case E.VERTEX_ARRAY_STRIDE: return this.arr.vertex.stride; case E.NORMAL_ARRAY_TYPE: return this.arr.normal.type; case E.NORMAL_ARRAY_STRIDE: return this.arr.normal.stride;
      case E.COLOR_ARRAY_SIZE: return this.arr.color.size; case E.COLOR_ARRAY_TYPE: return this.arr.color.type; case E.COLOR_ARRAY_STRIDE: return this.arr.color.stride; case E.TEXTURE_COORD_ARRAY_SIZE: return this.arr.tc[this.clientUnit].size; case E.TEXTURE_COORD_ARRAY_TYPE: return this.arr.tc[this.clientUnit].type; case E.TEXTURE_COORD_ARRAY_STRIDE: return this.arr.tc[this.clientUnit].stride;
      case E.EDGE_FLAG: return 1; case 0x0D34 /* MAX_PIXEL_MAP_TABLE */: return 256; case 0x0D35 /* MAX_NAME_STACK_DEPTH */: return 64; case 0x0D30 /* MAX_LIST_NESTING */: return 64; case 0x0D37 /* MAX_EVAL_ORDER */: return 8; case 0x0D3B /* MAX_CLIENT_ATTRIB_STACK_DEPTH */: return 16;
    }
    void unit; return undefined;
  }
  private readVec(p: bigint, n: number, type: number, raw = false): number[] | null {
    const h = this.h;
    switch (type) {
      case E.FLOAT: { const v = h.f32(p, n); return v ? Array.from(v) : null; } case E.DOUBLE: { const v = h.f64(p, n); return v ? Array.from(v) : null; }
      case E.INT: { const v = h.i32(p, n); return v ? Array.from(v, (x) => raw ? x : Math.max(-1, x / 2147483647)) : null; } case E.UNSIGNED_INT: { const v = h.i32(p, n); return v ? Array.from(v, (x) => raw ? x >>> 0 : (x >>> 0) / 4294967295) : null; }
      case E.SHORT: { const b = h.u8(p, n * 2); if (!b) return null; const v = new Int16Array(b.slice().buffer); return Array.from(v, (x) => raw ? x : Math.max(-1, x / 32767)); } case E.UNSIGNED_SHORT: { const b = h.u8(p, n * 2); if (!b) return null; const v = new Uint16Array(b.slice().buffer); return Array.from(v, (x) => raw ? x : x / 65535); }
      case E.BYTE: { const b = h.u8(p, n); return b ? Array.from(b, (x) => raw ? (x << 24) >> 24 : Math.max(-1, ((x << 24) >> 24) / 127)) : null; } case E.UNSIGNED_BYTE: { const b = h.u8(p, n); return b ? Array.from(b, (x) => raw ? x : x / 255) : null; }
    }
    return null; }

  // ---------------- matrix helpers ----------------
  private translate(x: number, y: number, z: number) { const m = identity(); m[12] = x; m[13] = y; m[14] = z; this.mulTop(m); }
  private scale(x: number, y: number, z: number) { const m = identity(); m[0] = x; m[5] = y; m[10] = z; this.mulTop(m); }
  private rotate(deg: number, x: number, y: number, z: number) {
    const l = Math.hypot(x, y, z); if (!l) return; x /= l; y /= l; z /= l; const r = deg * Math.PI / 180, c = Math.cos(r), s = Math.sin(r), t = 1 - c; const m = identity();
    m[0] = t * x * x + c; m[1] = t * x * y + s * z; m[2] = t * x * z - s * y; m[4] = t * x * y - s * z; m[5] = t * y * y + c; m[6] = t * y * z + s * x; m[8] = t * x * z + s * y; m[9] = t * y * z - s * x; m[10] = t * z * z + c; this.mulTop(m); }
  private ortho(l: number, r: number, b: number, t: number, n: number, f: number) { if (l === r || b === t || n === f) { this.err(E.INVALID_VALUE); return; } const m = identity(); m[0] = 2 / (r - l); m[5] = 2 / (t - b); m[10] = -2 / (f - n); m[12] = -(r + l) / (r - l); m[13] = -(t + b) / (t - b); m[14] = -(f + n) / (f - n); this.mulTop(m); }
  private frustum(l: number, r: number, b: number, t: number, n: number, f: number) { if (n <= 0 || f <= 0 || l === r || b === t || n === f) { this.err(E.INVALID_VALUE); return; } const m = new Float32Array(16); m[0] = 2 * n / (r - l); m[5] = 2 * n / (t - b); m[8] = (r + l) / (r - l); m[9] = (t + b) / (t - b); m[10] = -(f + n) / (f - n); m[11] = -1; m[14] = -2 * f * n / (f - n); this.mulTop(m); }
  private lookAt(ex: number, ey: number, ez: number, cx: number, cy: number, cz: number, ux: number, uy: number, uz: number) {
    let fx = cx - ex, fy = cy - ey, fz = cz - ez; const fl = Math.hypot(fx, fy, fz) || 1; fx /= fl; fy /= fl; fz /= fl;
    let sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux; const sl = Math.hypot(sx, sy, sz) || 1; sx /= sl; sy /= sl; sz /= sl;
    const vx = sy * fz - sz * fy, vy = sz * fx - sx * fz, vz = sx * fy - sy * fx;
    const m = identity(); m[0] = sx; m[4] = sy; m[8] = sz; m[1] = vx; m[5] = vy; m[9] = vz; m[2] = -fx; m[6] = -fy; m[10] = -fz; this.mulTop(m); this.translate(-ex, -ey, -ez); }

  // ---------------- lighting / material / fog / texenv / texgen state ----------------
  private lightModel(p: number, v: number[]) { switch (p) { case E.LIGHT_MODEL_AMBIENT: this.lmAmbient.set(v.slice(0, 4)); break; case E.LIGHT_MODEL_LOCAL_VIEWER: this.localViewer = v[0] !== 0; break; case E.LIGHT_MODEL_TWO_SIDE: this.twoSide = v[0] !== 0; break; case E.LIGHT_MODEL_COLOR_CONTROL: break; default: this.err(E.INVALID_ENUM); return; } this.ver.light++; }
  private light(l: number, p: number, v: number[]) {
    const i = l - E.LIGHT0; if (i < 0 || i >= 8) { this.err(E.INVALID_ENUM); return; } const L = this.lights[i]; const mv = this.mv[this.mv.length - 1];
    switch (p) {
      case E.AMBIENT: L.amb.set(v.slice(0, 4)); break; case E.DIFFUSE: L.dif.set(v.slice(0, 4)); break; case E.SPECULAR: L.spec.set(v.slice(0, 4)); break;
      case E.POSITION: L.pos = xform(mv, [v[0], v[1], v[2], v[3]]); break;
      case E.SPOT_DIRECTION: { const d = xform(mv, [v[0], v[1], v[2], 0]); L.spotDir.set([d[0], d[1], d[2]]); break; }
      case E.SPOT_EXPONENT: L.spotExp = v[0]; break; case E.SPOT_CUTOFF: L.spotCut = v[0]; break; case E.CONSTANT_ATTENUATION: L.att[0] = v[0]; break; case E.LINEAR_ATTENUATION: L.att[1] = v[0]; break; case E.QUADRATIC_ATTENUATION: L.att[2] = v[0]; break;
      default: this.err(E.INVALID_ENUM); return; }
    this.ver.light++; }
  private getLight(l: number, p: number): ArrayLike<number> | null { const i = l - E.LIGHT0; if (i < 0 || i >= 8) { this.err(E.INVALID_ENUM); return null; } const L = this.lights[i]; switch (p) { case E.AMBIENT: return L.amb; case E.DIFFUSE: return L.dif; case E.SPECULAR: return L.spec; case E.POSITION: return L.pos; case E.SPOT_DIRECTION: return L.spotDir; case E.SPOT_EXPONENT: return [L.spotExp]; case E.SPOT_CUTOFF: return [L.spotCut]; case E.CONSTANT_ATTENUATION: return [L.att[0]]; case E.LINEAR_ATTENUATION: return [L.att[1]]; case E.QUADRATIC_ATTENUATION: return [L.att[2]]; } this.err(E.INVALID_ENUM); return null; }
  private material(face: number, p: number, v: number[]) {
    if (face !== E.FRONT && face !== E.BACK && face !== E.FRONT_AND_BACK) { this.err(E.INVALID_ENUM); return; }
    switch (p) { case E.AMBIENT: this.mat.amb.set(v.slice(0, 4)); break; case E.DIFFUSE: this.mat.dif.set(v.slice(0, 4)); break; case E.AMBIENT_AND_DIFFUSE: this.mat.amb.set(v.slice(0, 4)); this.mat.dif.set(v.slice(0, 4)); break; case E.SPECULAR: this.mat.spec.set(v.slice(0, 4)); break; case E.EMISSION: this.mat.emis.set(v.slice(0, 4)); break; case E.SHININESS: this.mat.shin = Math.min(128, Math.max(0, v[0])); break; case E.COLOR_INDEXES: break; default: this.err(E.INVALID_ENUM); return; }
    this.ver.mat++; }
  private fogParam(p: number, v: number[]) { switch (p) { case E.FOG_MODE: if (v[0] !== E.LINEAR && v[0] !== E.EXP && v[0] !== E.EXP2) { this.err(E.INVALID_ENUM); return; } this.fogMode = v[0]; break; case E.FOG_DENSITY: if (v[0] < 0) { this.err(E.INVALID_VALUE); return; } this.fogDensity = v[0]; break; case E.FOG_START: this.fogStart = v[0]; break; case E.FOG_END: this.fogEnd = v[0]; break; case E.FOG_COLOR: this.fogColor.set(v.slice(0, 4)); break; case E.FOG_INDEX: case E.FOG_COORD_SRC: case E.FOG_HINT: break; default: this.err(E.INVALID_ENUM); return; } this.ver.fog++; }
  private texEnv(target: number, p: number, v: number[]) {
    const u = this.units[this.h.bound().unit & 3];
    if (target === E.TEXTURE_FILTER_CONTROL) { if (p === E.TEXTURE_LOD_BIAS) u.lodBias = v[0]; return; }
    if (target === E.POINT_SPRITE) return;
    if (target !== E.TEXTURE_ENV) { this.err(E.INVALID_ENUM); return; }
    switch (p) {
      case E.TEXTURE_ENV_MODE: if (![E.MODULATE, E.DECAL, E.BLEND, E.REPLACE, E.ADD, E.COMBINE].includes(v[0] as never)) { this.err(E.INVALID_ENUM); return; } u.env = v[0]; break;
      case E.TEXTURE_ENV_COLOR: u.envColor.set(v.slice(0, 4)); break;
      case E.COMBINE_RGB: u.comb[0] = v[0]; break; case E.COMBINE_ALPHA: u.comb[1] = v[0]; break; case E.RGB_SCALE: u.scaleRgb = v[0]; break; case E.ALPHA_SCALE: u.scaleA = v[0]; break;
      default:
        if (p >= E.SOURCE0_RGB && p < E.SOURCE0_RGB + 3) u.comb[2 + (p - E.SOURCE0_RGB)] = v[0]; else if (p >= E.OPERAND0_RGB && p < E.OPERAND0_RGB + 3) u.comb[5 + (p - E.OPERAND0_RGB)] = v[0];
        else if (p >= E.SOURCE0_ALPHA && p < E.SOURCE0_ALPHA + 3) u.comb[8 + (p - E.SOURCE0_ALPHA)] = v[0]; else if (p >= E.OPERAND0_ALPHA && p < E.OPERAND0_ALPHA + 3) u.comb[11 + (p - E.OPERAND0_ALPHA)] = v[0];
        else { this.err(E.INVALID_ENUM); return; } }
    this.ver.env++; }
  private getTexEnv(target: number, p: number): number[] | null { const u = this.units[this.h.bound().unit & 3]; if (target === E.TEXTURE_FILTER_CONTROL) return [u.lodBias]; if (target !== E.TEXTURE_ENV) { this.err(E.INVALID_ENUM); return null; } switch (p) { case E.TEXTURE_ENV_MODE: return [u.env]; case E.TEXTURE_ENV_COLOR: return Array.from(u.envColor); case E.COMBINE_RGB: return [u.comb[0]]; case E.COMBINE_ALPHA: return [u.comb[1]]; case E.RGB_SCALE: return [u.scaleRgb]; case E.ALPHA_SCALE: return [u.scaleA]; } if (p >= E.SOURCE0_RGB && p < E.SOURCE0_RGB + 3) return [u.comb[2 + p - E.SOURCE0_RGB]]; if (p >= E.OPERAND0_RGB && p < E.OPERAND0_RGB + 3) return [u.comb[5 + p - E.OPERAND0_RGB]]; if (p >= E.SOURCE0_ALPHA && p < E.SOURCE0_ALPHA + 3) return [u.comb[8 + p - E.SOURCE0_ALPHA]]; if (p >= E.OPERAND0_ALPHA && p < E.OPERAND0_ALPHA + 3) return [u.comb[11 + p - E.OPERAND0_ALPHA]]; this.err(E.INVALID_ENUM); return null; }
  private texGen(coord: number, p: number, v: number[]) {
    const c = coord - E.S; if (c < 0 || c > 3) { this.err(E.INVALID_ENUM); return; } const u = this.units[this.h.bound().unit & 3];
    switch (p) {
      case E.TEXTURE_GEN_MODE: if (![E.EYE_LINEAR, E.OBJECT_LINEAR, E.SPHERE_MAP, E.NORMAL_MAP, E.REFLECTION_MAP].includes(v[0] as never)) { this.err(E.INVALID_ENUM); return; } u.gen[c] = (u.gen[c] & 0x10000) | v[0]; break;
      case E.OBJECT_PLANE: u.genObj[c].set(v.slice(0, 4)); break;
      case E.EYE_PLANE: { const inv = invert(this.mv[this.mv.length - 1]); const pl = Float32Array.from(v.slice(0, 4)); u.genEye[c] = inv ? xformT(inv, pl) : pl; break; }
      default: this.err(E.INVALID_ENUM); return; }
    this.ver.gen++; }
  private getTexGen(coord: number, p: number): number[] | null { const c = coord - E.S; if (c < 0 || c > 3) { this.err(E.INVALID_ENUM); return null; } const u = this.units[this.h.bound().unit & 3]; switch (p) { case E.TEXTURE_GEN_MODE: return [(u.gen[c] & 0xffff) || E.EYE_LINEAR]; case E.OBJECT_PLANE: return Array.from(u.genObj[c]); case E.EYE_PLANE: return Array.from(u.genEye[c]); } this.err(E.INVALID_ENUM); return null; }
  private clientState(cap: number, on: boolean) { switch (cap) { case E.VERTEX_ARRAY: this.arr.vertex.on = on; break; case E.NORMAL_ARRAY: this.arr.normal.on = on; break; case E.COLOR_ARRAY: this.arr.color.on = on; break; case E.TEXTURE_COORD_ARRAY: this.arr.tc[this.clientUnit].on = on; break; case E.INDEX_ARRAY: case E.EDGE_FLAG_ARRAY: case E.SECONDARY_COLOR_ARRAY: case E.FOG_COORD_ARRAY: break; default: this.err(E.INVALID_ENUM); } }
  private interleaved(fmt: number, stride: number, p: bigint) {
    // GL_V2F … GL_T4F_C4F_N3F_V4F: enable/point the arrays the format names
    const f = 4; let off = 0; const tc = { n: 0, o: 0 }, col: { n: number; type: number; o: number } = { n: 0, type: E.FLOAT, o: 0 }, nrm = { on: false, o: 0 }, vtx = { n: 0, o: 0 };
    switch (fmt) {
      case 0x2A20: vtx.n = 2; break; case 0x2A21: vtx.n = 3; break; case 0x2A22: col.n = 4; col.type = E.UNSIGNED_BYTE; col.o = 0; off = 4; vtx.n = 2; vtx.o = off; break; case 0x2A23: col.n = 4; col.type = E.UNSIGNED_BYTE; off = 4; vtx.n = 3; vtx.o = off; break;
      case 0x2A24: col.n = 3; off = 12; vtx.n = 3; vtx.o = off; break; case 0x2A25: nrm.on = true; off = 12; vtx.n = 3; vtx.o = off; break; case 0x2A26: col.n = 4; off = 16; nrm.on = true; nrm.o = off; off += 12; vtx.n = 3; vtx.o = off; break;
      case 0x2A27: tc.n = 2; off = 8; vtx.n = 3; vtx.o = off; break; case 0x2A28: tc.n = 4; off = 16; vtx.n = 4; vtx.o = off; break; case 0x2A29: tc.n = 2; off = 8; col.n = 4; col.type = E.UNSIGNED_BYTE; col.o = off; off += 4; vtx.n = 3; vtx.o = off; break;
      case 0x2A2A: tc.n = 2; off = 8; col.n = 3; col.o = off; off += 12; vtx.n = 3; vtx.o = off; break; case 0x2A2B: tc.n = 2; off = 8; nrm.on = true; nrm.o = off; off += 12; vtx.n = 3; vtx.o = off; break;
      case 0x2A2C: tc.n = 2; off = 8; col.n = 4; col.o = off; off += 16; nrm.on = true; nrm.o = off; off += 12; vtx.n = 3; vtx.o = off; break; case 0x2A2D: tc.n = 4; off = 16; col.n = 4; col.o = off; off += 16; nrm.on = true; nrm.o = off; off += 12; vtx.n = 4; vtx.o = off; break;
      default: this.err(E.INVALID_ENUM); return; }
    const total = off + vtx.n * f; const st = stride || total; const b = this.h.bound().array;
    this.arr.tc[this.clientUnit].on = tc.n > 0; if (tc.n) Object.assign(this.arr.tc[this.clientUnit], { size: tc.n, type: E.FLOAT, stride: st, ptr: p + BigInt(tc.o), buffer: b });
    this.arr.color.on = col.n > 0; if (col.n) Object.assign(this.arr.color, { size: col.n, type: col.type, stride: st, ptr: p + BigInt(col.o), buffer: b });
    this.arr.normal.on = nrm.on; if (nrm.on) Object.assign(this.arr.normal, { size: 3, type: E.FLOAT, stride: st, ptr: p + BigInt(nrm.o), buffer: b });
    this.arr.vertex.on = true; Object.assign(this.arr.vertex, { size: vtx.n, type: E.FLOAT, stride: st, ptr: p + BigInt(vtx.o), buffer: b }); }

  // ---------------- attribute stack ----------------
  private pushAttrib(mask: number) {
    if (this.attribStack.length >= 16) { this.err(E.STACK_OVERFLOW); return; }
    const gl = this.h.gl(); const st: Record<string, unknown> = {};
    const cl = <T>(x: T): T => structuredClone(x);
    if (mask & E.CURRENT_BIT) { st.color = cl(this.color); st.normal = cl(this.normal); st.tc = cl(this.tc); st.raster = cl(this.raster); }
    if (mask & E.LIGHTING_BIT) { st.lighting = this.lighting; st.lights = cl(this.lights); st.colorMaterial = this.colorMaterial; st.cmFace = this.cmFace; st.cmMode = this.cmMode; st.lmAmbient = cl(this.lmAmbient); st.localViewer = this.localViewer; st.twoSide = this.twoSide; st.mat = cl(this.mat); st.shade = this.shade; }
    if (mask & E.FOG_BIT) { st.fog = this.fog; st.fogMode = this.fogMode; st.fogDensity = this.fogDensity; st.fogStart = this.fogStart; st.fogEnd = this.fogEnd; st.fogColor = cl(this.fogColor); }
    if (mask & E.COLOR_BUFFER_BIT) { st.alphaTest = this.alphaTest; st.alphaFunc = this.alphaFunc; st.alphaRef = this.alphaRef; st.blend = gl.isEnabled(gl.BLEND); st.blendSrc = gl.getParameter(gl.BLEND_SRC_RGB); st.blendDst = gl.getParameter(gl.BLEND_DST_RGB); st.blendSrcA = gl.getParameter(gl.BLEND_SRC_ALPHA); st.blendDstA = gl.getParameter(gl.BLEND_DST_ALPHA); st.blendEq = gl.getParameter(gl.BLEND_EQUATION_RGB); st.blendEqA = gl.getParameter(gl.BLEND_EQUATION_ALPHA); st.colorMask = gl.getParameter(gl.COLOR_WRITEMASK); st.clearColor = gl.getParameter(gl.COLOR_CLEAR_VALUE); st.dither = gl.isEnabled(gl.DITHER); }
    if (mask & E.DEPTH_BUFFER_BIT) { st.depthTest = gl.isEnabled(gl.DEPTH_TEST); st.depthFunc = gl.getParameter(gl.DEPTH_FUNC); st.depthMask = gl.getParameter(gl.DEPTH_WRITEMASK); st.clearDepth = gl.getParameter(gl.DEPTH_CLEAR_VALUE); }
    if (mask & E.STENCIL_BUFFER_BIT) { st.stencilTest = gl.isEnabled(gl.STENCIL_TEST); st.stencilFunc = gl.getParameter(gl.STENCIL_FUNC); st.stencilRef = gl.getParameter(gl.STENCIL_REF); st.stencilVMask = gl.getParameter(gl.STENCIL_VALUE_MASK); st.stencilMask = gl.getParameter(gl.STENCIL_WRITEMASK); st.sfail = gl.getParameter(gl.STENCIL_FAIL); st.dpfail = gl.getParameter(gl.STENCIL_PASS_DEPTH_FAIL); st.dppass = gl.getParameter(gl.STENCIL_PASS_DEPTH_PASS); st.clearStencil = gl.getParameter(gl.STENCIL_CLEAR_VALUE); }
    if (mask & E.VIEWPORT_BIT) { st.viewport = gl.getParameter(gl.VIEWPORT); st.depthRange = gl.getParameter(gl.DEPTH_RANGE); }
    if (mask & E.SCISSOR_BIT) { st.scissorTest = gl.isEnabled(gl.SCISSOR_TEST); st.scissor = gl.getParameter(gl.SCISSOR_BOX); }
    if (mask & E.POLYGON_BIT) { st.polyFront = this.polyFront; st.polyBack = this.polyBack; st.cull = gl.isEnabled(gl.CULL_FACE); st.cullFace = gl.getParameter(gl.CULL_FACE_MODE); st.frontFace = gl.getParameter(gl.FRONT_FACE); st.polyOffsetFill = gl.isEnabled(gl.POLYGON_OFFSET_FILL); st.polyOffset = [gl.getParameter(gl.POLYGON_OFFSET_FACTOR), gl.getParameter(gl.POLYGON_OFFSET_UNITS)]; }
    if (mask & E.LINE_BIT) st.lineWidth = gl.getParameter(gl.LINE_WIDTH); if (mask & E.POINT_BIT) st.pointSize = this.pointSize;
    if (mask & E.TEXTURE_BIT) { st.units = cl(this.units.map((u) => ({ ...u, genObj: u.genObj.map((g) => new Float32Array(g)), genEye: u.genEye.map((g) => new Float32Array(g)) }))); st.texBind = this.units.map((_, i) => [this.h.texIdOn(i, E.TEXTURE_2D), this.h.texIdOn(i, 0x8513), this.h.texIdOn(i, 0x806F)]); st.activeUnit = this.h.bound().unit; }
    if (mask & E.TRANSFORM_BIT) { st.matrixMode = this.mode; st.normalize = this.normalize; st.rescale = this.rescale; st.clipOn = this.clipOn; st.clip = this.clip.map((c) => c ? new Float32Array(c) : null); }
    if (mask & E.ENABLE_BIT) { st.en = { lighting: this.lighting, lightsOn: this.lights.map((l) => l.on), colorMaterial: this.colorMaterial, normalize: this.normalize, rescale: this.rescale, fog: this.fog, alphaTest: this.alphaTest, clipOn: this.clipOn, tex: this.units.map((u) => [u.on2d, u.on1d, Array.from(u.gen)]), blend: gl.isEnabled(gl.BLEND), depth: gl.isEnabled(gl.DEPTH_TEST), stencil: gl.isEnabled(gl.STENCIL_TEST), cull: gl.isEnabled(gl.CULL_FACE), scissor: gl.isEnabled(gl.SCISSOR_TEST), dither: gl.isEnabled(gl.DITHER), poFill: gl.isEnabled(gl.POLYGON_OFFSET_FILL) }; }
    if (mask & E.LIST_BIT) st.listBase = this.listBase;
    if (mask & E.PIXEL_MODE_BIT) { st.zoomX = this.zoomX; st.zoomY = this.zoomY; }
    this.attribStack.push({ mask, state: st }); }
  private popAttrib() {
    const e = this.attribStack.pop(); if (!e) { this.err(E.STACK_UNDERFLOW); return; } const st = e.state, mask = e.mask, gl = this.h.gl(); const en = (cap: number, on: boolean) => on ? gl.enable(cap) : gl.disable(cap);
    if (mask & E.CURRENT_BIT) { this.color = st.color as Float32Array; this.normal = st.normal as Float32Array; this.tc = st.tc as Float32Array[]; this.raster = st.raster as typeof this.raster; }
    if (mask & E.LIGHTING_BIT) { this.lighting = st.lighting as boolean; this.lights = st.lights as Light[]; this.colorMaterial = st.colorMaterial as boolean; this.cmFace = st.cmFace as number; this.cmMode = st.cmMode as number; this.lmAmbient = st.lmAmbient as Float32Array; this.localViewer = st.localViewer as boolean; this.twoSide = st.twoSide as boolean; this.mat = st.mat as typeof this.mat; this.shade = st.shade as number; this.ver.light++; this.ver.mat++; }
    if (mask & E.FOG_BIT) { this.fog = st.fog as boolean; this.fogMode = st.fogMode as number; this.fogDensity = st.fogDensity as number; this.fogStart = st.fogStart as number; this.fogEnd = st.fogEnd as number; this.fogColor = st.fogColor as Float32Array; this.ver.fog++; }
    if (mask & E.COLOR_BUFFER_BIT) { this.alphaTest = st.alphaTest as boolean; this.alphaFunc = st.alphaFunc as number; this.alphaRef = st.alphaRef as number; this.ver.misc++; en(gl.BLEND, st.blend as boolean); gl.blendFuncSeparate(st.blendSrc as number, st.blendDst as number, st.blendSrcA as number, st.blendDstA as number); gl.blendEquationSeparate(st.blendEq as number, st.blendEqA as number); const cm = st.colorMask as boolean[]; gl.colorMask(cm[0], cm[1], cm[2], cm[3]); const cc = st.clearColor as Float32Array; gl.clearColor(cc[0], cc[1], cc[2], cc[3]); en(gl.DITHER, st.dither as boolean); }
    if (mask & E.DEPTH_BUFFER_BIT) { en(gl.DEPTH_TEST, st.depthTest as boolean); gl.depthFunc(st.depthFunc as number); gl.depthMask(st.depthMask as boolean); gl.clearDepth(st.clearDepth as number); }
    if (mask & E.STENCIL_BUFFER_BIT) { en(gl.STENCIL_TEST, st.stencilTest as boolean); gl.stencilFunc(st.stencilFunc as number, st.stencilRef as number, st.stencilVMask as number); gl.stencilMask(st.stencilMask as number); gl.stencilOp(st.sfail as number, st.dpfail as number, st.dppass as number); gl.clearStencil(st.clearStencil as number); }
    if (mask & E.VIEWPORT_BIT) { const v = st.viewport as Int32Array; gl.viewport(v[0], v[1], v[2], v[3]); const dr = st.depthRange as Float32Array; gl.depthRange(dr[0], dr[1]); }
    if (mask & E.SCISSOR_BIT) { en(gl.SCISSOR_TEST, st.scissorTest as boolean); const s = st.scissor as Int32Array; gl.scissor(s[0], s[1], s[2], s[3]); }
    if (mask & E.POLYGON_BIT) { this.polyFront = st.polyFront as number; this.polyBack = st.polyBack as number; en(gl.CULL_FACE, st.cull as boolean); gl.cullFace(st.cullFace as number); gl.frontFace(st.frontFace as number); en(gl.POLYGON_OFFSET_FILL, st.polyOffsetFill as boolean); const po = st.polyOffset as number[]; gl.polygonOffset(po[0], po[1]); }
    if (mask & E.LINE_BIT) gl.lineWidth(st.lineWidth as number); if (mask & E.POINT_BIT) this.pointSize = st.pointSize as number;
    if (mask & E.TEXTURE_BIT) { this.units = st.units as TexUnit[]; const tb = st.texBind as number[][]; for (let u = 0; u < tb.length; u++) { this.h.callImpl('glActiveTexture', [BigInt(E.TEXTURE0 + u)]); this.h.callImpl('glBindTexture', [BigInt(E.TEXTURE_2D), BigInt(tb[u][0])]); if (tb[u][1]) this.h.callImpl('glBindTexture', [0x8513n, BigInt(tb[u][1])]); if (tb[u][2]) this.h.callImpl('glBindTexture', [0x806Fn, BigInt(tb[u][2])]); } this.h.callImpl('glActiveTexture', [BigInt(E.TEXTURE0 + (st.activeUnit as number))]); this.ver.env++; this.ver.gen++; }
    if (mask & E.TRANSFORM_BIT) { this.mode = st.matrixMode as number; this.normalize = st.normalize as boolean; this.rescale = st.rescale as boolean; this.clipOn = st.clipOn as number; this.clip = st.clip as (Float32Array | null)[]; this.ver.clip++; }
    if (mask & E.ENABLE_BIT) { const s = st.en as { lighting: boolean; lightsOn: boolean[]; colorMaterial: boolean; normalize: boolean; rescale: boolean; fog: boolean; alphaTest: boolean; clipOn: number; tex: [boolean, boolean, number[]][]; blend: boolean; depth: boolean; stencil: boolean; cull: boolean; scissor: boolean; dither: boolean; poFill: boolean };
      this.lighting = s.lighting; s.lightsOn.forEach((on, i) => { this.lights[i].on = on; }); this.colorMaterial = s.colorMaterial; this.normalize = s.normalize; this.rescale = s.rescale; this.fog = s.fog; this.alphaTest = s.alphaTest; this.clipOn = s.clipOn; s.tex.forEach((t, i) => { this.units[i].on2d = t[0]; this.units[i].on1d = t[1]; this.units[i].gen.set(t[2]); });
      en(gl.BLEND, s.blend); en(gl.DEPTH_TEST, s.depth); en(gl.STENCIL_TEST, s.stencil); en(gl.CULL_FACE, s.cull); en(gl.SCISSOR_TEST, s.scissor); en(gl.DITHER, s.dither); en(gl.POLYGON_OFFSET_FILL, s.poFill); this.ver.light++; this.ver.mat++; this.ver.clip++; this.ver.gen++; }
    if (mask & E.LIST_BIT) this.listBase = st.listBase as number;
    if (mask & E.PIXEL_MODE_BIT) { this.zoomX = st.zoomX as number; this.zoomY = st.zoomY as number; } }

  // ---------------- immediate mode ----------------
  private begin(mode: number) {
    if (this.inBegin) { this.err(E.INVALID_OPERATION); return; } if (mode > E.POLYGON) { this.err(E.INVALID_ENUM); return; }
    this.inBegin = true; this.prim = mode; this.nv = 0; this.setMask = this.rec ? this.recSet & 15 : 15; }
  private vertex(x: number, y: number, z: number, w: number) {
    if (!this.inBegin) { this.err(E.INVALID_OPERATION); return; }
    if ((this.nv + 1) * VSTRIDE > this.verts.length) { const n = new Float32Array(this.verts.length * 2); n.set(this.verts); this.verts = n; }
    const o = this.nv * VSTRIDE, v = this.verts; v[o] = x; v[o + 1] = y; v[o + 2] = z; v[o + 3] = w; v[o + 4] = this.color[0]; v[o + 5] = this.color[1]; v[o + 6] = this.color[2]; v[o + 7] = this.color[3];
    v[o + 8] = this.normal[0]; v[o + 9] = this.normal[1]; v[o + 10] = this.normal[2]; const t0 = this.tc[0], t1 = this.tc[1]; v[o + 11] = t0[0]; v[o + 12] = t0[1]; v[o + 13] = t0[2]; v[o + 14] = t0[3]; v[o + 15] = t1[0]; v[o + 16] = t1[1]; v[o + 17] = t1[2]; v[o + 18] = t1[3]; this.nv++; }
  private end() {
    if (!this.inBegin) { this.err(E.INVALID_OPERATION); return; } this.inBegin = false; if (!this.nv) return;
    if (this.rec) { const fromCurrent = ~(this.recSet | this.setMask) & 15; this.rec.push({ kind: 'batch', mode: this.prim, verts: this.verts.slice(0, this.nv * VSTRIDE), n: this.nv, fromCurrent }); this.recSet |= this.setMask; if (this.recMode !== E.COMPILE_AND_EXECUTE) return; }
    this.drawBatch(this.prim, this.verts, this.nv, 0); }
  private rect(x1: number, y1: number, x2: number, y2: number) { if (this.inBegin) { this.err(E.INVALID_OPERATION); return; } this.begin(E.QUADS); this.vertex(x1, y1, 0, 1); this.vertex(x2, y1, 0, 1); this.vertex(x2, y2, 0, 1); this.vertex(x1, y2, 0, 1); this.end(); }
  private arrayElement(i: number) {
    // glArrayElement: fetch vertex i from the client arrays and emit it as an immediate-mode vertex
    const rd = (a: ClientArray, n: number, norm: boolean): number[] | null => { if (!a.on) return null; const es = elemBytes(a.type) * a.size; const st = a.stride || es; return this.readVec(a.ptr + BigInt(i * st), Math.min(n, a.size), a.type, !norm); };
    const c = rd(this.arr.color, 4, true); if (c) { this.color[0] = c[0]; this.color[1] = c[1]; this.color[2] = c[2]; this.color[3] = c.length > 3 ? c[3] : 1; }
    const nn = rd(this.arr.normal, 3, true); if (nn) { this.normal.set(nn); }
    for (let u = 0; u < 2; u++) { const t = rd(this.arr.tc[u], 4, false); if (t) { const v = this.tc[u]; v[0] = t[0]; v[1] = t.length > 1 ? t[1] : 0; v[2] = t.length > 2 ? t[2] : 0; v[3] = t.length > 3 ? t[3] : 1; } }
    const p = rd(this.arr.vertex, 4, false); if (p) this.vertex(p[0], p[1], p.length > 2 ? p[2] : 0, p.length > 3 ? p[3] : 1); }

  // ---------------- draws ----------------
  private ensureObjects() { const gl = this.h.gl(); if (!this.vao) { this.vao = gl.createVertexArray(); this.vbo = gl.createBuffer(); this.ibo = gl.createBuffer(); } }
  /** immediate-mode batch (also display-list replay): interleaved 19-float vertices */
  private drawBatch(mode: number, verts: Float32Array, n: number, fromCurrent: number) {
    const gl = this.h.gl(); this.ensureObjects();
    let data = verts; if (fromCurrent) { data = verts.slice(0, n * VSTRIDE); for (let i = 0; i < n; i++) { const o = i * VSTRIDE; if (fromCurrent & 1) { data[o + 4] = this.color[0]; data[o + 5] = this.color[1]; data[o + 6] = this.color[2]; data[o + 7] = this.color[3]; } if (fromCurrent & 2) { data[o + 8] = this.normal[0]; data[o + 9] = this.normal[1]; data[o + 10] = this.normal[2]; } if (fromCurrent & 4) { data[o + 11] = this.tc[0][0]; data[o + 12] = this.tc[0][1]; data[o + 13] = this.tc[0][2]; data[o + 14] = this.tc[0][3]; } if (fromCurrent & 8) { data[o + 15] = this.tc[1][0]; data[o + 16] = this.tc[1][1]; data[o + 17] = this.tc[1][2]; data[o + 18] = this.tc[1][3]; } } }
    const saved = this.enter();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.vbo); gl.bufferData(gl.ARRAY_BUFFER, data.subarray(0, n * VSTRIDE), gl.STREAM_DRAW);
    const SB = VSTRIDE * 4; for (let i = 0; i < 5; i++) gl.enableVertexAttribArray(i);
    gl.vertexAttribPointer(0, 4, gl.FLOAT, false, SB, 0); gl.vertexAttribPointer(1, 4, gl.FLOAT, false, SB, 16); gl.vertexAttribPointer(2, 3, gl.FLOAT, false, SB, 32); gl.vertexAttribPointer(3, 4, gl.FLOAT, false, SB, 44); gl.vertexAttribPointer(4, 4, gl.FLOAT, false, SB, 60);
    this.emit(mode, n, null, 0);
    this.leave(saved); }
  /** glDrawArrays / glDrawElements through the fixed-function client arrays */
  drawArrays(mode: number, first: number, count: number) { if (count <= 0 || this.inBegin) return; if (mode > E.POLYGON) { this.err(E.INVALID_ENUM); return; } const saved = this.enter(); if (this.bindArrays(first, count)) this.emit(mode, count, null, first); this.leave(saved); }
  drawElements(mode: number, count: number, type: number, indices: bigint) {
    if (count <= 0 || this.inBegin) return; if (mode > E.POLYGON) { this.err(E.INVALID_ENUM); return; } const gl = this.h.gl(); const tb = type === E.UNSIGNED_BYTE ? 1 : type === E.UNSIGNED_SHORT ? 2 : 4;
    let idx: Uint32Array | Uint16Array | Uint8Array | null; const eb = this.h.bound().element;
    if (eb) { const tmp = new Uint8Array(count * tb); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.h.bufObj(eb)); gl.getBufferSubData(gl.ELEMENT_ARRAY_BUFFER, Number(indices), tmp); idx = tb === 4 ? new Uint32Array(tmp.buffer) : tb === 2 ? new Uint16Array(tmp.buffer) : tmp; }
    else { const v = this.h.view(indices, count * tb, type); if (!v) return; idx = v as Uint32Array; }
    let maxI = 0, minI = 0x7fffffff; for (let i = 0; i < idx.length; i++) { if (idx[i] > maxI) maxI = idx[i]; if (idx[i] < minI) minI = idx[i]; }
    const saved = this.enter(); if (this.bindArrays(0, maxI + 1)) this.emit(mode, count, idx, 0); this.leave(saved); }
  /** point the five fixed attributes at the client arrays (VBO-backed directly, client memory through the scratch buffer) */
  private bindArrays(first: number, count: number): boolean {
    const gl = this.h.gl(); const h = this.h; const list: { loc: number; a: ClientArray; norm: boolean }[] = [{ loc: 0, a: this.arr.vertex, norm: false }, { loc: 1, a: this.arr.color, norm: true }, { loc: 2, a: this.arr.normal, norm: true }, { loc: 3, a: this.arr.tc[0], norm: false }, { loc: 4, a: this.arr.tc[1], norm: false }];
    gl.disableVertexAttribArray(1); gl.vertexAttrib4fv(1, this.color); gl.disableVertexAttribArray(2); gl.vertexAttrib3fv(2, this.normal); gl.disableVertexAttribArray(3); gl.vertexAttrib4fv(3, this.tc[0]); gl.disableVertexAttribArray(4); gl.vertexAttrib4fv(4, this.tc[1]);
    const parts: { loc: number; a: ClientArray; bytes: Uint8Array; off: number; stride: number }[] = []; let total = 0;
    for (const it of list) { const a = it.a; if (!a.on) continue;
      if (a.type === E.DOUBLE) { this.warn('dblarray', 'GL_DOUBLE client arrays are converted to float at every draw'); }
      const es = elemBytes(a.type) * a.size, stride = a.stride || es;
      if (a.buffer) { gl.bindBuffer(gl.ARRAY_BUFFER, h.bufObj(a.buffer)); gl.enableVertexAttribArray(it.loc); gl.vertexAttribPointer(it.loc, a.size, a.type === E.DOUBLE ? E.FLOAT : a.type, it.norm && a.type !== E.FLOAT, stride, Number(a.ptr)); continue; }
      const nverts = first + count; const need = (nverts - 1) * stride + es; let bytes = h.u8(a.ptr, need); if (!bytes) return false;
      let st = stride, type = a.type; if (a.type === E.DOUBLE) { const dv = new Float64Array(bytes.slice().buffer); const f = new Float32Array(nverts * a.size); for (let i = 0; i < nverts; i++) for (let k = 0; k < a.size; k++) f[i * a.size + k] = dv[(i * stride) / 8 + k]; bytes = new Uint8Array(f.buffer); st = a.size * 4; type = E.FLOAT; }
      total = (total + 3) & ~3; parts.push({ loc: it.loc, a: { ...a, type, stride: st }, bytes, off: total, stride: st }); total += bytes.length; }
    if (parts.length) { const blob = new Uint8Array(total); for (const p of parts) blob.set(p.bytes, p.off); gl.bindBuffer(gl.ARRAY_BUFFER, this.vbo); gl.bufferData(gl.ARRAY_BUFFER, blob, gl.STREAM_DRAW);
      for (const p of parts) { gl.enableVertexAttribArray(p.loc); const norm = p.loc === 1 || p.loc === 2 ? p.a.type !== E.FLOAT : false; gl.vertexAttribPointer(p.loc, p.a.size, p.a.type, norm, p.stride, p.off); } }
    return true; }
  /** enter the fixed-function drawing state (own VAO); returns what to restore */
  private enter(): { vao: WebGLVertexArrayObject | null; prog: WebGLProgram | null; array: WebGLBuffer | null } {
    const gl = this.h.gl(); this.ensureObjects(); const b = this.h.bound();
    const saved = { vao: this.h.vaoObj(b.vao), prog: this.h.progObj(b.program), array: this.h.bufObj(b.array) };
    gl.bindVertexArray(this.vao); return saved; }
  private leave(s: { vao: WebGLVertexArrayObject | null; prog: WebGLProgram | null; array: WebGLBuffer | null }) { const gl = this.h.gl(); gl.bindVertexArray(s.vao); gl.useProgram(s.prog); gl.bindBuffer(gl.ARRAY_BUFFER, s.array); const eb = this.h.bound().element; gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, eb ? this.h.bufObj(eb) : null); }
  /** choose/upload the program and issue the WebGL draw for a GL 1.x primitive (quads, polygons, polygon mode handled) */
  private emit(mode: number, count: number, idx: Uint32Array | Uint16Array | Uint8Array | null, first: number) {
    const gl = this.h.gl(); const prog = this.program(); if (!prog) return; this.uniforms(prog);
    const polyMode = mode >= E.TRIANGLES ? this.polyFront : E.FILL;   // GL_BACK polygon mode separately is not supported: front mode applies to both faces
    let glMode = mode, n = count, ind: Uint32Array | null = null;
    const src = (i: number) => idx ? idx[i] : first + i;
    if (mode === E.QUADS || mode === E.QUAD_STRIP || mode === E.POLYGON || polyMode !== E.FILL || (idx && idx.BYTES_PER_ELEMENT !== 4)) {
      // build an explicit triangle (or edge) index list
      const tris: number[] = [];
      if (mode === E.QUADS) for (let q = 0; q + 3 < count; q += 4) tris.push(src(q), src(q + 1), src(q + 2), src(q), src(q + 2), src(q + 3));
      else if (mode === E.QUAD_STRIP) for (let q = 0; q + 3 < count; q += 2) tris.push(src(q), src(q + 1), src(q + 3), src(q), src(q + 3), src(q + 2));
      else if (mode === E.POLYGON || mode === E.TRIANGLE_FAN) for (let i = 1; i + 1 < count; i++) tris.push(src(0), src(i), src(i + 1));
      else if (mode === E.TRIANGLE_STRIP) for (let i = 0; i + 2 < count; i++) { if (i & 1) tris.push(src(i + 1), src(i), src(i + 2)); else tris.push(src(i), src(i + 1), src(i + 2)); }
      else if (mode === E.TRIANGLES) for (let i = 0; i + 2 < count; i += 3) tris.push(src(i), src(i + 1), src(i + 2));
      else for (let i = 0; i < count; i++) tris.push(src(i));
      if (mode >= E.TRIANGLES && polyMode === E.LINE) { const e: number[] = []; for (let i = 0; i + 2 < tris.length; i += 3) e.push(tris[i], tris[i + 1], tris[i + 1], tris[i + 2], tris[i + 2], tris[i]); ind = new Uint32Array(e); glMode = E.LINES; }
      else if (mode >= E.TRIANGLES && polyMode === E.POINT) { ind = new Uint32Array(tris); glMode = E.POINTS; }
      else { ind = new Uint32Array(tris); glMode = mode >= E.TRIANGLES ? E.TRIANGLES : mode; }
      n = ind.length; }
    else if (idx) ind = idx as Uint32Array;
    if (ind) { gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.ibo); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, ind, gl.STREAM_DRAW); gl.drawElements(glMode, n, gl.UNSIGNED_INT, 0); }
    else gl.drawArrays(glMode, first, n); }

  // ---------------- shader generation ----------------
  private key(): string {
    const u0 = this.units[0], u1 = this.units[1]; const t0 = (u0.on2d || u0.on1d) && this.h.texIdOn(0, E.TEXTURE_2D) ? 1 : 0, t1 = (u1.on2d || u1.on1d) && this.h.texIdOn(1, E.TEXTURE_2D) ? 1 : 0;
    return `${t0}${t1}${this.lighting ? 1 : 0}${this.fog ? 1 : 0}${this.alphaTest ? 1 : 0}${this.shade === E.FLAT ? SHADE_FLAT : 0}${this.twoSide && this.lighting ? 1 : 0}${this.clipOn ? 1 : 0}`; }
  private program(): Program | null {
    const k = this.key(); let p = this.progs.get(k); if (p) { this.h.gl().useProgram(p.p); return p; }
    const gl = this.h.gl(); const [t0, t1, lit, fog, at, flat, two, clip] = k.split('').map(Number);
    const q = flat ? 'flat ' : '';
    const vs = `#version 300 es
precision highp float; precision highp int;
layout(location=0) in vec4 aPos; layout(location=1) in vec4 aColor; layout(location=2) in vec3 aNormal; layout(location=3) in vec4 aTc0; layout(location=4) in vec4 aTc1;
uniform mat4 uMV, uP, uTex0, uTex1; uniform mat3 uNM; uniform float uPointSize; uniform bool uNormalize;
uniform ivec4 uGen0, uGen1; uniform vec4 uGenObj0[4], uGenEye0[4], uGenObj1[4], uGenEye1[4];
${lit ? `struct Light { vec4 pos; vec4 amb; vec4 dif; vec4 spec; vec3 spotDir; float spotExp; float spotCos; vec3 att; };
uniform Light uL[8]; uniform int uLightMask; uniform bool uLocalViewer; uniform vec4 uLMAmb; uniform int uColorMat; uniform vec4 uMatAmb, uMatDif, uMatSpec, uMatEmis; uniform float uMatShin;` : ''}
${q}out vec4 vColor; ${two ? `${q}out vec4 vColorBack;` : ''} out vec4 vTc0; out vec4 vTc1; out vec3 vEye; out float vFog;
${lit ? `vec4 shade(vec3 P, vec3 N) {
  vec4 mamb = uMatAmb, mdif = uMatDif, mspec = uMatSpec, memis = uMatEmis;
  if (uColorMat == 1) { mamb = aColor; mdif = aColor; } else if (uColorMat == 2) mamb = aColor; else if (uColorMat == 3) mdif = aColor; else if (uColorMat == 4) mspec = aColor; else if (uColorMat == 5) memis = aColor;
  vec3 c = memis.rgb + mamb.rgb * uLMAmb.rgb;
  for (int i = 0; i < 8; i++) { if ((uLightMask & (1 << i)) == 0) continue;
    vec3 L; float att = 1.0; if (uL[i].pos.w == 0.0) L = normalize(uL[i].pos.xyz); else { vec3 d = uL[i].pos.xyz / uL[i].pos.w - P; float dist = length(d); L = d / max(dist, 1e-6); att = 1.0 / (uL[i].att.x + uL[i].att.y * dist + uL[i].att.z * dist * dist);
      if (uL[i].spotCos > -1.0) { float sd = dot(-L, normalize(uL[i].spotDir)); if (sd < uL[i].spotCos) att = 0.0; else att *= pow(max(sd, 0.0), uL[i].spotExp); } }
    float nl = max(dot(N, L), 0.0); vec3 col = mamb.rgb * uL[i].amb.rgb + nl * mdif.rgb * uL[i].dif.rgb;
    if (nl > 0.0) { vec3 H = normalize(L + (uLocalViewer ? normalize(-P) : vec3(0.0, 0.0, 1.0))); float nh = max(dot(N, H), 0.0); float sp = uMatShin > 0.0 ? pow(nh, uMatShin) : 1.0; col += sp * mspec.rgb * uL[i].spec.rgb; }
    c += att * col; }
  return vec4(clamp(c, 0.0, 1.0), mdif.a); }` : ''}
vec4 texgen(ivec4 g, vec4 tc, vec4 obj[4], vec4 eye[4], vec3 P, vec3 N) { vec4 r = tc; vec3 u = normalize(P); vec3 R = reflect(u, N);
  for (int c = 0; c < 4; c++) { int m = c == 0 ? g.x : c == 1 ? g.y : c == 2 ? g.z : g.w; if (m == 0) continue; float v = 0.0;
    if (m == 0x2401) v = dot(aPos, obj[c]); else if (m == 0x2400) v = dot(vec4(P, 1.0), eye[c]); else if (m == 0x2402) { vec3 f = R + vec3(0.0, 0.0, 1.0); float mm = 2.0 * length(f); v = (c == 0 ? R.x : R.y) / max(mm, 1e-6) + 0.5; } else if (m == 0x8511) v = c == 0 ? N.x : c == 1 ? N.y : N.z; else if (m == 0x8512) v = c == 0 ? R.x : c == 1 ? R.y : R.z;
    if (c == 0) r.x = v; else if (c == 1) r.y = v; else if (c == 2) r.z = v; else r.w = v; } return r; }
void main() {
  vec4 ep = uMV * aPos; vec3 P = ep.xyz / (ep.w == 0.0 ? 1.0 : ep.w); vec3 N = uNM * aNormal; if (uNormalize) N = normalize(N);
  gl_Position = uP * ep; gl_PointSize = uPointSize; vEye = P; vFog = length(P);
  ${lit ? `vColor = shade(P, N); ${two ? 'vColorBack = shade(P, -N);' : ''}` : 'vColor = aColor;'}
  vTc0 = uTex0 * texgen(uGen0, aTc0, uGenObj0, uGenEye0, P, N); vTc1 = uTex1 * texgen(uGen1, aTc1, uGenObj1, uGenEye1, P, N);
}`;
    const fs = `#version 300 es
precision highp float; precision highp int;
${q}in vec4 vColor; ${two ? `${q}in vec4 vColorBack;` : ''} in vec4 vTc0; in vec4 vTc1; in vec3 vEye; in float vFog;
uniform sampler2D uS0, uS1; uniform int uEnv0, uEnv1; uniform vec4 uEnvColor0, uEnvColor1; uniform bool uHasA0, uHasA1, u1D0, u1D1;
uniform int uComb0[14], uComb1[14]; uniform float uScale0, uScale1, uScaleA0, uScaleA1;
uniform int uAlphaFunc; uniform float uAlphaRef; uniform int uFogMode; uniform vec4 uFogColor; uniform float uFogDensity, uFogStart, uFogEnd;
uniform int uClipMask; uniform vec4 uClip[6];
layout(location=0) out vec4 oColor;
vec4 srcOf(int s, vec4 tex, vec4 prev, vec4 prim, vec4 cst) { return s == 0x1702 ? tex : s == 0x8578 ? prev : s == 0x8577 ? prim : s == 0x8576 ? cst : prev; }
vec3 opRgb(int o, vec4 v) { return o == 0x0300 ? v.rgb : o == 0x0301 ? 1.0 - v.rgb : o == 0x0302 ? vec3(v.a) : vec3(1.0 - v.a); }
float opA(int o, vec4 v) { return o == 0x0302 ? v.a : 1.0 - v.a; }
vec4 combine(int comb[14], float sc, float sca, vec4 tex, vec4 prev, vec4 prim, vec4 cst) {
  vec3 a0 = opRgb(comb[5], srcOf(comb[2], tex, prev, prim, cst)), a1 = opRgb(comb[6], srcOf(comb[3], tex, prev, prim, cst)), a2 = opRgb(comb[7], srcOf(comb[4], tex, prev, prim, cst));
  float b0 = opA(comb[11], srcOf(comb[8], tex, prev, prim, cst)), b1 = opA(comb[12], srcOf(comb[9], tex, prev, prim, cst)), b2 = opA(comb[13], srcOf(comb[10], tex, prev, prim, cst));
  vec3 rgb; int m = comb[0];
  if (m == 0x1E01) rgb = a0; else if (m == 0x2100) rgb = a0 * a1; else if (m == 0x0104) rgb = a0 + a1; else if (m == 0x8574) rgb = a0 + a1 - 0.5; else if (m == 0x8575) rgb = mix(a1, a0, a2); else if (m == 0x84E7) rgb = a0 - a1; else if (m == 0x86AE) rgb = vec3(4.0 * dot(a0 - 0.5, a1 - 0.5)); else rgb = a0 * a1;
  float a; m = comb[1]; if (m == 0x1E01) a = b0; else if (m == 0x2100) a = b0 * b1; else if (m == 0x0104) a = b0 + b1; else if (m == 0x8574) a = b0 + b1 - 0.5; else if (m == 0x8575) a = mix(b1, b0, b2); else if (m == 0x84E7) a = b0 - b1; else a = b0 * b1;
  if (comb[0] == 0x86AF) a = rgb.r;
  return vec4(clamp(rgb * sc, 0.0, 1.0), clamp(a * sca, 0.0, 1.0)); }
vec4 stage(int env, vec4 tex, bool hasA, vec4 prev, vec4 prim, vec4 cst, int comb[14], float sc, float sca) {
  if (env == 0x2100) return vec4(prev.rgb * tex.rgb, hasA ? prev.a * tex.a : prev.a);
  if (env == 0x1E01) return vec4(tex.rgb, hasA ? tex.a : prev.a);
  if (env == 0x2101) return vec4(hasA ? mix(prev.rgb, tex.rgb, tex.a) : tex.rgb, prev.a);
  if (env == 0x0BE2) return vec4(mix(prev.rgb, cst.rgb, tex.rgb), hasA ? prev.a * tex.a : prev.a);
  if (env == 0x0104) return vec4(min(prev.rgb + tex.rgb, 1.0), hasA ? prev.a * tex.a : prev.a);
  return combine(comb, sc, sca, tex, prev, prim, cst); }
void main() {
  ${clip ? 'for (int i = 0; i < 6; i++) if ((uClipMask & (1 << i)) != 0 && dot(uClip[i], vec4(vEye, 1.0)) < 0.0) discard;' : ''}
  vec4 prim = ${two ? '(gl_FrontFacing ? vColor : vColorBack)' : 'vColor'}; vec4 c = prim;
  ${t0 ? 'vec4 x0 = textureProj(uS0, u1D0 ? vec4(vTc0.x, 0.5 * vTc0.w, vTc0.z, vTc0.w) : vTc0); c = stage(uEnv0, x0, uHasA0, c, prim, uEnvColor0, uComb0, uScale0, uScaleA0);' : ''}
  ${t1 ? 'vec4 x1 = textureProj(uS1, u1D1 ? vec4(vTc1.x, 0.5 * vTc1.w, vTc1.z, vTc1.w) : vTc1); c = stage(uEnv1, x1, uHasA1, c, prim, uEnvColor1, uComb1, uScale1, uScaleA1);' : ''}
  ${at ? `bool pass = uAlphaFunc == 0x0207 ? true : uAlphaFunc == 0x0200 ? false : uAlphaFunc == 0x0201 ? c.a < uAlphaRef : uAlphaFunc == 0x0202 ? c.a == uAlphaRef : uAlphaFunc == 0x0203 ? c.a <= uAlphaRef : uAlphaFunc == 0x0204 ? c.a > uAlphaRef : uAlphaFunc == 0x0205 ? c.a != uAlphaRef : c.a >= uAlphaRef; if (!pass) discard;` : ''}
  ${fog ? `float f = uFogMode == 0x2601 ? (uFogEnd - vFog) / max(uFogEnd - uFogStart, 1e-6) : uFogMode == 0x0800 ? exp(-uFogDensity * vFog) : exp(-(uFogDensity * vFog) * (uFogDensity * vFog)); c.rgb = mix(uFogColor.rgb, c.rgb, clamp(f, 0.0, 1.0));` : ''}
  oColor = c;
}`;
    const mk = (type: number, src: string) => { const s = gl.createShader(type)!; gl.shaderSource(s, src); gl.compileShader(s); if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) { this.warn('ffshader', 'fixed-function shader failed to compile: ' + gl.getShaderInfoLog(s)); return null; } return s; };
    const v = mk(gl.VERTEX_SHADER, vs), f = mk(gl.FRAGMENT_SHADER, fs); if (!v || !f) return null;
    const pr = gl.createProgram()!; gl.attachShader(pr, v); gl.attachShader(pr, f); gl.linkProgram(pr); if (!gl.getProgramParameter(pr, gl.LINK_STATUS)) { this.warn('fflink', 'fixed-function program failed to link: ' + gl.getProgramInfoLog(pr)); return null; }
    p = { p: pr, u: new Map(), ver: {} }; const nu = gl.getProgramParameter(pr, gl.ACTIVE_UNIFORMS) as number;
    for (let i = 0; i < nu; i++) { const info = gl.getActiveUniform(pr, i)!; const base = info.name.replace(/\[0\]$/, ''); p.u.set(base, gl.getUniformLocation(pr, info.name)); if (info.size > 1) for (let k = 1; k < info.size; k++) p.u.set(`${base}[${k}]`, gl.getUniformLocation(pr, `${base}[${k}]`)); }
    gl.useProgram(pr); gl.uniform1i(p.u.get('uS0') ?? null, 0); gl.uniform1i(p.u.get('uS1') ?? null, 1);
    this.progs.set(k, p); return p; }
  private uniforms(p: Program) {
    const gl = this.h.gl(); const u = (n: string) => p.u.get(n) ?? null;
    const mv = this.mv[this.mv.length - 1]; gl.uniformMatrix4fv(u('uMV'), false, mv); gl.uniformMatrix4fv(u('uP'), false, this.pr[this.pr.length - 1]); gl.uniformMatrix3fv(u('uNM'), false, normalMatrix(mv));
    gl.uniformMatrix4fv(u('uTex0'), false, this.tx[0][this.tx[0].length - 1]); gl.uniformMatrix4fv(u('uTex1'), false, this.tx[1][this.tx[1].length - 1]); gl.uniform1f(u('uPointSize'), this.pointSize); gl.uniform1i(u('uNormalize'), this.normalize || this.rescale ? 1 : 0);
    if (p.ver.gen !== this.ver.gen) { p.ver.gen = this.ver.gen; for (let k = 0; k < 2; k++) { const un = this.units[k]; const g = Array.from(un.gen, (x) => x & 0x10000 ? x & 0xffff : 0); gl.uniform4iv(u(`uGen${k}`), g); for (let c = 0; c < 4; c++) { gl.uniform4fv(u(`uGenObj${k}[${c}]`) ?? u(`uGenObj${k}`), un.genObj[c]); gl.uniform4fv(u(`uGenEye${k}[${c}]`) ?? u(`uGenEye${k}`), un.genEye[c]); } } }
    if (this.lighting && (p.ver.light !== this.ver.light || p.ver.mat !== this.ver.mat)) { p.ver.light = this.ver.light; p.ver.mat = this.ver.mat; let mask = 0;
      for (let i = 0; i < 8; i++) { const L = this.lights[i]; if (!L.on) continue; mask |= 1 << i; gl.uniform4fv(u(`uL[${i}].pos`), L.pos); gl.uniform4fv(u(`uL[${i}].amb`), L.amb); gl.uniform4fv(u(`uL[${i}].dif`), L.dif); gl.uniform4fv(u(`uL[${i}].spec`), L.spec); gl.uniform3fv(u(`uL[${i}].spotDir`), L.spotDir); gl.uniform1f(u(`uL[${i}].spotExp`), L.spotExp); gl.uniform1f(u(`uL[${i}].spotCos`), L.spotCut >= 180 ? -2 : Math.cos(L.spotCut * Math.PI / 180)); gl.uniform3fv(u(`uL[${i}].att`), L.att); }
      gl.uniform1i(u('uLightMask'), mask); gl.uniform1i(u('uLocalViewer'), this.localViewer ? 1 : 0); gl.uniform4fv(u('uLMAmb'), this.lmAmbient);
      const cm = this.colorMaterial ? (this.cmMode === E.AMBIENT_AND_DIFFUSE ? 1 : this.cmMode === E.AMBIENT ? 2 : this.cmMode === E.DIFFUSE ? 3 : this.cmMode === E.SPECULAR ? 4 : 5) : 0; gl.uniform1i(u('uColorMat'), cm);
      gl.uniform4fv(u('uMatAmb'), this.mat.amb); gl.uniform4fv(u('uMatDif'), this.mat.dif); gl.uniform4fv(u('uMatSpec'), this.mat.spec); gl.uniform4fv(u('uMatEmis'), this.mat.emis); gl.uniform1f(u('uMatShin'), this.mat.shin); }
    /* texture alpha may change with the bound texture → always refresh the (tiny) texenv block */ { p.ver.env = this.ver.env; for (let k = 0; k < 2; k++) { const un = this.units[k]; const id = this.h.texIdOn(k, E.TEXTURE_2D); gl.uniform1i(u(`uEnv${k}`), un.env); gl.uniform4fv(u(`uEnvColor${k}`), un.envColor); gl.uniform1i(u(`uHasA${k}`), id && this.h.texHasAlpha(id) ? 1 : 0); gl.uniform1i(u(`u1D${k}`), un.on1d && !un.on2d ? 1 : 0); if (un.env === E.COMBINE) { gl.uniform1iv(u(`uComb${k}`), un.comb); gl.uniform1f(u(`uScale${k}`), un.scaleRgb); gl.uniform1f(u(`uScaleA${k}`), un.scaleA); } } }
    if (this.alphaTest) { gl.uniform1i(u('uAlphaFunc'), this.alphaFunc); gl.uniform1f(u('uAlphaRef'), this.alphaRef); }
    if (this.fog && p.ver.fog !== this.ver.fog) { p.ver.fog = this.ver.fog; gl.uniform1i(u('uFogMode'), this.fogMode); gl.uniform4fv(u('uFogColor'), this.fogColor); gl.uniform1f(u('uFogDensity'), this.fogDensity); gl.uniform1f(u('uFogStart'), this.fogStart); gl.uniform1f(u('uFogEnd'), this.fogEnd); }
    if (this.clipOn && p.ver.clip !== this.ver.clip) { p.ver.clip = this.ver.clip; gl.uniform1i(u('uClipMask'), this.clipOn); for (let i = 0; i < 6; i++) gl.uniform4fv(u(`uClip[${i}]`) ?? u('uClip'), this.clip[i] ?? new Float32Array(4)); } }

  // ---------------- raster position / bitmaps / pixel rectangles ----------------
  private rasterPos(x: number, y: number, z: number, w: number) {
    if (this.inBegin) { this.err(E.INVALID_OPERATION); return; }
    const v = xform(this.pr[this.pr.length - 1], xform(this.mv[this.mv.length - 1], [x, y, z, w])); const vp = this.h.viewport();
    if (v[3] === 0 || v[0] < -v[3] || v[0] > v[3] || v[1] < -v[3] || v[1] > v[3] || v[2] < -v[3] || v[2] > v[3]) { this.raster.valid = false; return; }
    this.raster.valid = true; this.raster.pos.set([x, y, z, w]); this.raster.win[0] = vp[0] + (v[0] / v[3] + 1) * vp[2] / 2; this.raster.win[1] = vp[1] + (v[1] / v[3] + 1) * vp[3] / 2; this.raster.win[2] = (v[2] / v[3] + 1) / 2; this.raster.color.set(this.color); }
  private windowPos(x: number, y: number, z: number) { this.raster.valid = true; this.raster.win[0] = x; this.raster.win[1] = y; this.raster.win[2] = Math.min(1, Math.max(0, z)); this.raster.color.set(this.color); }
  private blit(): Program | null {
    if (this.blitProg) return this.blitProg; const gl = this.h.gl();
    const vs = `#version 300 es\nlayout(location=0) in vec2 aP; layout(location=1) in vec2 aT; uniform vec4 uRect; uniform vec2 uSize; uniform float uZ; out vec2 vT; void main(){ vec2 p = uRect.xy + aP * uRect.zw; gl_Position = vec4(p / uSize * 2.0 - 1.0, uZ * 2.0 - 1.0, 1.0); vT = aT; }`;
    const fs = `#version 300 es\nprecision highp float; in vec2 vT; uniform sampler2D uS; uniform vec4 uColor; uniform int uMode; layout(location=0) out vec4 o; void main(){ vec4 t = texture(uS, vT); if (uMode == 1) { if (t.r < 0.5) discard; o = uColor; } else o = t; }`;
    const mk = (type: number, src: string) => { const s = gl.createShader(type)!; gl.shaderSource(s, src); gl.compileShader(s); return s; };
    const pr = gl.createProgram()!; gl.attachShader(pr, mk(gl.VERTEX_SHADER, vs)); gl.attachShader(pr, mk(gl.FRAGMENT_SHADER, fs)); gl.linkProgram(pr); if (!gl.getProgramParameter(pr, gl.LINK_STATUS)) { this.warn('blit', 'raster blit program failed: ' + gl.getProgramInfoLog(pr)); return null; }
    this.blitProg = { p: pr, u: new Map(['uRect', 'uSize', 'uZ', 'uS', 'uColor', 'uMode'].map((n) => [n, gl.getUniformLocation(pr, n)])), ver: {} }; this.blitTex = gl.createTexture(); return this.blitProg; }
  /** draw an RGBA8 (or R8 mask) image of w×h at window position (x, y) scaled by zoom, through the blit program */
  private blitImage(data: ArrayBufferView, w: number, h: number, x: number, y: number, zx: number, zy: number, mask: boolean, fmt = 0, type = 0) {
    const gl = this.h.gl(); const p = this.blit(); if (!p) return; const saved = this.enter();
    const prevUnit = this.h.bound().unit; gl.activeTexture(gl.TEXTURE0 + 15); gl.bindTexture(gl.TEXTURE_2D, this.blitTex);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1); gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0); gl.pixelStorei(gl.UNPACK_SKIP_ROWS, 0); gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, 0);
    if (mask) gl.texImage2D(gl.TEXTURE_2D, 0, gl.R8, w, h, 0, gl.RED, gl.UNSIGNED_BYTE, data); else gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, w, h, 0, fmt || gl.RGBA, type || gl.UNSIGNED_BYTE, data);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, this.h.unpackAlign());
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.useProgram(p.p); gl.uniform1i(p.u.get('uS') ?? null, 15); const vp = this.h.viewport(); const cw = gl.drawingBufferWidth, ch = gl.drawingBufferHeight; void vp;
    const x0 = zx >= 0 ? x : x + w * zx, y0 = zy >= 0 ? y : y + h * zy; gl.uniform4f(p.u.get('uRect') ?? null, x0, y0, Math.abs(w * zx), Math.abs(h * zy)); gl.uniform2f(p.u.get('uSize') ?? null, cw, ch); gl.uniform1f(p.u.get('uZ') ?? null, this.raster.win[2]);
    gl.uniform4fv(p.u.get('uColor') ?? null, this.raster.color); gl.uniform1i(p.u.get('uMode') ?? null, mask ? 1 : 0);
    const fx = zx >= 0 ? 0 : 1, fy = zy >= 0 ? 0 : 1; const quad = new Float32Array([0, 0, fx, fy, 1, 0, 1 - fx, fy, 0, 1, fx, 1 - fy, 1, 1, 1 - fx, 1 - fy]);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.vbo); gl.bufferData(gl.ARRAY_BUFFER, quad, gl.STREAM_DRAW); gl.enableVertexAttribArray(0); gl.enableVertexAttribArray(1); for (let i = 2; i < 5; i++) gl.disableVertexAttribArray(i); gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 16, 0); gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 16, 8);
    const vpSaved = gl.getParameter(gl.VIEWPORT) as Int32Array; gl.viewport(0, 0, cw, ch); gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4); gl.viewport(vpSaved[0], vpSaved[1], vpSaved[2], vpSaved[3]);
    gl.activeTexture(gl.TEXTURE0 + prevUnit); this.leave(saved); }
  private bitmap(w: number, h: number, xo: number, yo: number, xm: number, ym: number, p: bigint) {
    if (this.inBegin) { this.err(E.INVALID_OPERATION); return; } if (w < 0 || h < 0) { this.err(E.INVALID_VALUE); return; }
    if (this.raster.valid && p && w && h) {
      const align = this.h.unpackAlign(); const rowBytes = Math.ceil(Math.ceil(w / 8) / align) * align; const src = this.h.u8(p, rowBytes * h); if (!src) return;
      const img = new Uint8Array(w * h); for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) img[y * w + x] = (src[y * rowBytes + (x >> 3)] >> (7 - (x & 7))) & 1 ? 255 : 0;   // MSB first (GL_UNPACK_LSB_FIRST false)
      this.blitImage(img, w, h, Math.round(this.raster.win[0] - xo), Math.round(this.raster.win[1] - yo), 1, 1, true); }
    this.raster.win[0] += xm; this.raster.win[1] += ym; }
  private drawPixels(w: number, h: number, format: number, type: number, p: bigint) {
    if (this.inBegin) { this.err(E.INVALID_OPERATION); return; } if (w < 0 || h < 0) { this.err(E.INVALID_VALUE); return; } if (!this.raster.valid || !w || !h) return;
    if (format === E.DEPTH_COMPONENT || format === E.STENCIL_INDEX || format === E.COLOR_INDEX) { this.warn('drawpixels:' + format, 'glDrawPixels with GL_DEPTH_COMPONENT/GL_STENCIL_INDEX/GL_COLOR_INDEX data is not supported (color images are)'); return; }
    const px = this.h.pixels(p, w, h, format, type); if (!px || !px.data) return;
    let data = px.data, fmt = px.format, ty = px.type;
    if (fmt === E.LUMINANCE || fmt === E.ALPHA || fmt === E.LUMINANCE_ALPHA || fmt === E.RED || fmt === E.RGB || ty !== E.UNSIGNED_BYTE) {
      // expand to RGBA8 so the blit texture is always RGBA (handles the float / 16-bit cases through normalisation)
      const comps = fmt === E.RGB ? 3 : fmt === E.LUMINANCE_ALPHA ? 2 : fmt === E.RGBA ? 4 : 1; const n = w * h; const out = new Uint8Array(n * 4);
      const get = (i: number): number => { const d = data as unknown as ArrayLike<number>; if (ty === E.UNSIGNED_BYTE) return d[i]; if (ty === E.FLOAT) return Math.min(255, Math.max(0, Math.round(d[i] * 255))); if (ty === E.UNSIGNED_SHORT) return d[i] >> 8; if (ty === E.BYTE) return Math.max(0, d[i]) * 2; if (ty === E.SHORT) return Math.max(0, d[i]) >> 7; return (d[i] >>> 24); };
      for (let i = 0; i < n; i++) { const o = i * 4; if (fmt === E.RGB) { out[o] = get(i * 3); out[o + 1] = get(i * 3 + 1); out[o + 2] = get(i * 3 + 2); out[o + 3] = 255; } else if (fmt === E.RGBA) { out[o] = get(i * 4); out[o + 1] = get(i * 4 + 1); out[o + 2] = get(i * 4 + 2); out[o + 3] = get(i * 4 + 3); } else if (fmt === E.LUMINANCE_ALPHA) { const l = get(i * 2); out[o] = out[o + 1] = out[o + 2] = l; out[o + 3] = get(i * 2 + 1); } else if (fmt === E.ALPHA) { out[o] = out[o + 1] = out[o + 2] = 0; out[o + 3] = get(i); } else { const l = get(i); out[o] = out[o + 1] = out[o + 2] = l; out[o + 3] = 255; } void comps; }
      data = out; fmt = E.RGBA; ty = E.UNSIGNED_BYTE; }
    this.blitImage(data, w, h, Math.round(this.raster.win[0]), Math.round(this.raster.win[1]), this.zoomX, this.zoomY, false, fmt, ty); }
  private copyPixels(x: number, y: number, w: number, h: number, type: number) {
    if (type !== 0x1800 /* GL_COLOR */) { this.warn('copypixels', 'glCopyPixels(GL_DEPTH/GL_STENCIL): only color copies are supported'); return; } if (!this.raster.valid || w <= 0 || h <= 0) return;
    const gl = this.h.gl(); const buf = new Uint8Array(w * h * 4); gl.readPixels(x, y, w, h, gl.RGBA, gl.UNSIGNED_BYTE, buf); this.blitImage(buf, w, h, Math.round(this.raster.win[0]), Math.round(this.raster.win[1]), this.zoomX, this.zoomY, false); }

  // ---------------- GLU mipmaps / scaling / quadrics ----------------
  private buildMipmaps(target: number, ifmt: number, w: number, h: number, format: number, type: number, p: bigint): number {
    if (w < 1 || h < 1) return 100901; if (!this.h.texIdOn(this.h.bound().unit, E.TEXTURE_2D)) { /* GLU uploads to the bound texture (0 is allowed in GL 1.x but not in WebGL) */ }
    const t = target === E.TEXTURE_1D ? E.TEXTURE_2D : target;
    this.h.callImpl('glTexImage2D', [BigInt(t), 0n, BigInt(ifmt), BigInt(w), BigInt(h), 0n, BigInt(format), BigInt(type), p]);
    const gl = this.h.gl(); gl.generateMipmap(t); if (gl.getError() !== 0) { this.warn('glumip', 'gluBuild2DMipmaps: generateMipmap failed for this format — only level 0 is present'); }
    gl.texParameteri(t, gl.TEXTURE_MIN_FILTER, gl.getTexParameter(t, gl.TEXTURE_MIN_FILTER) as number); return 0; }
  private scaleImage(format: number, wi: number, hi: number, ti: number, pi: bigint, wo: number, ho: number, to: number, po: bigint): number {
    if (ti !== E.UNSIGNED_BYTE || to !== E.UNSIGNED_BYTE) { this.warn('gluscale', 'gluScaleImage: only GL_UNSIGNED_BYTE images are rescaled'); return 100900; }
    const comps = format === E.RGBA || format === E.BGRA ? 4 : format === E.RGB || format === E.BGR ? 3 : format === E.LUMINANCE_ALPHA ? 2 : 1; const align = this.h.unpackAlign(); const rin = Math.ceil(wi * comps / align) * align, rout = Math.ceil(wo * comps / align) * align;
    const src = this.h.u8(pi, rin * hi), dst = this.h.u8(po, rout * ho, true); if (!src || !dst) return 100902;
    for (let y = 0; y < ho; y++) { const sy = Math.min(hi - 1, Math.floor((y + 0.5) * hi / ho)); for (let x = 0; x < wo; x++) { const sx = Math.min(wi - 1, Math.floor((x + 0.5) * wi / wo)); for (let c = 0; c < comps; c++) dst[y * rout + x * comps + c] = src[sy * rin + sx * comps + c]; } }
    return 0; }
  private sphere(q: Quadric, r: number, slices: number, stacks: number) {
    const inside = q.orientation === 100021, line = q.drawStyle === 100011 || q.drawStyle === 100013, pts = q.drawStyle === 100010; if (slices < 2 || stacks < 1) return;
    const emitV = (i: number, j: number) => { const th = i * 2 * Math.PI / slices, ph = j * Math.PI / stacks; const x = Math.sin(ph) * Math.cos(th), y = Math.sin(ph) * Math.sin(th), z = Math.cos(ph); const s = inside ? -1 : 1;
      if (q.normals !== 100002) { this.normal[0] = s * x; this.normal[1] = s * y; this.normal[2] = s * z; } if (q.texture) { this.tc[0][0] = i / slices; this.tc[0][1] = 1 - j / stacks; } this.vertex(r * x, r * y, r * z, 1); };
    if (pts || line) { this.begin(pts ? E.POINTS : E.LINES); for (let j = 0; j <= stacks; j++) for (let i = 0; i < slices; i++) { emitV(i, j); if (line) emitV(i + 1, j); if (line && j < stacks) { emitV(i, j); emitV(i, j + 1); } } this.end(); return; }
    for (let j = 0; j < stacks; j++) { this.begin(E.QUAD_STRIP); for (let i = 0; i <= slices; i++) { if (inside) { emitV(i, j); emitV(i, j + 1); } else { emitV(i, j + 1); emitV(i, j); } } this.end(); } }
  private cylinder(q: Quadric, rb: number, rt: number, hgt: number, slices: number, stacks: number) {
    const inside = q.orientation === 100021, line = q.drawStyle === 100011 || q.drawStyle === 100013; if (slices < 2 || stacks < 1) return; const nz = (rb - rt) / Math.max(hgt, 1e-9); const nl = Math.hypot(1, nz);
    const emitV = (i: number, j: number) => { const th = i * 2 * Math.PI / slices, t = j / stacks; const r = rb + (rt - rb) * t; const c = Math.cos(th), s = Math.sin(th); const sg = inside ? -1 : 1; if (q.normals !== 100002) { this.normal[0] = sg * c / nl; this.normal[1] = sg * s / nl; this.normal[2] = sg * nz / nl; } if (q.texture) { this.tc[0][0] = i / slices; this.tc[0][1] = t; } this.vertex(r * c, r * s, hgt * t, 1); };
    if (line) { this.begin(E.LINES); for (let j = 0; j <= stacks; j++) for (let i = 0; i < slices; i++) { emitV(i, j); emitV(i + 1, j); if (j < stacks) { emitV(i, j); emitV(i, j + 1); } } this.end(); return; }
    for (let j = 0; j < stacks; j++) { this.begin(E.QUAD_STRIP); for (let i = 0; i <= slices; i++) { if (inside) { emitV(i, j + 1); emitV(i, j); } else { emitV(i, j); emitV(i, j + 1); } } this.end(); } }
  private disk(q: Quadric, ri: number, ro: number, slices: number, loops: number, start: number, sweep: number) {
    const inside = q.orientation === 100021, line = q.drawStyle === 100011 || q.drawStyle === 100013; if (slices < 2 || loops < 1) return; if (q.normals !== 100002) { this.normal[0] = 0; this.normal[1] = 0; this.normal[2] = inside ? -1 : 1; }
    const a0 = start * Math.PI / 180, da = sweep * Math.PI / 180; const emitV = (i: number, r: number) => { const th = a0 + (inside ? -1 : 1) * i * da / slices; const c = Math.cos(th), s = Math.sin(th); if (q.texture) { this.tc[0][0] = 0.5 + 0.5 * c * r / ro; this.tc[0][1] = 0.5 + 0.5 * s * r / ro; } this.vertex(r * c, r * s, 0, 1); };
    if (line) { this.begin(E.LINES); for (let l = 0; l <= loops; l++) { const r = ri + (ro - ri) * l / loops; for (let i = 0; i < slices; i++) { emitV(i, r); emitV(i + 1, r); } } this.end(); return; }
    for (let l = 0; l < loops; l++) { const r1 = ri + (ro - ri) * l / loops, r2 = ri + (ro - ri) * (l + 1) / loops; this.begin(E.QUAD_STRIP); for (let i = 0; i <= slices; i++) { emitV(i, r2); emitV(i, r1); } this.end(); } }
}
function elemBytes(t: number): number { return t === E.BYTE || t === E.UNSIGNED_BYTE ? 1 : t === E.SHORT || t === E.UNSIGNED_SHORT ? 2 : t === E.DOUBLE ? 8 : 4; }
function xformT(invM: Mat, p: Float32Array): Float32Array { /* plane' = p · M⁻¹ (row vector times matrix) */ const o = new Float32Array(4); for (let c = 0; c < 4; c++) o[c] = p[0] * invM[c * 4] + p[1] * invM[c * 4 + 1] + p[2] * invM[c * 4 + 2] + p[3] * invM[c * 4 + 3]; return o; }
