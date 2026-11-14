/**
 * gl.ts — the OpenGL bridge: opengl32.dll calls made by the emulated program are executed on the device's WebGL2 context.
 *
 * emu/x64/opengl.inc marshals every GL entry point generically (id = index in gltable.ts, arguments as raw 64-bit words:
 * ints/enums, float/double bit patterns, guest pointers). This file implements each entry point against WebGL2 and talks to
 * guest memory through the emulator exports (x64_gl_ptr / x64_gl_strlen / x64_gl_alloc). What WebGL2 cannot do is reported
 * the way a core-profile driver does it — GL_INVALID_OPERATION / GL_INVALID_ENUM plus one log line — never silently faked:
 *   • the fixed-function pipeline (glBegin/glEnd, matrices, lighting, display lists, GLU) lives in glff.ts (GL 1.x on WebGL2)
 *   • multisampled/rectangle textures, geometry/tessellation stages → GL_INVALID_ENUM / INVALID_OPERATION
 *   • GL_QUADS, client-side vertex arrays, polygon-mode outlines and 1D textures ARE emulated (index expansion / scratch VBOs).
 * The context behaves like a forward-compatible OpenGL 3.3 core context (GL_CONTEXT_FLAGS reports it), which is what WebGL2 is closest to.
 */
import { GL_FUNCS } from './gltable';

export interface GLMem {
  ptr(addr: bigint, len: number, write: boolean): number;   // offset into the wasm memory or 0 (unmapped guest address)
  strlen(addr: bigint): number;
  alloc(len: number): bigint;                                 // guest heap block (process heap) for strings / mapped buffers
  free(addr: bigint): void;
  psize(): number;                                            // 4 (x86 program) or 8 (x64)
  buffer(): ArrayBuffer;
}
type Impl = (a: BigUint64Array) => number | bigint | void;
const u32f = new Uint32Array(1), f32v = new Float32Array(u32f.buffer);
const u64f = new BigUint64Array(1), f64v = new Float64Array(u64f.buffer);
const I = (v: bigint) => Number(BigInt.asIntN(32, v));
const U = (v: bigint) => Number(v & 0xffffffffn);
const F = (v: bigint) => { u32f[0] = Number(v & 0xffffffffn); return f32v[0]; };
const D = (v: bigint) => { u64f[0] = v; return f64v[0]; };
const Q = (v: bigint) => Number(BigInt.asIntN(64, v));
const B = (v: bigint) => (v & 0xffn) !== 0n;

const GL = { NO_ERROR: 0, INVALID_ENUM: 0x500, INVALID_VALUE: 0x501, INVALID_OPERATION: 0x502, OUT_OF_MEMORY: 0x505,
  TEXTURE_2D: 0x0DE1, TEXTURE_1D: 0x0DE0, TEXTURE_3D: 0x806F, TEXTURE_2D_ARRAY: 0x8C1A, TEXTURE_CUBE_MAP: 0x8513,
  ARRAY_BUFFER: 0x8892, ELEMENT_ARRAY_BUFFER: 0x8893, PIXEL_PACK_BUFFER: 0x88EB, PIXEL_UNPACK_BUFFER: 0x88EC, UNIFORM_BUFFER: 0x8A11, COPY_READ_BUFFER: 0x8F36, COPY_WRITE_BUFFER: 0x8F37, TRANSFORM_FEEDBACK_BUFFER: 0x8C8E,
  FRAMEBUFFER: 0x8D40, READ_FRAMEBUFFER: 0x8CA8, DRAW_FRAMEBUFFER: 0x8CA9, RENDERBUFFER: 0x8D41,
  QUADS: 7, QUAD_STRIP: 8, POLYGON: 9, TRIANGLES: 4, TRIANGLE_FAN: 6,
  UNSIGNED_BYTE: 0x1401, BYTE: 0x1400, UNSIGNED_SHORT: 0x1403, SHORT: 0x1402, UNSIGNED_INT: 0x1405, INT: 0x1404, FLOAT: 0x1406, HALF_FLOAT: 0x140B, DOUBLE: 0x140A,
  RGBA: 0x1908, RGB: 0x1907, BGRA: 0x80E1, BGR: 0x80E0, RED: 0x1903, RG: 0x8227, ALPHA: 0x1906, LUMINANCE: 0x1909, LUMINANCE_ALPHA: 0x190A, DEPTH_COMPONENT: 0x1902, DEPTH_STENCIL: 0x84F9, STENCIL_INDEX: 0x1901,
  RGBA8: 0x8058, RGB8: 0x8051, RGBA16F: 0x881A, RGBA32F: 0x8814, RGB16F: 0x881B, RGB32F: 0x8815, R8: 0x8229, RG8: 0x822B, DEPTH_COMPONENT16: 0x81A5, DEPTH_COMPONENT24: 0x81A6, DEPTH_COMPONENT32: 0x81A7, DEPTH_COMPONENT32F: 0x8CAC, DEPTH24_STENCIL8: 0x88F0, STENCIL_INDEX8: 0x8D48, SRGB8_ALPHA8: 0x8C43, SRGB_ALPHA: 0x8C42, SRGB8: 0x8C41, SRGB: 0x8C40,
  RGBA_INTEGER: 0x8D99, RGB_INTEGER: 0x8D98, RG_INTEGER: 0x8228, RED_INTEGER: 0x8D94,
  EXTENSIONS: 0x1F03, VENDOR: 0x1F00, RENDERER: 0x1F01, VERSION: 0x1F02, SHADING_LANGUAGE_VERSION: 0x8B8C, NUM_EXTENSIONS: 0x821D, MAJOR_VERSION: 0x821B, MINOR_VERSION: 0x821C, CONTEXT_FLAGS: 0x821E, CONTEXT_PROFILE_MASK: 0x9126,
  UNPACK_ALIGNMENT: 0x0CF5, PACK_ALIGNMENT: 0x0D05, UNPACK_ROW_LENGTH: 0x0CF2, UNPACK_SKIP_ROWS: 0x0CF3, UNPACK_SKIP_PIXELS: 0x0CF4, PACK_ROW_LENGTH: 0x0D02, PACK_SKIP_ROWS: 0x0D03, PACK_SKIP_PIXELS: 0x0D04, UNPACK_IMAGE_HEIGHT: 0x806E, UNPACK_SKIP_IMAGES: 0x806D, UNPACK_SWAP_BYTES: 0x0CF0, UNPACK_LSB_FIRST: 0x0CF1, PACK_SWAP_BYTES: 0x0D00, PACK_LSB_FIRST: 0x0D01,
  VERTEX_SHADER: 0x8B31, FRAGMENT_SHADER: 0x8B30, GEOMETRY_SHADER: 0x8DD9, COMPILE_STATUS: 0x8B81, LINK_STATUS: 0x8B82, VALIDATE_STATUS: 0x8B83, INFO_LOG_LENGTH: 0x8B84, SHADER_SOURCE_LENGTH: 0x8B88, ATTACHED_SHADERS: 0x8B85, ACTIVE_UNIFORMS: 0x8B86, ACTIVE_UNIFORM_MAX_LENGTH: 0x8B87, ACTIVE_ATTRIBUTES: 0x8B89, ACTIVE_ATTRIBUTE_MAX_LENGTH: 0x8B8A, DELETE_STATUS: 0x8B80, SHADER_TYPE: 0x8B4F, ACTIVE_UNIFORM_BLOCKS: 0x8A36, ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH: 0x8A35, TRANSFORM_FEEDBACK_VARYINGS: 0x8C83, PROGRAM_BINARY_LENGTH: 0x8741,
  TEXTURE_BINDING_2D: 0x8069, TEXTURE_BINDING_3D: 0x806A, TEXTURE_BINDING_2D_ARRAY: 0x8C1D, TEXTURE_BINDING_CUBE_MAP: 0x8514, ARRAY_BUFFER_BINDING: 0x8894, ELEMENT_ARRAY_BUFFER_BINDING: 0x8895, FRAMEBUFFER_BINDING: 0x8CA6, READ_FRAMEBUFFER_BINDING: 0x8CAA, RENDERBUFFER_BINDING: 0x8CA7, CURRENT_PROGRAM: 0x8B8D, VERTEX_ARRAY_BINDING: 0x85B5, PIXEL_PACK_BUFFER_BINDING: 0x88ED, PIXEL_UNPACK_BUFFER_BINDING: 0x88EF, UNIFORM_BUFFER_BINDING: 0x8A28, ACTIVE_TEXTURE: 0x84E0, SAMPLER_BINDING: 0x8919, TRANSFORM_FEEDBACK_BUFFER_BINDING: 0x8C8F, TRANSFORM_FEEDBACK_BINDING: 0x8E25, COPY_READ_BUFFER_BINDING: 0x8F36, COPY_WRITE_BUFFER_BINDING: 0x8F37,
  TEXTURE_WIDTH: 0x1000, TEXTURE_HEIGHT: 0x1001, TEXTURE_INTERNAL_FORMAT: 0x1003, TEXTURE_DEPTH: 0x8071, TEXTURE_RED_SIZE: 0x805C, TEXTURE_GREEN_SIZE: 0x805D, TEXTURE_BLUE_SIZE: 0x805E, TEXTURE_ALPHA_SIZE: 0x805F, TEXTURE_DEPTH_SIZE: 0x884A, TEXTURE_COMPRESSED: 0x86A1,
  GENERATE_MIPMAP: 0x8191, TEXTURE_MAX_ANISOTROPY_EXT: 0x84FE, TEXTURE_BORDER_COLOR: 0x1004, TEXTURE_LOD_BIAS: 0x8501, TEXTURE_PRIORITY: 0x8066, TEXTURE_SWIZZLE_R: 0x8E42, TEXTURE_SWIZZLE_RGBA: 0x8E46, DEPTH_TEXTURE_MODE: 0x884B,
  BUFFER_SIZE: 0x8764, BUFFER_USAGE: 0x8765, BUFFER_MAPPED: 0x88BC, BUFFER_MAP_POINTER: 0x88BD, BUFFER_ACCESS: 0x88BB, READ_ONLY: 0x88B8, WRITE_ONLY: 0x88B9, READ_WRITE: 0x88BA, MAP_READ_BIT: 1, MAP_WRITE_BIT: 2, MAP_INVALIDATE_RANGE_BIT: 4, MAP_INVALIDATE_BUFFER_BIT: 8, MAP_FLUSH_EXPLICIT_BIT: 0x10, MAP_UNSYNCHRONIZED_BIT: 0x20,
  VERTEX_ATTRIB_ARRAY_ENABLED: 0x8622, VERTEX_ATTRIB_ARRAY_SIZE: 0x8623, VERTEX_ATTRIB_ARRAY_STRIDE: 0x8624, VERTEX_ATTRIB_ARRAY_TYPE: 0x8625, VERTEX_ATTRIB_ARRAY_NORMALIZED: 0x886A, VERTEX_ATTRIB_ARRAY_POINTER: 0x8645, VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: 0x889F, VERTEX_ATTRIB_ARRAY_DIVISOR: 0x88FE, VERTEX_ATTRIB_ARRAY_INTEGER: 0x88FD, CURRENT_VERTEX_ATTRIB: 0x8626,
  QUERY_RESULT: 0x8866, QUERY_RESULT_AVAILABLE: 0x8867, CURRENT_QUERY: 0x8865, SAMPLES_PASSED: 0x8914, ANY_SAMPLES_PASSED: 0x8C2F, ANY_SAMPLES_PASSED_CONSERVATIVE: 0x8D6A, TIME_ELAPSED: 0x88BF, TIMESTAMP: 0x8E28, PRIMITIVES_GENERATED: 0x8C87, TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN: 0x8C88,
  SYNC_STATUS: 0x9114, OBJECT_TYPE: 0x9112, SYNC_CONDITION: 0x9113, SYNC_FLAGS: 0x9115, ALREADY_SIGNALED: 0x911A, TIMEOUT_EXPIRED: 0x911B, CONDITION_SATISFIED: 0x911C, WAIT_FAILED: 0x911D,
  POINT_SIZE: 0x0B11, LINE_WIDTH: 0x0B21, FILL: 0x1B02, LINE: 0x1B01, POINT: 0x1B00, FRONT_AND_BACK: 0x0408, BACK: 0x0405, FRONT: 0x0404, NONE: 0, COLOR_ATTACHMENT0: 0x8CE0,
  MULTISAMPLE: 0x809D, ALPHA_TEST: 0x0BC0, LIGHTING: 0x0B50, POINT_SMOOTH: 0x0B10, LINE_SMOOTH: 0x0B20, POLYGON_SMOOTH: 0x0B41, TEXTURE_CUBE_MAP_SEAMLESS: 0x884F, FRAMEBUFFER_SRGB: 0x8DB9, PROGRAM_POINT_SIZE: 0x8642, POINT_SPRITE: 0x8861, PRIMITIVE_RESTART: 0x8F9D, PRIMITIVE_RESTART_FIXED_INDEX: 0x8D69, DEPTH_CLAMP: 0x864F, COLOR_MATERIAL: 0x0B57, NORMALIZE: 0x0BA1, FOG: 0x0B60, TEXTURE_GEN_S: 0x0C60, TEXTURE_GEN_T: 0x0C61, TEXTURE_GEN_R: 0x0C62, TEXTURE_GEN_Q: 0x0C63, COLOR_SUM: 0x8458, CLIP_PLANE0: 0x3000, SAMPLE_ALPHA_TO_ONE: 0x809F, LINE_STIPPLE: 0x0B24, POLYGON_STIPPLE: 0x0B42, COLOR_LOGIC_OP: 0x0BF2, INDEX_LOGIC_OP: 0x0BF1, DEBUG_OUTPUT: 0x92E0, DEBUG_OUTPUT_SYNCHRONOUS: 0x8242, SAMPLE_SHADING: 0x8C36,
  MAX_TEXTURE_SIZE: 0x0D33, MAX_VERTEX_ATTRIBS: 0x8869, MAX_TEXTURE_IMAGE_UNITS: 0x8872, MAX_COMBINED_TEXTURE_IMAGE_UNITS: 0x8B4D, MAX_VARYING_FLOATS: 0x8B4B, MAX_VERTEX_UNIFORM_COMPONENTS: 0x8B4A, MAX_FRAGMENT_UNIFORM_COMPONENTS: 0x8B49, MAX_TEXTURE_UNITS: 0x84E2, MAX_MODELVIEW_STACK_DEPTH: 0x0D36, MAX_PROJECTION_STACK_DEPTH: 0x0D38, MAX_LIGHTS: 0x0D31, MAX_CLIP_PLANES: 0x0D32, MAX_ATTRIB_STACK_DEPTH: 0x0D35, MAX_LIST_NESTING: 0x0B31, MAX_EVAL_ORDER: 0x0D30, MAX_PIXEL_MAP_TABLE: 0x0D34, MAX_NAME_STACK_DEPTH: 0x0D37, MAX_TEXTURE_COORDS: 0x8871, MAX_VERTEX_TEXTURE_IMAGE_UNITS: 0x8B4C, MAX_COLOR_ATTACHMENTS: 0x8CDF, MAX_DRAW_BUFFERS: 0x8824, MAX_RENDERBUFFER_SIZE: 0x84E8, MAX_SAMPLES: 0x8D57, MAX_ELEMENTS_VERTICES: 0x80E8, MAX_ELEMENTS_INDICES: 0x80E9, MAX_CUBE_MAP_TEXTURE_SIZE: 0x851C, MAX_3D_TEXTURE_SIZE: 0x8073, MAX_ARRAY_TEXTURE_LAYERS: 0x88FF, MAX_VERTEX_UNIFORM_VECTORS: 0x8DFB, MAX_FRAGMENT_UNIFORM_VECTORS: 0x8DFD, MAX_VARYING_VECTORS: 0x8DFC, MAX_TEXTURE_LOD_BIAS: 0x84FD, MAX_VERTEX_OUTPUT_COMPONENTS: 0x9122, MAX_FRAGMENT_INPUT_COMPONENTS: 0x9125, MAX_UNIFORM_BLOCK_SIZE: 0x8A30, MAX_UNIFORM_BUFFER_BINDINGS: 0x8A2F, MAX_GEOMETRY_TEXTURE_IMAGE_UNITS: 0x8C29, MAX_TEXTURE_BUFFER_SIZE: 0x8C2B, MAX_RECTANGLE_TEXTURE_SIZE: 0x84F8, MAX_COLOR_TEXTURE_SAMPLES: 0x910E, MAX_DEPTH_TEXTURE_SAMPLES: 0x910F, MAX_INTEGER_SAMPLES: 0x9110, MAX_DUAL_SOURCE_DRAW_BUFFERS: 0x88FC, MAX_SERVER_WAIT_TIMEOUT: 0x9111, MAX_VIEWPORT_DIMS: 0x0D3A, ALIASED_POINT_SIZE_RANGE: 0x846D, ALIASED_LINE_WIDTH_RANGE: 0x846E, SMOOTH_POINT_SIZE_RANGE: 0x0B12, SMOOTH_LINE_WIDTH_RANGE: 0x0B22, POINT_SIZE_RANGE: 0x0B12, LINE_WIDTH_RANGE: 0x0B22, SUBPIXEL_BITS: 0x0D50, RED_BITS: 0x0D52, GREEN_BITS: 0x0D53, BLUE_BITS: 0x0D54, ALPHA_BITS: 0x0D55, DEPTH_BITS: 0x0D56, STENCIL_BITS: 0x0D57, DOUBLEBUFFER: 0x0C32, STEREO: 0x0C33, AUX_BUFFERS: 0x0C00, SAMPLE_BUFFERS: 0x80A8, SAMPLES: 0x80A9, NUM_COMPRESSED_TEXTURE_FORMATS: 0x86A2, COMPRESSED_TEXTURE_FORMATS: 0x86A3, NUM_SHADER_BINARY_FORMATS: 0x8DF9, NUM_PROGRAM_BINARY_FORMATS: 0x87FE, IMPLEMENTATION_COLOR_READ_TYPE: 0x8B9A, IMPLEMENTATION_COLOR_READ_FORMAT: 0x8B9B, POLYGON_MODE: 0x0B40, VIEWPORT: 0x0BA2, SCISSOR_BOX: 0x0C10, COLOR_CLEAR_VALUE: 0x0C22, DEPTH_RANGE: 0x0B70, MATRIX_MODE: 0x0BA0, MODELVIEW_MATRIX: 0x0BA6, PROJECTION_MATRIX: 0x0BA7, SHADE_MODEL: 0x0B54, UNIFORM_BUFFER_OFFSET_ALIGNMENT: 0x8A34, MIN_PROGRAM_TEXEL_OFFSET: 0x8904, MAX_PROGRAM_TEXEL_OFFSET: 0x8905, MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS: 0x8C8B, MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS: 0x8C8A, MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS: 0x8C80, MAX_ELEMENT_INDEX: 0x8D6B, MAX_VERTEX_UNIFORM_BLOCKS: 0x8A2B, MAX_FRAGMENT_UNIFORM_BLOCKS: 0x8A2D, MAX_COMBINED_UNIFORM_BLOCKS: 0x8A2E, MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS: 0x8A31, MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS: 0x8A33, CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT: 1, CONTEXT_CORE_PROFILE_BIT: 1,
  FRAMEBUFFER_COMPLETE: 0x8CD5, FRAMEBUFFER_UNSUPPORTED: 0x8CDD, DRAW_BUFFER: 0x0C01, READ_BUFFER: 0x0C02, DRAW_BUFFER0: 0x8825,
  FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE: 0x8CD0, FRAMEBUFFER_ATTACHMENT_OBJECT_NAME: 0x8CD1, TEXTURE: 0x1702, RENDERBUFFER_WIDTH: 0x8D42, RENDERBUFFER_HEIGHT: 0x8D43, RENDERBUFFER_INTERNAL_FORMAT: 0x8D44, RENDERBUFFER_SAMPLES: 0x8CAB,
  STREAM_DRAW: 0x88E0, STATIC_DRAW: 0x88E4, DYNAMIC_DRAW: 0x88E8, UNIFORM_TYPE: 0x8A37, UNIFORM_SIZE: 0x8A38, UNIFORM_NAME_LENGTH: 0x8A39, UNIFORM_BLOCK_INDEX: 0x8A3A, UNIFORM_OFFSET: 0x8A3B, UNIFORM_ARRAY_STRIDE: 0x8A3C, UNIFORM_MATRIX_STRIDE: 0x8A3D, UNIFORM_IS_ROW_MAJOR: 0x8A3E, UNIFORM_BLOCK_BINDING: 0x8A3F, UNIFORM_BLOCK_DATA_SIZE: 0x8A40, UNIFORM_BLOCK_NAME_LENGTH: 0x8A41, UNIFORM_BLOCK_ACTIVE_UNIFORMS: 0x8A42, UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES: 0x8A43, UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER: 0x8A44, UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER: 0x8A46, INVALID_INDEX: 0xFFFFFFFF,
  UNSIGNED_SHORT_5_6_5: 0x8363, UNSIGNED_SHORT_4_4_4_4: 0x8033, UNSIGNED_SHORT_5_5_5_1: 0x8034, UNSIGNED_SHORT_5_6_5_REV: 0x8364, UNSIGNED_SHORT_4_4_4_4_REV: 0x8365, UNSIGNED_SHORT_1_5_5_5_REV: 0x8366, UNSIGNED_INT_8_8_8_8: 0x8035, UNSIGNED_INT_8_8_8_8_REV: 0x8367, UNSIGNED_INT_10_10_10_2: 0x8036, UNSIGNED_INT_2_10_10_10_REV: 0x8368, UNSIGNED_INT_24_8: 0x84FA, UNSIGNED_INT_10F_11F_11F_REV: 0x8C3B, UNSIGNED_INT_5_9_9_9_REV: 0x8C3E, FLOAT_32_UNSIGNED_INT_24_8_REV: 0x8DAD, UNSIGNED_BYTE_3_3_2: 0x8032, UNSIGNED_BYTE_2_3_3_REV: 0x8362,
  TEXTURE0: 0x84C0, TEXTURE_CUBE_MAP_POSITIVE_X: 0x8515, TEXTURE_RECTANGLE: 0x84F5, TEXTURE_BUFFER: 0x8C2A, TEXTURE_2D_MULTISAMPLE: 0x9100, TEXTURE_MIN_FILTER: 0x2801, TEXTURE_MAG_FILTER: 0x2800, TEXTURE_WRAP_S: 0x2802, TEXTURE_WRAP_T: 0x2803, TEXTURE_WRAP_R: 0x8072, TEXTURE_BASE_LEVEL: 0x813C, TEXTURE_MAX_LEVEL: 0x813D, TEXTURE_MIN_LOD: 0x813A, TEXTURE_MAX_LOD: 0x813B, TEXTURE_COMPARE_MODE: 0x884C, TEXTURE_COMPARE_FUNC: 0x884D, CLAMP: 0x2900, CLAMP_TO_EDGE: 0x812F, CLAMP_TO_BORDER: 0x812D, MIRROR_CLAMP_TO_EDGE: 0x8743,
  COMPRESSED_RGB_S3TC_DXT1_EXT: 0x83F0, COMPRESSED_RGBA_S3TC_DXT1_EXT: 0x83F1, COMPRESSED_RGBA_S3TC_DXT3_EXT: 0x83F2, COMPRESSED_RGBA_S3TC_DXT5_EXT: 0x83F3, COMPRESSED_RGB: 0x84ED, COMPRESSED_RGBA: 0x84EE,
  DEBUG_SOURCE_API: 0x8246, DEBUG_TYPE_ERROR: 0x824C, DEBUG_SEVERITY_HIGH: 0x9146,
} as const;

interface TexInfo { target: number; levels: Map<number, { w: number; h: number; d: number; ifmt: number }>; genMipmap: boolean; up?: string; minf?: number; magf?: number; }
/** kind: 0 = never bound, 1 = "other data" (vertex/pixel/uniform/copy targets), 2 = element array. WebGL2 forbids one buffer object serving both kinds, so a buffer a program
 *  uses for both (e.g. an index buffer uploaded through GL_ARRAY_BUFFER, which desktop GL allows) gets a second object (twin) of the other kind, kept in sync lazily (stale: 1 = main object outdated, 2 = twin outdated) */
interface BufInfo { size: number; usage: number; version: number; shadow: Uint8Array | null; kind: number; twin: WebGLBuffer | null; stale: number; }
interface Attrib { enabled: boolean; size: number; type: number; normalized: boolean; integer: boolean; stride: number; pointer: bigint; buffer: number; divisor: number; }
interface MapInfo { buffer: number; target: number; offset: number; length: number; ptr: bigint; access: number; }
interface ProgInfo { uniforms: Map<string, number>; locs: WebGLUniformLocation[]; sources: Map<number, { sid: number; src: string; compiled: string }>; mats: Map<number, Float32Array>; }   /* stage (GL_VERTEX_SHADER / GL_FRAGMENT_SHADER) → source as it was at link time: games detach and delete their shaders right after linking */

function typeBytes(type: number): number {
  switch (type) {
    case GL.UNSIGNED_BYTE: case GL.BYTE: case GL.UNSIGNED_BYTE_3_3_2: case GL.UNSIGNED_BYTE_2_3_3_REV: return 1;
    case GL.UNSIGNED_SHORT: case GL.SHORT: case GL.HALF_FLOAT: case 0x8D61 /* HALF_FLOAT_OES */: return 2;
    case GL.UNSIGNED_INT: case GL.INT: case GL.FLOAT: return 4; case GL.DOUBLE: return 8;
    case GL.UNSIGNED_SHORT_5_6_5: case GL.UNSIGNED_SHORT_4_4_4_4: case GL.UNSIGNED_SHORT_5_5_5_1: case GL.UNSIGNED_SHORT_5_6_5_REV: case GL.UNSIGNED_SHORT_4_4_4_4_REV: case GL.UNSIGNED_SHORT_1_5_5_5_REV: return 2;
    case GL.UNSIGNED_INT_8_8_8_8: case GL.UNSIGNED_INT_8_8_8_8_REV: case GL.UNSIGNED_INT_10_10_10_2: case GL.UNSIGNED_INT_2_10_10_10_REV: case GL.UNSIGNED_INT_24_8: case GL.UNSIGNED_INT_10F_11F_11F_REV: case GL.UNSIGNED_INT_5_9_9_9_REV: return 4;
    case GL.FLOAT_32_UNSIGNED_INT_24_8_REV: return 8;
    default: return 0; } }
function isPacked(type: number): boolean { return typeBytes(type) > 0 && type !== GL.UNSIGNED_BYTE && type !== GL.BYTE && type !== GL.UNSIGNED_SHORT && type !== GL.SHORT && type !== GL.UNSIGNED_INT && type !== GL.INT && type !== GL.FLOAT && type !== GL.HALF_FLOAT && type !== 0x8D61 && type !== GL.DOUBLE; }
function formatComponents(format: number): number {
  switch (format) {
    case GL.RGBA: case GL.BGRA: case GL.RGBA_INTEGER: case 0x8D9B /* BGRA_INTEGER */: return 4;
    case GL.RGB: case GL.BGR: case GL.RGB_INTEGER: return 3;
    case GL.RG: case GL.LUMINANCE_ALPHA: case GL.RG_INTEGER: return 2;
    case GL.RED: case GL.ALPHA: case GL.LUMINANCE: case GL.DEPTH_COMPONENT: case GL.STENCIL_INDEX: case GL.RED_INTEGER: case 0x1900 /* COLOR_INDEX */: case 0x1904 /* GREEN */: case 0x1905 /* BLUE */: return 1;
    case GL.DEPTH_STENCIL: return 1;
    default: return 4; } }
/** bytes of one pixel for (format, type) */
function pixelBytes(format: number, type: number): number { const tb = typeBytes(type); if (!tb) return 0; return isPacked(type) ? tb : tb * formatComponents(format); }

import { FixedFunction, type FFHost } from './glff';

/** glEnable/glDisable capabilities WebGL2 accepts — validated here instead of with a synchronous gl.getError() per call */
const WEBGL_CAPS = new Set<number>([0x0BE2, 0x0B44, 0x0B71, 0x0BD0, 0x8037, 0x809E, 0x80A0, 0x0C11, 0x0B90, 0x8C89]);   // BLEND CULL_FACE DEPTH_TEST DITHER POLYGON_OFFSET_FILL SAMPLE_ALPHA_TO_COVERAGE SAMPLE_COVERAGE SCISSOR_TEST STENCIL_TEST RASTERIZER_DISCARD
/** glPixelStorei parameters WebGL2 accepts */
const WEBGL_PIXELSTORE = new Set<number>([0x0D05, 0x0CF5, 0x0CF2, 0x0CF3, 0x0CF4, 0x806E, 0x806D, 0x0D02, 0x0D03, 0x0D04]);   // PACK_ALIGNMENT UNPACK_ALIGNMENT UNPACK_ROW_LENGTH UNPACK_SKIP_ROWS UNPACK_SKIP_PIXELS UNPACK_IMAGE_HEIGHT UNPACK_SKIP_IMAGES PACK_ROW_LENGTH PACK_SKIP_ROWS PACK_SKIP_PIXELS
export class GLBridge {
  private ff: FixedFunction;
  gl: WebGL2RenderingContext | null = null;
  private canvas: OffscreenCanvas | HTMLCanvasElement | null = null;
  log: (t: string) => void = () => {};
  /** call tracer (diagnostics): localStorage nebula_gltrace = "<afterFrame>:<calls>" → every GL call after that presented frame is logged with decoded arguments */
  private traceAfter = -1; private traceLeft = 0; private frameNo = 0;
  mem: GLMem | null = null;
  private impl: (Impl | null)[] = [];
  private names: string[] = GL_FUNCS.map((f) => f[0]);
  private byName = new Map<string, number>();
  private err = 0;
  private once = new Set<string>();
  private width = 0; private height = 0;
  // GL names ↔ WebGL objects
  private tex = new Map<number, WebGLTexture>(); private texInfo = new Map<number, TexInfo>(); private texIds = new Map<WebGLTexture, number>();
  private buf = new Map<number, WebGLBuffer>(); private bufInfo = new Map<number, BufInfo>(); private bufIds = new Map<WebGLBuffer, number>();
  private prog = new Map<number, WebGLProgram>(); private progInfo = new Map<number, ProgInfo>(); private progIds = new Map<WebGLProgram, number>();
  private shader = new Map<number, WebGLShader>(); private shaderSrc = new Map<number, string>(); private shaderType = new Map<number, number>(); private shaderIds = new Map<WebGLShader, number>();
  private fbo = new Map<number, WebGLFramebuffer>(); private fboIds = new Map<WebGLFramebuffer, number>();
  private rbo = new Map<number, WebGLRenderbuffer>(); private rboIds = new Map<WebGLRenderbuffer, number>(); private rboInfo = new Map<number, { w: number; h: number; ifmt: number; samples: number }>();
  private vao = new Map<number, WebGLVertexArrayObject>(); private vaoIds = new Map<WebGLVertexArrayObject, number>();
  private query = new Map<number, WebGLQuery>(); private queryIds = new Map<WebGLQuery, number>(); private queryTarget = new Map<number, number>();
  private sampler = new Map<number, WebGLSampler>(); private samplerIds = new Map<WebGLSampler, number>();
  private tfb = new Map<number, WebGLTransformFeedback>(); private tfbIds = new Map<WebGLTransformFeedback, number>();
  private sync = new Map<bigint, WebGLSync>(); private nextSync = 0x5000n;
  private next = { tex: 1, buf: 1, prog: 1, shader: 1, fbo: 1, rbo: 1, vao: 1, query: 1, sampler: 1, tfb: 1 };
  // bindings we must know (WebGL cannot tell us GL names) and shadow vertex-attribute state (client arrays, getters)
  private bound = { array: 0, element: 0, unpack: 0, pack: 0, uniform: 0, copyRead: 0, copyWrite: 0, tfb: 0, vao: 0, fbDraw: 0, fbRead: 0, rbo: 0, program: 0, unit: 0 };
  private texUnits: Map<number, number>[] = [];   // unit → (target → tex id)
  private attribs = new Map<number, Attrib[]>();  // per VAO id (0 = default)
  private maps: MapInfo[] = [];
  /** Guest addresses cease to exist when a new program replaces its process. */
  resetGuestStrings() { this.strings.clear(); }
  private strings = new Map<string, bigint>();    // guest copies of glGetString results
  private extList: string[] = [];
  private scratchVbo: WebGLBuffer | null = null; private scratchIbo: WebGLBuffer | null = null; private quadIbo: WebGLBuffer | null = null; private quadIboCount = 0;
  private unpackAlign = 4; private unpackRowLength = 0; private unpackSkipRows = 0; private unpackSkipPixels = 0; private unpackImageHeight = 0;
  private packAlign = 4; private packRowLength = 0;
  private s3tc: WEBGL_compressed_texture_s3tc | null = null; private aniso: EXT_texture_filter_anisotropic | null = null; private floatLinear = false; private colorBufferFloat = false;
  private maxAttribs = 16; private drawBuffer: number = GL.BACK;

  constructor() { this.names.forEach((n, i) => this.byName.set(n, i)); this.impl = new Array(this.names.length).fill(null); this.ff = new FixedFunction(this.ffHost()); this.register(); }
  private ffHost(): FFHost {
    return {
      gl: () => this.gl!, def: (n, f) => this.def(n, f), warn: (k, m) => this.warn(k, m), setErr: (e) => this.setErr(e),
      f32: (a, n) => this.f32(a, n), f64: (a, n) => this.f64(a, n), i32: (a, n) => this.i32(a, n), u8: (a, n, w) => this.u8(a, n, w), view: (a, n, t) => this.view(a, n, t),
      wrf32: (a, v) => this.wrf32(a, v), wrf64: (a, v) => this.wrf64(a, v), wr32: (a, v) => this.wr32(a, v), wrp: (a, v) => this.wrp(a, v), gstr: (t) => this.gstr(t), rdstr: (a) => this.rdstr(a),
      alloc: (n) => this.mem!.alloc(n), free: (p) => this.mem!.free(p),
      pixels: (ptr, w, h, format, type) => this.pixels(ptr, w, h, 1, format, type), texIdOn: (unit, target) => this.texUnits[unit]?.get(this.texTarget(target)) ?? 0,
      texHasAlpha: (id) => { const ti = this.texInfo.get(id); const f = ti?.levels.get(0)?.ifmt ?? 0; return f === GL.RGBA || f === GL.RGBA8 || f === GL.LUMINANCE_ALPHA || f === GL.ALPHA || f === 0x8058 || f === 0x8C43 || f === 0x881A || f === 0x8814 || f === 0x8056 || f === 0x8057 || f === 0x8051 + 0x100; },
      bound: () => this.bound, bufObj: (id) => this.bufFor(id, GL.ARRAY_BUFFER), elemObj: (id) => this.bufFor(id, GL.ELEMENT_ARRAY_BUFFER), vaoObj: (id) => this.vao.get(id) ?? null, progObj: (id) => this.prog.get(id) ?? null,
      callImpl: (name, args) => { const id = this.byName.get(name); const f = id !== undefined ? this.impl[id] : null; return f ? f(BigUint64Array.from(args)) : undefined; },
      unpackAlign: () => this.unpackAlign, viewport: () => Array.from(this.gl!.getParameter(this.gl!.VIEWPORT) as Int32Array),
    };
  }

  /** a new process starts: the previous program's context, objects and counters go away (the next host_gl_init creates a fresh WebGL2 context) */
  dispose(): void { const gl = this.gl; if (!gl) return; try { gl.getExtension('WEBGL_lose_context')?.loseContext(); } catch { /* already lost */ } this.gl = null; this.canvas = null; }
  /** host_gl_init: (re)size the drawing surface; 1 = WebGL2 ready */
  init(w: number, h: number): number {
    if (!this.gl) {
      try {
        const canvas = typeof OffscreenCanvas !== 'undefined' ? new OffscreenCanvas(w, h) : document.createElement('canvas');
        if (!(canvas instanceof OffscreenCanvas)) { canvas.width = w; canvas.height = h; }
        const gl = canvas.getContext('webgl2', { alpha: true, depth: true, stencil: true, antialias: false, preserveDrawingBuffer: true, premultipliedAlpha: false, powerPreference: 'high-performance' }) as WebGL2RenderingContext | null;
        if (!gl) { this.log('[gl] WebGL2 is not available on this device: OpenGL programs cannot create a context\n'); return 0; }
        this.gl = gl; this.canvas = canvas; this.width = w; this.height = h;
        try { (canvas as unknown as EventTarget).addEventListener('webglcontextlost', (e) => { e.preventDefault(); if (!this.lost) { this.lost = true; this.log(`[gl] the WebGL2 context was LOST at frame ${this.frameNo} after ${this.draws} draws (GPU reset, memory pressure or the app went to the background): every GL call fails from here on — restart the program\n`); } }); } catch { /* no events on this canvas */ }
        try { const t = typeof localStorage !== 'undefined' ? localStorage.getItem('nebula_gltrace') : null; if (t) { const [a, n] = t.split(':'); this.traceAfter = Number(a) || 0; this.traceLeft = -(Number(n) || 400); this.log(`[gl] call trace armed: after frame ${this.traceAfter}, ${-this.traceLeft} calls\n`); } } catch { /* no storage */ }
        this.s3tc = gl.getExtension('WEBGL_compressed_texture_s3tc'); this.aniso = gl.getExtension('EXT_texture_filter_anisotropic'); this.floatLinear = !!gl.getExtension('OES_texture_float_linear'); this.colorBufferFloat = !!gl.getExtension('EXT_color_buffer_float');
        gl.getExtension('EXT_float_blend'); gl.getExtension('OES_texture_half_float_linear');
        this.maxAttribs = gl.getParameter(gl.MAX_VERTEX_ATTRIBS) as number;
        for (let u = 0; u < 32; u++) this.texUnits.push(new Map());
        this.attribs.set(0, this.freshAttribs());
        this.extList = ['GL_ARB_multitexture', 'GL_ARB_texture_cube_map', 'GL_ARB_texture_non_power_of_two', 'GL_ARB_vertex_buffer_object', 'GL_ARB_pixel_buffer_object', 'GL_ARB_shader_objects', 'GL_ARB_vertex_shader', 'GL_ARB_fragment_shader', 'GL_ARB_shading_language_100', 'GL_ARB_vertex_program', 'GL_ARB_fragment_program',
          'GL_ARB_framebuffer_object', 'GL_EXT_framebuffer_object', 'GL_EXT_framebuffer_blit', 'GL_EXT_framebuffer_multisample', 'GL_EXT_packed_depth_stencil', 'GL_ARB_depth_texture', 'GL_ARB_shadow', 'GL_ARB_texture_float', 'GL_ARB_half_float_pixel', 'GL_ARB_half_float_vertex', 'GL_ARB_texture_rg', 'GL_ARB_texture_compression', 'GL_ARB_vertex_array_object', 'GL_ARB_map_buffer_range', 'GL_ARB_copy_buffer', 'GL_ARB_uniform_buffer_object', 'GL_ARB_sync', 'GL_ARB_instanced_arrays', 'GL_ARB_draw_instanced', 'GL_ARB_draw_buffers', 'GL_ARB_occlusion_query2', 'GL_ARB_ES2_compatibility', 'GL_ARB_ES3_compatibility', 'GL_ARB_texture_storage', 'GL_ARB_explicit_attrib_location', 'GL_ARB_seamless_cube_map', 'GL_ARB_fragment_coord_conventions', 'GL_ARB_texture_swizzle', 'GL_ARB_sampler_objects', 'GL_ARB_transform_feedback2', 'GL_ARB_framebuffer_sRGB', 'GL_EXT_texture_sRGB', 'GL_EXT_texture_array', 'GL_EXT_blend_func_separate', 'GL_EXT_blend_equation_separate', 'GL_EXT_blend_minmax', 'GL_EXT_blend_subtract', 'GL_EXT_blend_color', 'GL_ARB_point_sprite', 'GL_ARB_point_parameters', 'GL_EXT_texture_lod_bias', 'GL_EXT_draw_range_elements', 'GL_EXT_texture3D', 'GL_ARB_texture_rectangle', 'GL_ARB_debug_output', 'GL_KHR_debug', 'GL_EXT_abgr', 'GL_EXT_bgra', 'GL_APPLE_vertex_array_object', 'GL_EXT_gpu_shader4', 'GL_ARB_texture_multisample'];
        if (this.s3tc) this.extList.push('GL_EXT_texture_compression_s3tc', 'GL_EXT_texture_compression_dxt1');
        if (this.aniso) this.extList.push('GL_EXT_texture_filter_anisotropic');
        if (this.colorBufferFloat) this.extList.push('GL_ARB_color_buffer_float');
        this.log(`[gl] WebGL2 context ready: ${this.describeRenderer()}\n`);
      } catch (e) { this.log(`[gl] WebGL2 init failed: ${(e as Error).message}\n`); return 0; }
    }
    if (w !== this.width || h !== this.height) { this.canvas!.width = w; this.canvas!.height = h; this.width = w; this.height = h; }
    return 1;
  }
  private trace(id: number, a: BigUint64Array): void {
    this.traceLeft--;
    const name = this.names[id]; const n = Math.min(a.length, 8);
    const ints = Array.from(a.slice(0, n)).map((v) => { const u = U(v); return v > 0xffffffffn ? '0x' + v.toString(16) : (u > 0x10000 ? '0x' + u.toString(16) : String(I(v))); });
    let extra = '';
    try {
      if (/^glClearColor$|^glBlendColor$|^glUniform[1-4]f$|^glVertexAttrib[1-4]f$/.test(name)) extra = ' floats=' + Array.from(a.slice(0, n)).map((v) => F(v).toPrecision(4)).join(',');
      else if (/^glUniformMatrix[234]fv$/.test(name)) { const k = name[15] === '2' ? 4 : name[15] === '3' ? 9 : 16; const v = this.f32(a[3], k * Math.max(1, I(a[1]))); extra = ' m=[' + (v ? Array.from(v.slice(0, k)).map((x) => x.toPrecision(4)).join(',') : '?') + ']'; }
      else if (/^glUniform[1-4]fv$/.test(name)) { const k = Number(name[9]) * Math.max(1, I(a[1])); const v = this.f32(a[2], Math.min(k, 16)); extra = ' v=[' + (v ? Array.from(v).map((x) => x.toPrecision(4)).join(',') : '?') + ']'; }
      else if (/^glTex(Sub)?Image2D$/.test(name)) { const sub = name === 'glTexSubImage2D'; const w = I(a[sub ? 4 : 3]), h = I(a[sub ? 5 : 4]), fmt = U(a[6]), type = U(a[7]), ptr = a[8]; const bpp = fmt === GL.RGBA || fmt === 0x80E1 ? 4 : fmt === GL.RGB ? 3 : 1; const len = Math.min(w * h * bpp, 1 << 20); const v = ptr && this.bound.unpack === 0 ? this.u8(ptr, len) : null; let nz = 0; if (v) for (let i = 0; i < v.length; i += 7) if (v[i]) nz++; extra = ` ${w}x${h} fmt=0x${fmt.toString(16)} type=0x${type.toString(16)} ptr=0x${ptr.toString(16)} nonzero≈${v ? Math.round(nz * 7 / Math.max(1, v.length) * 100) : '?'}%`; }
      else if (/^glDrawArrays$|^glDrawElements$/.test(name)) extra = this.drawState();
    } catch (e) { extra = ' (trace error: ' + (e as Error).message + ')'; }
    const line = `[gltrace] ${name}(${ints.join(', ')})${extra}`;
    this.log(line + '\n'); const gt = globalThis as unknown as { __gltrace?: string[] }; (gt.__gltrace ??= []).push(line);
    if (this.traceLeft === 0) this.log('[gltrace] --- end of trace ---\n');
  }
  /** the state a draw call sees: viewport, program, blend/scissor/depth, bound texture and the first two vertices of every enabled attribute (client pointer or VBO) */
  private drawState(): string {
    let extra = '';
    {
      {
        const attrs = this.curAttribs(); const parts: string[] = [];
        for (let i = 0; i < attrs.length; i++) { const at = attrs[i]; if (!at.enabled) continue;
          let sample = ''; if (!at.buffer && at.pointer && at.type === GL.FLOAT) { const st = at.stride || at.size * 4; const v0 = this.f32(at.pointer, at.size), v1 = this.f32(at.pointer + BigInt(st), at.size); sample = ' v0=[' + (v0 ? Array.from(v0).map((x) => x.toPrecision(4)).join(',') : '?') + '] v1=[' + (v1 ? Array.from(v1).map((x) => x.toPrecision(4)).join(',') : '?') + ']'; }
          else if (!at.buffer && at.pointer && at.type === GL.UNSIGNED_BYTE) { const st = at.stride || at.size; const v0 = this.u8(at.pointer, at.size), v1 = this.u8(at.pointer + BigInt(st), at.size); sample = ' v0=[' + (v0 ? Array.from(v0).join(',') : '?') + '] v1=[' + (v1 ? Array.from(v1).join(',') : '?') + ']'; }
          else if (at.buffer) sample = this.vboSample(at);
          parts.push(`a${i}:${at.size}x0x${at.type.toString(16)}${at.normalized ? 'n' : ''} stride=${at.stride} ${at.buffer ? 'vbo' + at.buffer + '+' + at.pointer.toString() : 'ptr=0x' + at.pointer.toString(16)}${sample}`); }
        const gl = this.gl!; const prog = gl.getParameter(gl.CURRENT_PROGRAM) as WebGLProgram | null; const vp = Array.from(gl.getParameter(gl.VIEWPORT) as Int32Array).join(',');
        extra = ` viewport=[${vp}] prog=${prog ? this.bound.program : 'NONE'} blend=${gl.isEnabled(gl.BLEND)} scissor=${gl.isEnabled(gl.SCISSOR_TEST)} depth=${gl.isEnabled(gl.DEPTH_TEST)} tex2d=${this.texUnits[0]?.get(GL.TEXTURE_2D) ?? 0}${this.texBrief(this.texUnits[0]?.get(GL.TEXTURE_2D) ?? 0)} | ${parts.join(' | ')}`;
      }
    }
    return extra;
  }
  /** vertex digest: the first draws of frames 241 and 1201 (right after the surface probes) land in the Report by themselves — what the program feeds the GPU
   *  (vertex colours with alpha 0, texcoords, positions outside the viewport, no program) is visible without pressing the trace button */
  private digestLeft = 0; private digests: string[] = []; private digestPending: { idx: number; q: WebGLQuery }[] = [];
  /** everything about one draw call that a black surface could be blamed on: the GL error it raised, colour mask / stencil / discard state, the real element-buffer binding,
   *  index statistics from the element buffer (range, degenerate triangles), the bounding box of the positions actually drawn, buffer sizes against what the draw needs, the mat4 uniforms */
  private drawDeep(mode: number, count: number, type: number, indices: bigint, first: number): string {
    const gl = this.gl!; const out: string[] = [];
    try {
      const err = gl.getError(); if (err) out.push(`GL ERROR BEFORE DRAW 0x${err.toString(16)}`);
      const cm = gl.getParameter(gl.COLOR_WRITEMASK) as boolean[]; if (!cm.every(Boolean)) out.push(`colorMask=[${cm.map(Number).join(',')}] (COLOUR WRITES OFF)`);
      if (gl.isEnabled(gl.STENCIL_TEST)) out.push(`STENCIL TEST ON func=0x${(gl.getParameter(gl.STENCIL_FUNC) as number).toString(16)} ref=${gl.getParameter(gl.STENCIL_REF)} mask=0x${(gl.getParameter(gl.STENCIL_VALUE_MASK) as number).toString(16)}`);
      if (gl.isEnabled(gl.RASTERIZER_DISCARD)) out.push('RASTERIZER_DISCARD ON'); if (gl.isEnabled(gl.SAMPLE_COVERAGE)) out.push('SAMPLE_COVERAGE ON'); if (gl.isEnabled(gl.SAMPLE_ALPHA_TO_COVERAGE)) out.push('ALPHA_TO_COVERAGE ON');
      if (gl.isEnabled(gl.CULL_FACE)) out.push(`cull=0x${(gl.getParameter(gl.CULL_FACE_MODE) as number).toString(16)} front=0x${(gl.getParameter(gl.FRONT_FACE) as number).toString(16)}`);
      if (gl.isEnabled(gl.DEPTH_TEST)) out.push(`depthFunc=0x${(gl.getParameter(gl.DEPTH_FUNC) as number).toString(16)} range=[${Array.from(gl.getParameter(gl.DEPTH_RANGE) as Float32Array).join(',')}]`);
      const fbs = gl.checkFramebufferStatus(gl.DRAW_FRAMEBUFFER); if (fbs !== gl.FRAMEBUFFER_COMPLETE) out.push(`FRAMEBUFFER INCOMPLETE 0x${fbs.toString(16)}`);
      out.push(`mode=${mode} blendFunc=0x${(gl.getParameter(gl.BLEND_SRC_RGB) as number).toString(16)}/0x${(gl.getParameter(gl.BLEND_DST_RGB) as number).toString(16)} a=0x${(gl.getParameter(gl.BLEND_SRC_ALPHA) as number).toString(16)}/0x${(gl.getParameter(gl.BLEND_DST_ALPHA) as number).toString(16)}`);
      /* indices */
      let minI = first, maxI = first + count - 1, degenerate = -1;
      if (type) {
        const realIbo = gl.getParameter(gl.ELEMENT_ARRAY_BUFFER_BINDING) as WebGLBuffer | null; const expect = this.bufFor(this.bound.element, GL.ELEMENT_ARRAY_BUFFER);
        if (realIbo !== expect) out.push(`ELEMENT BUFFER MISMATCH: GL has ${realIbo ? 'buffer ' + (this.bufIds.get(realIbo) ?? '?') : 'none'}, bridge thinks ${this.bound.element}`);
        const info = this.bufInfo.get(this.bound.element); const bytes = count * typeBytes(type);
        if (!this.bound.element) out.push(`client index array at 0x${indices.toString(16)}`);
        else if (!info || Number(indices) + bytes > info.size) out.push(`INDEX RANGE BEYOND IBO: offset ${indices} + ${bytes} B > size ${info?.size ?? 0}`);
        const raw = this.bound.element ? this.elementData(Number(indices), bytes) : this.u8(indices, bytes);
        if (raw) { const arr = type === GL.UNSIGNED_INT ? new Uint32Array(raw.buffer, raw.byteOffset, count) : type === GL.UNSIGNED_SHORT ? new Uint16Array(raw.buffer, raw.byteOffset, count) : raw;
          minI = 0xffffffff; maxI = 0; for (let i = 0; i < arr.length; i++) { const v = arr[i]; if (v < minI) minI = v; if (v > maxI) maxI = v; }
          if (mode === GL.TRIANGLES) { degenerate = 0; for (let i = 0; i + 2 < arr.length; i += 3) if (arr[i] === arr[i + 1] || arr[i] === arr[i + 2] || arr[i + 1] === arr[i + 2]) degenerate++; }
          out.push(`indices ${count}x${type === GL.UNSIGNED_INT ? 'u32' : type === GL.UNSIGNED_SHORT ? 'u16' : 'u8'} range ${minI}..${maxI}${degenerate >= 0 ? ` degenerate ${degenerate}/${Math.floor(count / 3)} tris` : ''} first=[${Array.from(arr.slice(0, 6)).join(',')}]${this.bound.element ? ` ibo ${this.bound.element} size ${info?.size ?? '?'}` : ''}`); }
        else out.push('indices unreadable');
      } else out.push(`vertices ${first}..${first + count - 1}`);
      /* vertex buffers: size against what the draw addresses, and the positions actually drawn */
      const attrs = this.curAttribs();
      for (let i = 0; i < attrs.length; i++) { const a = attrs[i]; if (!a.enabled) continue; const es = a.size * typeBytes(a.type); const stride = a.stride || es; const need = Number(a.pointer) + stride * maxI + es;
        if (a.buffer) { const info = this.bufInfo.get(a.buffer); if (!info || need > info.size) out.push(`ATTRIB ${i} BEYOND VBO ${a.buffer}: needs ${need} B, buffer ${info?.size ?? 0} B → WebGL draws nothing (INVALID_OPERATION)`); }
        if (i === 0 && a.type === GL.FLOAT && a.size >= 2 && maxI >= minI && maxI - minI < 20000) {
          let data: Uint8Array | null = null; const len = stride * (maxI - minI) + es;
          if (a.buffer) { const b = this.bufFor(a.buffer, GL.ARRAY_BUFFER); if (b) { const prev = this.bufFor(this.bound.array, GL.ARRAY_BUFFER); gl.bindBuffer(gl.ARRAY_BUFFER, b); data = new Uint8Array(len); try { gl.getBufferSubData(gl.ARRAY_BUFFER, Number(a.pointer) + stride * minI, data); } catch { data = null; } gl.bindBuffer(gl.ARRAY_BUFFER, prev); } }
          else data = this.u8(a.pointer + BigInt(stride * minI), len);
          if (data) { const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity]; const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
            for (let v = minI; v <= maxI; v++) { const o = (v - minI) * stride; for (let c = 0; c < Math.min(3, a.size); c++) { const f = dv.getFloat32(o + c * 4, true); if (f < mn[c]) mn[c] = f; if (f > mx[c]) mx[c] = f; } }
            out.push(`positions x[${mn[0].toPrecision(4)}..${mx[0].toPrecision(4)}] y[${mn[1].toPrecision(4)}..${mx[1].toPrecision(4)}]${a.size >= 3 ? ` z[${mn[2].toPrecision(4)}..${mx[2].toPrecision(4)}]` : ''}`); } } }
      const pi = this.progInfo.get(this.bound.program); if (pi && pi.mats.size) { const names = new Map<number, string>(); for (const [n, l] of pi.uniforms) names.set(l, n); out.push('mat4 ' + Array.from(pi.mats).map(([l, m]) => `${names.get(l) ?? 'loc' + l}=[${Array.from(m).map((x) => +x.toPrecision(4)).join(',')}]`).join(' ')); }
    } catch (e) { out.push(`(deep state failed: ${(e as Error).message})`); }
    return out.join(' · ');
  }
  private digestBegin(kind: string, mode: number, count: number, type: number, indices: bigint, first: number): { idx: number; q: WebGLQuery | null } {
    this.digestLeft--; const gl = this.gl!; let line = `f${this.frameNo} ${kind}(${count})`;
    try { line += this.drawState() + '\n      ' + this.drawDeep(mode, count, type, indices, first); } catch (e) { line += ` digest failed (${(e as Error).message})`; }
    this.digests.push(line); if (this.digests.length > 32) this.digests.splice(0, this.digests.length - 32);
    let q: WebGLQuery | null = null; try { q = gl.createQuery(); if (q) gl.beginQuery(gl.ANY_SAMPLES_PASSED, q); } catch { q = null; }
    return { idx: this.digests.length - 1, q };
  }
  private digestEnd(d: { idx: number; q: WebGLQuery | null }): void {
    const gl = this.gl!; const err = gl.getError(); if (err) this.digests[d.idx] += ` · DRAW RAISED GL ERROR 0x${err.toString(16)}${err === GL.INVALID_OPERATION ? ' (INVALID_OPERATION: index/attribute beyond its buffer, no element buffer, or incomplete program/framebuffer — nothing was drawn)' : ''}`;
    if (d.q) { try { gl.endQuery(gl.ANY_SAMPLES_PASSED); this.digestPending.push({ idx: d.idx, q: d.q }); } catch { /* ignore */ } }
  }
  private digestResolve(): void {
    const gl = this.gl; if (!gl || !this.digestPending.length) return;
    this.digestPending = this.digestPending.filter((p) => { try { if (!gl.getQueryParameter(p.q, gl.QUERY_RESULT_AVAILABLE)) return true; const passed = gl.getQueryParameter(p.q, gl.QUERY_RESULT) as number | boolean; gl.deleteQuery(p.q);
      if (this.digests[p.idx] !== undefined) this.digests[p.idx] += passed ? ' · fragments passed: YES (geometry reached the framebuffer — look at blend/colour/texture)' : ' · fragments passed: NONE (nothing rasterized: off-screen geometry, degenerate indices, culling, scissor, stencil or an error)'; return false; } catch { return false; } });
  }
  /** trace the next presented frame's GL calls into the log (Report tab button on the phone, where localStorage is out of reach) */
  armTrace(calls = 1500) { this.traceAfter = this.frameNo + 1; this.traceLeft = -calls; this.log(`[gl] call trace armed: next frame (${this.frameNo + 1}), ${calls} calls\n`); }
  /** running totals for the report: frames presented through GL, draws, programs/shaders/textures/framebuffers created, shader failures */
  summary(): string { const gl = this.gl; if (!gl) return ''; let vp = '', prog = '', fbo = '';
    try { vp = Array.from(gl.getParameter(gl.VIEWPORT) as Int32Array).join(','); prog = gl.getParameter(gl.CURRENT_PROGRAM) ? 'yes' : 'none'; fbo = gl.getParameter(gl.FRAMEBUFFER_BINDING) ? 'offscreen' : 'default'; } catch { /* lost context */ }
    return `GL: ${this.lost || gl.isContextLost() ? 'CONTEXT LOST · ' : ''}${this.frameNo} frames presented · ${this.draws} draw calls · ${this.prog.size} programs · ${this.shader.size} shaders · ${this.badShaders} shader/link failures · surface ${this.width}x${this.height} · viewport [${vp}] · program ${prog} · framebuffer ${fbo} · ${this.describeRenderer()}${this.probes.length ? '\nGL surface probes (16×16 samples of the presented frame): ' + this.probes.join(' | ') : ''}${this.digests.length ? '\nGL vertex digest (first draws of the frame after a probe; v0/v1 = the first two vertices of each attribute, 0x1401n = normalized bytes; second line = error / masks / index and position ranges / mat4 uniforms / occlusion query):\n  ' + this.digests.join('\n  ') : ''}` + (this.textureTable() ? '\n' + this.textureTable() : '') + (this.shaderDump() ? '\n' + this.shaderDump() : ''); }
  private draws = 0; private badShaders = 0; private linkCount = 0;
  private describeRenderer(): string { const gl = this.gl!; const dbg = gl.getExtension('WEBGL_debug_renderer_info'); return dbg ? String(gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL)) : String(gl.getParameter(gl.RENDERER)); }
  probe(id: number): number { return this.impl[id] ? 1 : 0; }

  /** host_gl_call */
  call(id: number, a: BigUint64Array): bigint {
    const f = this.impl[id];
    if (!f) { this.setErr(GL.INVALID_OPERATION); this.warn(this.names[id], `${this.names[id]}: not available in the WebGL2 bridge (GL_INVALID_OPERATION)`); return 0n; }
    if (this.ff.recording && this.ff.record(id, this.names[id], a, this.mem!.psize())) return 0n;   // glNewList … glEndList: stored, not executed
    const tracing = this.traceLeft > 0; if (tracing) this.trace(id, a);
    try {
      const r = f(a);
      if (tracing && /^glDraw(Arrays|Elements)/.test(this.names[id])) this.log('[gltrace]  ' + this.drawProbe() + '\n');
      if (typeof r === 'bigint') return r; if (typeof r === 'number') return BigInt.asUintN(64, BigInt(Math.trunc(r))); return 0n;
    } catch (e) {
      this.warn('exc:' + this.names[id], `${this.names[id]}: ${(e as Error).message}`); this.setErr(GL.INVALID_OPERATION); return 0n;
    }
  }
  /** swap-chain pacing: at most two presented frames may still be in flight on the GPU (fences polled without blocking) */
  private fences: WebGLSync[] = [];
  swapReady(maxInFlight = 2): boolean {
    const gl = this.gl; if (!gl) return true;
    while (this.fences.length && gl.getSyncParameter(this.fences[0], gl.SYNC_STATUS) === gl.SIGNALED) { gl.deleteSync(this.fences[0]); this.fences.shift(); }
    return this.fences.length < maxInFlight;
  }
  swapFence(): void { const gl = this.gl; if (!gl) return; const f = gl.fenceSync(gl.SYNC_GPU_COMMANDS_COMPLETE, 0); if (f) this.fences.push(f); gl.flush(); }
  /** the WebGL drawing surface (null until the program created a context) — composited on the GPU by present.ts */
  surface(): OffscreenCanvas | HTMLCanvasElement | null { return this.gl ? this.canvas : null; }
  /** one presented frame (both paths: GPU composition of the surface and the readback fallback) — frame counter, call-trace arming,
   *  and at a few frames a 16×16-sample brightness probe of the default framebuffer (tells "the surface is black" from "the surface is fine
   *  but not shown" without a screenshot) */
  private probes: string[] = [];
  private probeSurface(w: number, h: number): void {
    const gl = this.gl; if (!gl || w < 2 || h < 2) return;
    try {
      const readFb = gl.getParameter(gl.READ_FRAMEBUFFER_BINDING) as WebGLFramebuffer | null; gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
      const packBuf = this.bound.pack; if (packBuf) gl.bindBuffer(gl.PIXEL_PACK_BUFFER, null);
      gl.pixelStorei(gl.PACK_ALIGNMENT, 4); gl.pixelStorei(gl.PACK_ROW_LENGTH, 0); gl.pixelStorei(gl.PACK_SKIP_ROWS, 0); gl.pixelStorei(gl.PACK_SKIP_PIXELS, 0);
      const px = new Uint8Array(4); let sum = 0, max = 0, lit = 0, n = 0, alpha0 = 0;
      for (let j = 0; j < 16; j++) for (let i = 0; i < 16; i++) { gl.readPixels(Math.floor((i + 0.5) * w / 16), Math.floor((j + 0.5) * h / 16), 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        const l = Math.max(px[0], px[1], px[2]); sum += l; if (l > max) max = l; if (l > 16) lit++; if (px[3] === 0) alpha0++; n++; }
      if (packBuf) gl.bindBuffer(gl.PIXEL_PACK_BUFFER, packBuf); gl.bindFramebuffer(gl.READ_FRAMEBUFFER, readFb);
      const cc = gl.getParameter(gl.COLOR_CLEAR_VALUE) as Float32Array;
      this.probes.push(`f${this.frameNo} ${w}x${h}: mean ${Math.round(sum / n)} max ${max} lit ${Math.round(lit * 100 / n)}% alpha0 ${Math.round(alpha0 * 100 / n)}% clear(${cc[0].toFixed(2)},${cc[1].toFixed(2)},${cc[2].toFixed(2)}) blend ${gl.isEnabled(gl.BLEND) ? 'on' : 'off'} depth ${gl.isEnabled(gl.DEPTH_TEST) ? 'on' : 'off'} scissor ${gl.isEnabled(gl.SCISSOR_TEST) ? 'on' : 'off'} cull ${gl.isEnabled(gl.CULL_FACE) ? 'on' : 'off'} textures ${this.tex.size} tex2d ${gl.getParameter(gl.TEXTURE_BINDING_2D) ? 'bound' : 'none'} fbo ${gl.getParameter(gl.FRAMEBUFFER_BINDING) ? 'offscreen' : 'default'}`);
    } catch (e) { this.probes.push(`f${this.frameNo}: probe failed (${(e as Error).message})`); }
  }
  private lost = false;
  markFrame(w: number, h: number): void {
    this.digestResolve();
    if (!this.lost && this.gl && this.frameNo % 30 === 0 && this.gl.isContextLost()) { this.lost = true; this.log(`[gl] the WebGL2 context was LOST at frame ${this.frameNo} after ${this.draws} draws (GPU reset, memory pressure or the app went to the background): every GL call fails from here on — restart the program\n`); }
    this.frameNo++; const gt = globalThis as unknown as { __gltrace?: string[] };
    if (this.frameNo === 30 || this.frameNo === 240 || this.frameNo === 1200) { this.probeSurface(w, h); this.digestLeft = 8;
      /* a dark surface after 240 presented frames with draw calls going on → record the next frame by itself, so the Report already carries the trace */
      if ((this.frameNo === 240 || this.frameNo === 1200) && this.draws > 0 && this.traceLeft <= 0 && this.traceAfter < 0 && / max (\d+) /.exec(this.probes[this.probes.length - 1] ?? '')?.[1] !== undefined && Number(/ max (\d+) /.exec(this.probes[this.probes.length - 1])![1]) < 16) { this.log(`[gl] surface black at frame ${this.frameNo} with draw calls going on: tracing the next frame automatically\n`); this.armTrace(1500); } }
    if (this.traceLeft < 0 && this.frameNo >= this.traceAfter) { this.traceLeft = -this.traceLeft; this.traceAfter = -1; const l = `[gltrace] --- frame ${this.frameNo}: tracing ${this.traceLeft} calls (surface ${this.width}x${this.height}, present ${w}x${h}) ---`; this.log(l + '\n'); (gt.__gltrace ??= []).push(l); }
    else if (this.traceLeft > 0) { const l = `[gltrace] --- present frame ${this.frameNo} (${w}x${h}) ---`; this.log(l + '\n'); (gt.__gltrace ??= []).push(l); }
  }
  /** host_gl_present fallback (no presenter on screen): default framebuffer → BGRA rows (top first) at dst (wasm offset) */
  present(dst: Uint32Array, w: number, h: number): void {
    const gl = this.gl; if (!gl) return;
    this.markFrame(w, h);
    const rgba = new Uint8Array(w * h * 4);
    const readFb = gl.getParameter(gl.READ_FRAMEBUFFER_BINDING) as WebGLFramebuffer | null;
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
    gl.pixelStorei(gl.PACK_ALIGNMENT, 4); gl.pixelStorei(gl.PACK_ROW_LENGTH, 0); gl.pixelStorei(gl.PACK_SKIP_ROWS, 0); gl.pixelStorei(gl.PACK_SKIP_PIXELS, 0);
    const packBuf = this.bound.pack; if (packBuf) gl.bindBuffer(gl.PIXEL_PACK_BUFFER, null);
    gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, rgba);
    if (packBuf) gl.bindBuffer(gl.PIXEL_PACK_BUFFER, this.bufFor(packBuf, GL.PIXEL_PACK_BUFFER));
    gl.pixelStorei(gl.PACK_ALIGNMENT, this.packAlign); gl.pixelStorei(gl.PACK_ROW_LENGTH, this.packRowLength);
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, readFb);
    const src = new Uint32Array(rgba.buffer);
    for (let y = 0; y < h; y++) { const so = (h - 1 - y) * w, doff = y * w; for (let x = 0; x < w; x++) { const p = src[so + x]; dst[doff + x] = 0xff000000 | (p & 0x0000ff00) | ((p & 0xff) << 16) | ((p >>> 16) & 0xff); } }
  }

  // ---- helpers ----
  private setErr(e: number) { if (!this.err) this.err = e; }
  private warn(key: string, msg: string) { if (this.once.has(key)) return; this.once.add(key); this.log(`[gl] ${msg}\n`); }
  private u8(addr: bigint, len: number, write = false): Uint8Array | null { if (!len) return new Uint8Array(0); const o = this.mem!.ptr(addr, len, write); if (!o) { this.setErr(GL.INVALID_OPERATION); this.warn('badptr', `pointer 0x${addr.toString(16)} (${len} bytes) is not mapped in the program`); return null; } return new Uint8Array(this.mem!.buffer(), o, len); }
  private view(addr: bigint, len: number, type: number, write = false): ArrayBufferView | null {
    const o = this.mem!.ptr(addr, len, write); if (!o) { this.setErr(GL.INVALID_OPERATION); this.warn('badptr', `pointer 0x${addr.toString(16)} (${len} bytes) is not mapped in the program`); return null; }
    const buf = this.mem!.buffer(); const tb = typeBytes(type) || 1; const n = Math.floor(len / tb);
    const C = type === GL.FLOAT ? Float32Array : type === GL.INT ? Int32Array : (type === GL.UNSIGNED_INT || tb === 4) ? Uint32Array : type === GL.SHORT ? Int16Array : tb === 2 ? Uint16Array : type === GL.BYTE ? Int8Array : tb === 8 ? Float64Array : Uint8Array;
    if (o % C.BYTES_PER_ELEMENT) { const copy = new Uint8Array(len); copy.set(new Uint8Array(buf, o, len)); return new C(copy.buffer, 0, n); }   // misaligned guest data: copy
    return new C(buf, o, n); }
  private i32(addr: bigint, n: number, write = false): Int32Array | null { const o = this.mem!.ptr(addr, n * 4, write); if (!o) { this.setErr(GL.INVALID_OPERATION); return null; } if (o % 4) { const c = new Uint8Array(n * 4); c.set(new Uint8Array(this.mem!.buffer(), o, n * 4)); return new Int32Array(c.buffer); } return new Int32Array(this.mem!.buffer(), o, n); }
  private f32(addr: bigint, n: number, write = false): Float32Array | null { const o = this.mem!.ptr(addr, n * 4, write); if (!o) { this.setErr(GL.INVALID_OPERATION); return null; } if (o % 4) { const c = new Uint8Array(n * 4); c.set(new Uint8Array(this.mem!.buffer(), o, n * 4)); return new Float32Array(c.buffer); } return new Float32Array(this.mem!.buffer(), o, n); }
  private f64(addr: bigint, n: number): Float64Array | null { const o = this.mem!.ptr(addr, n * 8, false); if (!o) { this.setErr(GL.INVALID_OPERATION); return null; } if (o % 8) { const c = new Uint8Array(n * 8); c.set(new Uint8Array(this.mem!.buffer(), o, n * 8)); return new Float64Array(c.buffer); } return new Float64Array(this.mem!.buffer(), o, n); }
  private wr32(addr: bigint, v: number) { if (!addr) return; const o = this.mem!.ptr(addr, 4, true); if (o) new DataView(this.mem!.buffer()).setInt32(o, v | 0, true); }
  private wru32(addr: bigint, v: number) { if (!addr) return; const o = this.mem!.ptr(addr, 4, true); if (o) new DataView(this.mem!.buffer()).setUint32(o, v >>> 0, true); }
  private wrf32(addr: bigint, v: number) { if (!addr) return; const o = this.mem!.ptr(addr, 4, true); if (o) new DataView(this.mem!.buffer()).setFloat32(o, v, true); }
  private wr64(addr: bigint, v: bigint) { if (!addr) return; const o = this.mem!.ptr(addr, 8, true); if (o) new DataView(this.mem!.buffer()).setBigUint64(o, BigInt.asUintN(64, v), true); }
  private wrf64(addr: bigint, v: number) { if (!addr) return; const o = this.mem!.ptr(addr, 8, true); if (o) new DataView(this.mem!.buffer()).setFloat64(o, v, true); }
  private wrp(addr: bigint, v: bigint) { if (this.mem!.psize() === 8) this.wr64(addr, v); else this.wru32(addr, Number(v & 0xffffffffn)); }
  private rdp(addr: bigint): bigint { const ps = this.mem!.psize(); const o = this.mem!.ptr(addr, ps, false); if (!o) return 0n; const dv = new DataView(this.mem!.buffer()); return ps === 8 ? dv.getBigUint64(o, true) : BigInt(dv.getUint32(o, true)); }
  private rdstr(addr: bigint, len = -1): string { if (!addr) return ''; const n = len >= 0 ? len : this.mem!.strlen(addr); const b = this.u8(addr, n); return b ? new TextDecoder().decode(b) : ''; }
  /** write a NUL-terminated string into (buf, cap) and the length (without NUL) into lenPtr — glGet*InfoLog semantics */
  private wrstr(buf: bigint, cap: number, s: string, lenPtr: bigint) { const bytes = new TextEncoder().encode(s); const n = cap > 0 ? Math.min(bytes.length, cap - 1) : 0; if (buf && cap > 0) { const d = this.u8(buf, n + 1, true); if (d) { d.set(bytes.subarray(0, n)); d[n] = 0; } } this.wr32(lenPtr, n); }
  private gstr(s: string): bigint { let p = this.strings.get(s); if (p) return p; const bytes = new TextEncoder().encode(s); p = this.mem!.alloc(bytes.length + 1); const d = this.u8(p, bytes.length + 1, true); if (d) { d.set(bytes); d[bytes.length] = 0; } this.strings.set(s, p); return p; }
  private freshAttribs(): Attrib[] { const a: Attrib[] = []; for (let i = 0; i < this.maxAttribs; i++) a.push({ enabled: false, size: 4, type: GL.FLOAT, normalized: false, integer: false, stride: 0, pointer: 0n, buffer: 0, divisor: 0 }); return a; }
  private curAttribs(): Attrib[] { let a = this.attribs.get(this.bound.vao); if (!a) { a = this.freshAttribs(); this.attribs.set(this.bound.vao, a); } return a; }
  private texTarget(t: number): number { return t >= GL.TEXTURE_CUBE_MAP_POSITIVE_X && t < GL.TEXTURE_CUBE_MAP_POSITIVE_X + 6 ? GL.TEXTURE_CUBE_MAP : t === GL.TEXTURE_1D ? GL.TEXTURE_2D : t; }   // 1D textures live as h=1 2D textures
  private curTex(target: number): number { return this.texUnits[this.bound.unit]?.get(this.texTarget(target)) ?? 0; }
  private bufTargetId(target: number): number { switch (target) { case GL.ARRAY_BUFFER: return this.bound.array; case GL.ELEMENT_ARRAY_BUFFER: return this.bound.element; case GL.PIXEL_PACK_BUFFER: return this.bound.pack; case GL.PIXEL_UNPACK_BUFFER: return this.bound.unpack; case GL.UNIFORM_BUFFER: return this.bound.uniform; case GL.COPY_READ_BUFFER: return this.bound.copyRead; case GL.COPY_WRITE_BUFFER: return this.bound.copyWrite; case GL.TRANSFORM_FEEDBACK_BUFFER: return this.bound.tfb; default: return 0; } }
  private gen(n: number, out: bigint, kind: keyof GLBridge['next'], make: () => number) { for (let i = 0; i < n; i++) this.wru32(out + BigInt(i * 4), make()); }
  private ids(n: number, p: bigint): number[] { const v = this.i32(p, n); return v ? Array.from(v, (x) => x >>> 0) : []; }
  private legacy(name: string) { this.setErr(GL.INVALID_OPERATION); this.warn('legacy:' + name, `${name}: the fixed-function pipeline is not available in this (core-profile, WebGL2-backed) context → GL_INVALID_OPERATION`); }
  private defaultDrawBuffer(mode: number): number { return mode === GL.BACK || mode === GL.FRONT || mode === 0x0403 /* FRONT_LEFT */ || mode === 0x0402 /* BACK_LEFT */ ? this.gl!.BACK : mode === GL.NONE ? GL.NONE : mode; }
  private unmapIfMapped(bufId: number) { for (let i = this.maps.length - 1; i >= 0; i--) if (this.maps[i].buffer === bufId) this.maps.splice(i, 1); }

  /** desktop GLSL → ESSL for WebGL2: #version ≤ 120 shaders are accepted as ESSL 1.00 (precision added), 130–330 are rewritten to 300 es */
  private translate(src: string, type: number): string {
    const frag = type === GL.FRAGMENT_SHADER;
    const m = /^\s*#version\s+(\d+)(\s+\w+)?/m.exec(src); const ver = m ? parseInt(m[1], 10) : 0;
    const body = src.replace(/^\s*#version[^\n]*\n?/m, '').replace(/^\s*#extension\s+GL_ARB_explicit_attrib_location[^\n]*\n?/mg, '').replace(/^\s*#extension\s+GL_ARB_(?:shading_language_420pack|separate_shader_objects|gpu_shader5)[^\n]*\n?/mg, '');
    const hasPrecision = /precision\s+(lowp|mediump|highp)\s+float/.test(body);
    const precision100 = hasPrecision ? '' : 'precision highp float;\nprecision highp int;\n';
    const precision = frag ? (hasPrecision ? '' : 'precision highp float;\nprecision highp int;\nprecision highp sampler2DArray;\nprecision highp sampler3D;\nprecision highp samplerCube;\nprecision highp isampler2D;\nprecision highp usampler2D;\n') : precision100;
    if (ver <= 120) {
      // ESSL 1.00: no "layout", no integer bit ops; gl_FragData[n] stays (WebGL2 allows it in 100 shaders? no → rewrite to 300 es when used)
      if (!/gl_FragData|\blayout\s*\(|\btexture2DLod\b|\btexture2DGradARB\b|\btexelFetch\b|\bshadow2D\b/.test(body)) return precision100 + body;
    }
    // → 300 es
    let out = body;
    const pre = '#version 300 es\n' + precision + '#define NEBULA_GLES3 1\n';
    out = out.replace(/\battribute\b/g, 'in').replace(/\bvarying\b/g, frag ? 'in' : 'out');
    out = out.replace(/\btexture2DProjLod\b/g, 'textureProjLod').replace(/\btexture2DLod\b|\btextureCubeLod\b|\btexture3DLod\b/g, 'textureLod').replace(/\btexture2DProj\b|\btexture3DProj\b/g, 'textureProj').replace(/\btexture2D\b|\btextureCube\b|\btexture3D\b|\btexture2DArray\b|\btexture2DRect\b|\bshadow2D\b|\bshadow2DProj\b/g, (s) => s === 'shadow2DProj' ? 'textureProj' : 'texture');
    out = out.replace(/\bsampler2DRect\b/g, 'sampler2D').replace(/\btextureSize2D\b/g, 'textureSize');
    if (frag) {
      const usesFragData = /gl_FragData\s*\[/.test(out); const usesFragColor = /\bgl_FragColor\b/.test(out);
      let decl = '';
      if (usesFragColor) { out = out.replace(/\bgl_FragColor\b/g, 'nebula_FragColor'); decl += 'layout(location = 0) out vec4 nebula_FragColor;\n'; }
      if (usesFragData) { const idx = new Set<number>(); out = out.replace(/gl_FragData\s*\[\s*(\d+)\s*\]/g, (_s, i) => { idx.add(parseInt(i, 10)); return `nebula_FragData${i}`; }); for (const i of idx) decl += `layout(location = ${i}) out vec4 nebula_FragData${i};\n`; }
      out = decl + out;
    }
    out = out.replace(/\bgl_ClipVertex\b/g, 'nebula_ClipVertex').replace(/\bgl_FragDepthEXT\b/g, 'gl_FragDepth');
    if (/\bgl_ModelViewProjectionMatrix\b|\bgl_Vertex\b|\bgl_MultiTexCoord0\b|\bgl_Color\b|\bgl_Normal\b|\bgl_TexCoord\b|\bgl_FrontColor\b|\bftransform\s*\(/.test(out)) this.warn('glslcompat', 'shader uses fixed-function GLSL built-ins (gl_Vertex, gl_ModelViewProjectionMatrix, ftransform …) that do not exist in a core/WebGL2 context — it will fail to compile');
    if (ver >= 400) this.warn('glsl4', `#version ${ver} shaders: tessellation / GLSL 4.x features are beyond WebGL2 (GLSL ES 3.00) — compiled best-effort`);
    return pre + out;
  }
  private fmtType(ifmt: number, format: number, type: number): { ifmt: number; format: number; type: number } {
    // legacy numeric internal formats and unsized ↔ sized combinations WebGL2 rejects
    if (ifmt === 1) ifmt = GL.LUMINANCE; else if (ifmt === 2) ifmt = GL.LUMINANCE_ALPHA; else if (ifmt === 3) ifmt = GL.RGB; else if (ifmt === 4) ifmt = GL.RGBA;
    if (ifmt === GL.BGRA) ifmt = GL.RGBA; if (ifmt === GL.BGR) ifmt = GL.RGB;
    if (ifmt === GL.DEPTH_COMPONENT) ifmt = type === GL.UNSIGNED_SHORT ? GL.DEPTH_COMPONENT16 : type === GL.FLOAT ? GL.DEPTH_COMPONENT32F : GL.DEPTH_COMPONENT24;
    if (ifmt === GL.DEPTH_COMPONENT32) ifmt = GL.DEPTH_COMPONENT24;
    if (ifmt === GL.DEPTH_STENCIL) ifmt = GL.DEPTH24_STENCIL8;
    if (ifmt === GL.DEPTH_COMPONENT16 || ifmt === GL.DEPTH_COMPONENT24) { format = GL.DEPTH_COMPONENT; if (type !== GL.UNSIGNED_SHORT && type !== GL.UNSIGNED_INT) type = ifmt === GL.DEPTH_COMPONENT16 ? GL.UNSIGNED_SHORT : GL.UNSIGNED_INT; if (ifmt === GL.DEPTH_COMPONENT24) type = GL.UNSIGNED_INT; }
    if (ifmt === GL.DEPTH_COMPONENT32F) { format = GL.DEPTH_COMPONENT; type = GL.FLOAT; }
    if (ifmt === GL.DEPTH24_STENCIL8) { format = GL.DEPTH_STENCIL; type = GL.UNSIGNED_INT_24_8; }
    if (type === GL.FLOAT && (ifmt === GL.RGBA || ifmt === GL.RGB || ifmt === GL.RED || ifmt === GL.RG)) ifmt = ifmt === GL.RGBA ? GL.RGBA32F : ifmt === GL.RGB ? GL.RGB32F : ifmt === GL.RG ? 0x8230 : 0x822E;
    if ((type === GL.HALF_FLOAT || type === 0x8D61) && (ifmt === GL.RGBA || ifmt === GL.RGB || ifmt === GL.RED || ifmt === GL.RG)) ifmt = ifmt === GL.RGBA ? GL.RGBA16F : ifmt === GL.RGB ? GL.RGB16F : ifmt === GL.RG ? 0x822F : 0x822D;
    if (type === 0x8D61) type = GL.HALF_FLOAT;
    if (ifmt === GL.RGBA16F || ifmt === GL.RGBA32F) { if (format === GL.BGRA) format = GL.RGBA; }
    if (ifmt === GL.SRGB_ALPHA) ifmt = GL.SRGB8_ALPHA8; if (ifmt === GL.SRGB) ifmt = GL.SRGB8;
    if (ifmt === GL.RGBA8 || ifmt === GL.RGB8 || ifmt === GL.SRGB8_ALPHA8 || ifmt === GL.SRGB8 || ifmt === GL.R8 || ifmt === GL.RG8) { if (type === GL.UNSIGNED_INT_8_8_8_8_REV || type === GL.UNSIGNED_INT_8_8_8_8) type = GL.UNSIGNED_BYTE; }
    if (ifmt === 0x805B /* RGBA16 */) ifmt = GL.RGBA8; if (ifmt === 0x8054 /* RGB16 */) ifmt = GL.RGB8; if (ifmt === 0x8056 /* RGBA4 */) ifmt = 0x8056; if (ifmt === 0x8050 /* RGB5 */) ifmt = 0x8D62; if (ifmt === 0x8057 /* RGB5_A1 */) ifmt = 0x8057;
    if (ifmt === 0x8040 /* LUMINANCE8 */) ifmt = GL.LUMINANCE; if (ifmt === 0x8045 /* LUMINANCE8_ALPHA8 */) ifmt = GL.LUMINANCE_ALPHA; if (ifmt === 0x803C /* ALPHA8 */) ifmt = GL.ALPHA; if (ifmt === 0x8049 /* INTENSITY8 */ || ifmt === 0x8041 /* INTENSITY */) ifmt = GL.LUMINANCE;
    if (ifmt === GL.COMPRESSED_RGBA) ifmt = GL.RGBA; if (ifmt === GL.COMPRESSED_RGB) ifmt = GL.RGB;
    if (format === GL.LUMINANCE || format === GL.ALPHA || format === GL.LUMINANCE_ALPHA) { if (ifmt === GL.RGBA) ifmt = format; }
    return { ifmt, format, type }; }
  /** pixel data for glTexImage*: guest bytes (unpack state applied), BGRA/8_8_8_8_REV converted to what WebGL2 accepts */
  private pixels(ptr: bigint, w: number, h: number, d: number, format: number, type: number): { data: ArrayBufferView | null; format: number; type: number } | null {
    if (this.bound.unpack) return { data: null, format, type };   // PIXEL_UNPACK_BUFFER: offset form handled by the caller
    if (!ptr) return { data: null, format, type };
    const bpp = pixelBytes(format, type); if (!bpp) { this.setErr(GL.INVALID_ENUM); return null; }
    const rowPixels = this.unpackRowLength || w; const rowBytes = Math.ceil(rowPixels * bpp / this.unpackAlign) * this.unpackAlign;
    const imgRows = this.unpackImageHeight || h; const total = rowBytes * imgRows * d + (this.unpackSkipRows * rowBytes) + this.unpackSkipPixels * bpp - (d > 1 ? 0 : 0);
    const data = this.view(ptr, Math.max(total, rowBytes * h * d), type); if (!data) return null;
    if (format === GL.BGRA || format === GL.BGR || type === GL.UNSIGNED_INT_8_8_8_8_REV || type === GL.UNSIGNED_INT_8_8_8_8) {
      // convert to RGBA / UNSIGNED_BYTE (tightly packed) — WebGL has no BGRA uploads
      const comps = formatComponents(format); const src = new Uint8Array(data.buffer, data.byteOffset, data.byteLength); const out = new Uint8Array(w * h * d * 4);
      const skipRow = this.unpackSkipRows, skipPx = this.unpackSkipPixels;
      for (let z = 0; z < d; z++) for (let y = 0; y < h; y++) { const so = ((z * imgRows + y + skipRow) * rowBytes) + skipPx * bpp; const dof = (z * h + y) * w * 4;
        for (let x = 0; x < w; x++) { const s = so + x * bpp, o = dof + x * 4;
          if (type === GL.UNSIGNED_INT_8_8_8_8_REV && format === GL.BGRA) { out[o] = src[s + 2]; out[o + 1] = src[s + 1]; out[o + 2] = src[s]; out[o + 3] = src[s + 3]; }
          else if (type === GL.UNSIGNED_INT_8_8_8_8_REV) { out[o] = src[s]; out[o + 1] = src[s + 1]; out[o + 2] = src[s + 2]; out[o + 3] = src[s + 3]; }
          else if (type === GL.UNSIGNED_INT_8_8_8_8) { out[o] = src[s + 3]; out[o + 1] = src[s + 2]; out[o + 2] = src[s + 1]; out[o + 3] = src[s]; }
          else if (format === GL.BGRA) { out[o] = src[s + 2]; out[o + 1] = src[s + 1]; out[o + 2] = src[s]; out[o + 3] = src[s + 3]; }
          else { out[o] = src[s + 2]; out[o + 1] = src[s + 1]; out[o + 2] = src[s]; out[o + 3] = 255; void comps; } } }
      return { data: out, format: GL.RGBA, type: GL.UNSIGNED_BYTE };
    }
    return { data, format, type }; }
  private withUnpackTight<T>(tight: boolean, f: () => T): T { const gl = this.gl!; if (!tight) return f(); gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1); gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0); gl.pixelStorei(gl.UNPACK_SKIP_ROWS, 0); gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, 0); try { return f(); } finally { gl.pixelStorei(gl.UNPACK_ALIGNMENT, this.unpackAlign); gl.pixelStorei(gl.UNPACK_ROW_LENGTH, this.unpackRowLength); gl.pixelStorei(gl.UNPACK_SKIP_ROWS, this.unpackSkipRows); gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, this.unpackSkipPixels); } }
  private noteLevel(target: number, level: number, w: number, h: number, d: number, ifmt: number) { const id = this.curTex(target); const ti = this.texInfo.get(id); if (ti) { ti.levels.set(level, { w, h, d, ifmt }); if (ti.genMipmap && level === 0) this.gl!.generateMipmap(this.texTarget(target)); } }
  /** diagnostics: what the program really uploaded into a texture (format/type, mean value and the share of non-zero bytes of a sample) */
  private noteUpload(target: number, how: string, format: number, type: number, data: ArrayBufferView | null): void {
    const ti = this.texInfo.get(this.curTex(target)); if (!ti) return;
    if (!data) { ti.up = `${how} fmt=0x${format.toString(16)} type=0x${type.toString(16)} (no data)`; return; }
    const b = new Uint8Array(data.buffer, data.byteOffset, data.byteLength); const step = Math.max(1, Math.floor(b.length / 4096)) | 1; let sum = 0, nz = 0, n = 0, a0 = 0, an = 0;
    for (let i = 0; i < b.length; i += step) { sum += b[i]; if (b[i]) nz++; n++; }
    if (format === GL.RGBA || format === GL.BGRA) { const px = b.length >> 2, pstep = Math.max(1, Math.floor(px / 2048)); for (let i = 3; i < b.length; i += 4 * pstep) { an++; if (!b[i]) a0++; } }
    ti.up = `${how} fmt=0x${format.toString(16)} type=0x${type.toString(16)} ${b.length}B mean ${n ? Math.round(sum / n) : 0} nonzero ${n ? Math.round(nz * 100 / n) : 0}%${an ? ` alpha0 ${Math.round(a0 * 100 / an)}%` : ''}`; }
  /** report: every texture with its level-0 size, internal format, filters and upload statistics */
  textureTable(limit = 48): string {
    const rows: string[] = []; let n = 0;
    for (const [id, ti] of this.texInfo) { if (!ti.target && !ti.levels.size) continue; n++; if (rows.length >= limit) continue; const l0 = ti.levels.get(0);
      rows.push(`#${id} ${l0 ? `${l0.w}x${l0.h}${l0.d > 1 ? 'x' + l0.d : ''} ifmt=0x${l0.ifmt.toString(16)}` : 'no level 0'} levels=${ti.levels.size} min=0x${(ti.minf ?? 0x2702).toString(16)} mag=0x${(ti.magf ?? 0x2601).toString(16)}${ti.genMipmap ? ' genmip' : ''}${ti.up ? ' ' + ti.up : ' (never uploaded)'}`); }
    return n ? `GL textures (${n}${n > limit ? `, first ${limit}` : ''}): ` + rows.join(' · ') : ''; }
  /** report: the GLSL the program handed over and what the bridge compiled (first programs only — enough for the sprite shaders of a 2D game) */
  private shaderDumpCache: { key: string; text: string } | null = null;
  shaderDump(maxProgs = 6, maxChars = 1200): string {
    const gl = this.gl; if (!gl) return ''; const key = Array.from(this.prog.keys()).slice(0, maxProgs).join(',') + '|' + this.badShaders + '|' + this.linkCount; if (this.shaderDumpCache && this.shaderDumpCache.key === key) return this.shaderDumpCache.text;   /* shader queries are GPU-process round trips: computed once per set of programs */
    const out: string[] = []; let k = 0;
    for (const [pid, p] of this.prog) { if (k++ >= maxProgs) break; let sh: WebGLShader[] = []; try { sh = gl.getAttachedShaders(p) ?? []; } catch { /* lost */ }
      const snap = this.progInfo.get(pid)?.sources; const stages: { type: string; sid: number; src: string; tr: string; log: string }[] = [];
      for (const s of sh) { const sid = this.shaderIds.get(s) ?? 0; let st = this.shaderType.get(sid) ?? 0; try { if (!st) st = gl.getShaderParameter(s, GL.SHADER_TYPE) as number; } catch { /* lost */ } let tr = ''; try { tr = (gl.getShaderSource(s) ?? '').trim(); } catch { /* lost */ }
        let log = ''; try { log = gl.getShaderParameter(s, GL.COMPILE_STATUS) ? '' : ` COMPILE FAILED: ${(gl.getShaderInfoLog(s) || '').trim().slice(0, 300)}`; } catch { /* lost */ }
        const snapped = snap?.get(st); stages.push({ type: st === GL.FRAGMENT_SHADER ? 'fragment' : 'vertex', sid: sid || snapped?.sid || 0, src: (this.shaderSrc.get(sid) ?? snapped?.src ?? '').trim(), tr, log }); }
      if (!stages.length && snap) for (const [st, v] of snap) stages.push({ type: st === GL.FRAGMENT_SHADER ? 'fragment' : 'vertex', sid: v.sid, src: v.src.trim(), tr: v.compiled.trim(), log: ' (detached and deleted after linking — source as it was at link time)' });
      const parts = stages.map(({ type, sid, src, tr, log }) => { const ok = tr.replace(/\s+/g, ' ') === src.replace(/\s+/g, ' ');
        return `  [${type} shader ${sid}${log}]\n${src.slice(0, maxChars)}${src.length > maxChars ? '\n  …' : ''}${ok ? '' : `\n  [as compiled for WebGL2]\n${tr.slice(0, maxChars)}${tr.length > maxChars ? '\n  …' : ''}`}`; });
      const linked = gl.getProgramParameter(p, GL.LINK_STATUS) ? 'linked' : 'LINK FAILED: ' + (gl.getProgramInfoLog(p) || '').trim().slice(0, 300);
      let attrs = ''; try { const na = gl.getProgramParameter(p, GL.ACTIVE_ATTRIBUTES) as number; const al: string[] = []; for (let i = 0; i < na; i++) { const ai = gl.getActiveAttrib(p, i); if (ai) al.push(`${ai.name}@${gl.getAttribLocation(p, ai.name)}`); } const nu = gl.getProgramParameter(p, GL.ACTIVE_UNIFORMS) as number; const ul: string[] = []; for (let i = 0; i < nu; i++) { const ui = gl.getActiveUniform(p, i); if (ui) ul.push(ui.name); } attrs = ` attribs ${al.join(',') || '-'} uniforms ${ul.join(',') || '-'}`; } catch { /* lost */ }
      out.push(`program ${pid} (${linked}${attrs}):\n${parts.join('\n')}`); }
    const text = out.length ? 'GL shaders:\n' + out.join('\n') : ''; this.shaderDumpCache = { key, text }; return text; }
  /** a 6×6-sample look at the current draw framebuffer right after a traced draw call: did it put anything on the surface? */
  private texBrief(id: number): string { const ti = this.texInfo.get(id); if (!ti) return ''; const l0 = ti.levels.get(0); return ` (${l0 ? `${l0.w}x${l0.h}` : 'no level 0'}${ti.up ? ' ' + ti.up.split(' ').slice(0, 1).join('') + ' ' + (ti.up.match(/mean \d+ nonzero \d+%(?: alpha0 \d+%)?/)?.[0] ?? '') : ' never uploaded'})`; }
  private drawProbe(): string {
    const gl = this.gl; if (!gl) return '';
    try { const vp = gl.getParameter(gl.VIEWPORT) as Int32Array; const w = Math.min(Math.max(1, vp[2]), 1024), h = Math.min(Math.max(1, vp[3]), 1024);   /* one readback of (up to) 1024×1024 of the draw framebuffer: a single GPU sync per draw */
      const readFb = gl.getParameter(gl.READ_FRAMEBUFFER_BINDING) as WebGLFramebuffer | null, drawFb = gl.getParameter(gl.DRAW_FRAMEBUFFER_BINDING) as WebGLFramebuffer | null; if (readFb !== drawFb) gl.bindFramebuffer(gl.READ_FRAMEBUFFER, drawFb);
      const packBuf = this.bound.pack; if (packBuf) gl.bindBuffer(gl.PIXEL_PACK_BUFFER, null); gl.pixelStorei(gl.PACK_ALIGNMENT, 4); gl.pixelStorei(gl.PACK_ROW_LENGTH, 0); gl.pixelStorei(gl.PACK_SKIP_ROWS, 0); gl.pixelStorei(gl.PACK_SKIP_PIXELS, 0);
      if (!this.probeBuf || this.probeBuf.length < w * h * 4) this.probeBuf = new Uint8Array(w * h * 4); const px = this.probeBuf;
      gl.readPixels(vp[0] + ((Math.max(1, vp[2]) - w) >> 1), vp[1] + ((Math.max(1, vp[3]) - h) >> 1), w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
      if (packBuf) gl.bindBuffer(gl.PIXEL_PACK_BUFFER, this.bufFor(packBuf, GL.PIXEL_PACK_BUFFER)); if (readFb !== drawFb) gl.bindFramebuffer(gl.READ_FRAMEBUFFER, readFb); gl.getError();   /* a readback error of ours must not reach the program's glGetError */
      const step = Math.max(1, Math.floor(w * h / 16384)); let lit = 0, a = 0, n = 0, mx = 0;
      for (let i = 0; i < w * h; i += step) { const o = i * 4; const l = Math.max(px[o], px[o + 1], px[o + 2]); if (l > 16) lit++; if (px[o + 3]) a++; if (l > mx) mx = l; n++; }
      return ` → surface after draw: lit ${(lit * 100 / n).toFixed(1)}% max ${mx} alpha>0 ${(a * 100 / n).toFixed(1)}%`; } catch (e) { return ` → probe failed: ${(e as Error).message}`; } }
  private probeBuf: Uint8Array | null = null;
  /** the first two vertices of an attribute that lives in a VBO (getBufferSubData) — for the draw trace */
  private vboSample(at: Attrib): string {
    const gl = this.gl; const b = this.bufFor(at.buffer, GL.ARRAY_BUFFER); if (!gl || !b) return '';
    try { const comp = at.type === GL.FLOAT ? 4 : at.type === GL.UNSIGNED_BYTE || at.type === GL.BYTE ? 1 : at.type === GL.UNSIGNED_SHORT || at.type === GL.SHORT ? 2 : 4; const st = at.stride || at.size * comp; const need = Number(at.pointer) + st + at.size * comp; const prev = this.bufFor(this.bound.array, GL.ARRAY_BUFFER);
      gl.bindBuffer(gl.ARRAY_BUFFER, b); const size = gl.getBufferParameter(gl.ARRAY_BUFFER, gl.BUFFER_SIZE) as number; if (size < need) { gl.bindBuffer(gl.ARRAY_BUFFER, prev); return ` (vbo only ${size}B, needs ${need})`; }
      const raw = new Uint8Array(st + at.size * comp); gl.getBufferSubData(gl.ARRAY_BUFFER, Number(at.pointer), raw); gl.bindBuffer(gl.ARRAY_BUFFER, prev);
      const rd = (off: number) => { const out: string[] = []; for (let i = 0; i < at.size; i++) { const o = off + i * comp; if (at.type === GL.FLOAT) out.push(new Float32Array(raw.buffer.slice(o, o + 4))[0].toPrecision(4)); else if (comp === 1) out.push(String(raw[o])); else if (comp === 2) out.push(String(new Uint16Array(raw.buffer.slice(o, o + 2))[0])); else out.push(String(new Uint32Array(raw.buffer.slice(o, o + 4))[0])); } return out.join(','); };
      return ` v0=[${rd(0)}] v1=[${rd(st)}]`; } catch (e) { return ` (sample failed: ${(e as Error).message})`; } }
  private progInfoOf(id: number): ProgInfo { let p = this.progInfo.get(id); if (!p) { p = { uniforms: new Map(), locs: [], sources: new Map(), mats: new Map() }; this.progInfo.set(id, p); } return p; }
  private uloc(loc: number): WebGLUniformLocation | null { if (loc < 0) return null; const p = this.progInfo.get(this.bound.program); return p ? p.locs[loc] ?? null : null; }
  private queryTargetOf(t: number): number { return t === GL.SAMPLES_PASSED ? GL.ANY_SAMPLES_PASSED : t; }
  /** client-side vertex arrays / GL_QUADS emulation before a draw: returns false when the draw cannot be done */
  private prepareDraw(first: number, count: number, indices: { type: number; ptr: bigint; count: number } | null): { ok: boolean; indexOffset: number } {
    const gl = this.gl!; const attrs = this.curAttribs(); let maxIndex = -1;
    const needClient = attrs.some((a) => a.enabled && !a.buffer);
    let idxArr: Uint32Array | Uint16Array | Uint8Array | null = null;
    if (indices && !this.bound.element) {   // client-side index array → scratch IBO
      const bytes = indices.count * typeBytes(indices.type); const v = this.view(indices.ptr, bytes, indices.type); if (!v) return { ok: false, indexOffset: 0 };
      if (!this.scratchIbo) this.scratchIbo = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.scratchIbo); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, v as ArrayBufferView, gl.STREAM_DRAW);
      idxArr = v as Uint32Array;
    }
    if (needClient) {
      if (indices) { if (!idxArr) { const bytes = indices.count * typeBytes(indices.type); const tmp = this.elementData(Number(indices.ptr), bytes); if (!tmp) return { ok: false, indexOffset: 0 }; idxArr = indices.type === GL.UNSIGNED_INT ? new Uint32Array(tmp.buffer) : indices.type === GL.UNSIGNED_SHORT ? new Uint16Array(tmp.buffer) : tmp; }
        for (let i = 0; i < idxArr.length; i++) if (idxArr[i] > maxIndex) maxIndex = idxArr[i]; }
      else maxIndex = first + count - 1;
      if (!this.scratchVbo) this.scratchVbo = gl.createBuffer();
      // pack every client attribute into one scratch VBO (each attribute at its own offset), re-pointed for this draw
      const parts: { a: Attrib; i: number; data: Uint8Array; off: number }[] = []; let total = 0;
      for (let i = 0; i < attrs.length; i++) { const a = attrs[i]; if (!a.enabled || a.buffer) continue; const es = a.size * typeBytes(a.type); const stride = a.stride || es; const bytes = maxIndex * stride + es; const d = this.u8(a.pointer, bytes); if (!d) return { ok: false, indexOffset: 0 }; parts.push({ a, i, data: d, off: total }); total += (bytes + 3) & ~3; }
      const blob = new Uint8Array(total); for (const p of parts) blob.set(p.data, p.off);
      const prevArray = this.bound.array; gl.bindBuffer(gl.ARRAY_BUFFER, this.scratchVbo); gl.bufferData(gl.ARRAY_BUFFER, blob, gl.STREAM_DRAW);
      for (const p of parts) { const a = p.a; if (a.integer) gl.vertexAttribIPointer(p.i, a.size, a.type, a.stride, p.off); else gl.vertexAttribPointer(p.i, a.size, a.type, a.normalized, a.stride, p.off); }
      gl.bindBuffer(gl.ARRAY_BUFFER, this.bufFor(prevArray, GL.ARRAY_BUFFER));
      this.warn('clientarrays', 'client-side vertex arrays are uploaded to a scratch buffer at every draw (slow path; use VBOs)');
    }
    return { ok: true, indexOffset: indices && !this.bound.element ? 0 : indices ? Number(indices.ptr) : 0 };
  }
  private finishDraw(indices: boolean) { const gl = this.gl!; if (indices && !this.bound.element) gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, null); }
  /** GL_QUADS → indexed triangles (quad i → 4i,4i+1,4i+2, 4i,4i+2,4i+3) */
  private quadIndices(nquads: number): WebGLBuffer { const gl = this.gl!; if (!this.quadIbo || this.quadIboCount < nquads) { const n = Math.max(nquads, 1024); const idx = new Uint32Array(n * 6); for (let i = 0; i < n; i++) { idx[i * 6] = i * 4; idx[i * 6 + 1] = i * 4 + 1; idx[i * 6 + 2] = i * 4 + 2; idx[i * 6 + 3] = i * 4; idx[i * 6 + 4] = i * 4 + 2; idx[i * 6 + 5] = i * 4 + 3; } if (!this.quadIbo) this.quadIbo = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.quadIbo); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STATIC_DRAW); this.quadIboCount = n; } return this.quadIbo!; }
  private getParam(pname: number): number | number[] | boolean | boolean[] | null | bigint {
    const gl = this.gl!;
    switch (pname) {
      case GL.MAJOR_VERSION: return 3; case GL.MINOR_VERSION: return 3; case GL.NUM_EXTENSIONS: return this.extList.length; case GL.CONTEXT_FLAGS: return GL.CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT; case GL.CONTEXT_PROFILE_MASK: return GL.CONTEXT_CORE_PROFILE_BIT;
      case GL.TEXTURE_BINDING_2D: return this.curTex(GL.TEXTURE_2D); case GL.TEXTURE_BINDING_3D: return this.curTex(GL.TEXTURE_3D); case GL.TEXTURE_BINDING_2D_ARRAY: return this.curTex(GL.TEXTURE_2D_ARRAY); case GL.TEXTURE_BINDING_CUBE_MAP: return this.curTex(GL.TEXTURE_CUBE_MAP);
      case GL.ARRAY_BUFFER_BINDING: return this.bound.array; case GL.ELEMENT_ARRAY_BUFFER_BINDING: return this.bound.element; case GL.PIXEL_PACK_BUFFER_BINDING: return this.bound.pack; case GL.PIXEL_UNPACK_BUFFER_BINDING: return this.bound.unpack; case GL.UNIFORM_BUFFER_BINDING: return this.bound.uniform; case GL.COPY_READ_BUFFER_BINDING: return this.bound.copyRead; case GL.COPY_WRITE_BUFFER_BINDING: return this.bound.copyWrite; case GL.TRANSFORM_FEEDBACK_BUFFER_BINDING: return this.bound.tfb;
      case GL.FRAMEBUFFER_BINDING: return this.bound.fbDraw; case GL.READ_FRAMEBUFFER_BINDING: return this.bound.fbRead; case GL.RENDERBUFFER_BINDING: return this.bound.rbo; case GL.CURRENT_PROGRAM: return this.bound.program; case GL.VERTEX_ARRAY_BINDING: return this.bound.vao; case GL.ACTIVE_TEXTURE: return GL.TEXTURE0 + this.bound.unit;
      case GL.SAMPLER_BINDING: { const s = gl.getParameter(gl.SAMPLER_BINDING) as WebGLSampler | null; return s ? this.samplerIds.get(s) ?? 0 : 0; }
      case GL.TRANSFORM_FEEDBACK_BINDING: { const t = gl.getParameter(gl.TRANSFORM_FEEDBACK_BINDING) as WebGLTransformFeedback | null; return t ? this.tfbIds.get(t) ?? 0 : 0; }
      case GL.MAX_TEXTURE_UNITS: case GL.MAX_TEXTURE_COORDS: return 8; case GL.MAX_LIGHTS: return 8; case GL.MAX_CLIP_PLANES: return 6; case GL.MAX_MODELVIEW_STACK_DEPTH: return 32; case GL.MAX_PROJECTION_STACK_DEPTH: return 4; case GL.MAX_ATTRIB_STACK_DEPTH: return 16; case GL.MAX_LIST_NESTING: return 64; case GL.MAX_EVAL_ORDER: return 8; case GL.MAX_PIXEL_MAP_TABLE: return 32; case GL.MAX_NAME_STACK_DEPTH: return 64;
      case GL.MAX_VARYING_FLOATS: return (gl.getParameter(gl.MAX_VARYING_VECTORS) as number) * 4; case GL.MAX_VERTEX_UNIFORM_COMPONENTS: return (gl.getParameter(gl.MAX_VERTEX_UNIFORM_VECTORS) as number) * 4; case GL.MAX_FRAGMENT_UNIFORM_COMPONENTS: return (gl.getParameter(gl.MAX_FRAGMENT_UNIFORM_VECTORS) as number) * 4;
      case GL.MAX_GEOMETRY_TEXTURE_IMAGE_UNITS: return 0; case GL.MAX_TEXTURE_BUFFER_SIZE: return 0; case GL.MAX_RECTANGLE_TEXTURE_SIZE: return gl.getParameter(gl.MAX_TEXTURE_SIZE) as number; case GL.MAX_COLOR_TEXTURE_SAMPLES: case GL.MAX_DEPTH_TEXTURE_SAMPLES: case GL.MAX_INTEGER_SAMPLES: return 1; case GL.MAX_DUAL_SOURCE_DRAW_BUFFERS: return 0;
      case GL.POINT_SIZE_RANGE: case GL.SMOOTH_POINT_SIZE_RANGE: return gl.getParameter(gl.ALIASED_POINT_SIZE_RANGE) as number[]; case GL.LINE_WIDTH_RANGE: return gl.getParameter(gl.ALIASED_LINE_WIDTH_RANGE) as number[];
      case GL.DOUBLEBUFFER: return true; case GL.STEREO: return false; case GL.AUX_BUFFERS: return 0; case GL.POLYGON_MODE: return [GL.FILL, GL.FILL]; case GL.POINT_SIZE: return 1; case GL.MATRIX_MODE: return 0x1700; case GL.SHADE_MODEL: return 0x1D01;
      case GL.NUM_PROGRAM_BINARY_FORMATS: case GL.NUM_SHADER_BINARY_FORMATS: return 0; case GL.DRAW_BUFFER: case GL.DRAW_BUFFER0: return this.drawBuffer; case GL.READ_BUFFER: return gl.getParameter(gl.READ_BUFFER) as number;
      case GL.MAX_SERVER_WAIT_TIMEOUT: return 0; case GL.MIN_PROGRAM_TEXEL_OFFSET: return gl.getParameter(gl.MIN_PROGRAM_TEXEL_OFFSET) as number; case GL.PRIMITIVE_RESTART: return false;
      case GL.TEXTURE_CUBE_MAP_SEAMLESS: case GL.PROGRAM_POINT_SIZE: return true; case GL.MULTISAMPLE: case GL.ALPHA_TEST: case GL.LIGHTING: case GL.FRAMEBUFFER_SRGB: case GL.DEPTH_CLAMP: case GL.POINT_SPRITE: return false;
      case GL.DEPTH_BITS: return gl.getParameter(gl.DEPTH_BITS) as number; case GL.STENCIL_BITS: return gl.getParameter(gl.STENCIL_BITS) as number;
      default: { const fv = this.ff.getParam(pname); if (fv !== undefined) return fv; const v = gl.getParameter(pname); if (gl.getError() === GL.INVALID_ENUM && v === null) { this.setErr(GL.INVALID_ENUM); this.warn('get:' + pname, `glGet*(0x${pname.toString(16)}): unknown parameter → GL_INVALID_ENUM`); return null; }
        if (v && typeof v === 'object' && !Array.isArray(v) && !(v instanceof Float32Array) && !(v instanceof Int32Array) && !(v instanceof Uint32Array)) { const o = v as object; return this.bufIds.get(o as WebGLBuffer) ?? this.texIds.get(o as WebGLTexture) ?? this.progIds.get(o as WebGLProgram) ?? this.fboIds.get(o as WebGLFramebuffer) ?? this.rboIds.get(o as WebGLRenderbuffer) ?? 0; }
        return v as number | number[] | boolean; } } }
  private paramArray(pname: number): number[] | null { const v: unknown = this.getParam(pname); if (v === null) return null; if (typeof v === 'number') return [v]; if (typeof v === 'boolean') return [v ? 1 : 0]; if (typeof v === 'bigint') return [Number(v)]; if (Array.isArray(v)) return v.map((x) => typeof x === 'boolean' ? (x ? 1 : 0) : Number(x)); if (v instanceof Float32Array || v instanceof Int32Array || v instanceof Uint32Array) return Array.from(v); return [0]; }
  private getTexParam(target: number, pname: number): number[] { const gl = this.gl!; if (pname === GL.TEXTURE_MAX_ANISOTROPY_EXT && this.aniso) return [gl.getTexParameter(this.texTarget(target), this.aniso.TEXTURE_MAX_ANISOTROPY_EXT) as number]; if (pname === GL.GENERATE_MIPMAP) return [this.texInfo.get(this.curTex(target))?.genMipmap ? 1 : 0]; if (pname === GL.TEXTURE_BORDER_COLOR) return [0, 0, 0, 0]; if (pname === GL.TEXTURE_LOD_BIAS || pname === GL.TEXTURE_PRIORITY) return [0]; const v = gl.getTexParameter(this.texTarget(target), pname); if (v === null) { this.setErr(GL.INVALID_ENUM); return [0]; } return [typeof v === 'boolean' ? (v ? 1 : 0) : (v as number)]; }
  private texParam(target: number, pname: number, v: number) { const gl = this.gl!; const t = this.texTarget(target);
    switch (pname) {
      case GL.GENERATE_MIPMAP: { const ti = this.texInfo.get(this.curTex(target)); if (ti) ti.genMipmap = !!v; return; }
      case GL.TEXTURE_MAX_ANISOTROPY_EXT: if (this.aniso) gl.texParameterf(t, this.aniso.TEXTURE_MAX_ANISOTROPY_EXT, v); return;
      case GL.TEXTURE_BORDER_COLOR: case GL.TEXTURE_LOD_BIAS: case GL.TEXTURE_PRIORITY: case GL.DEPTH_TEXTURE_MODE: case GL.TEXTURE_SWIZZLE_R: case GL.TEXTURE_SWIZZLE_RGBA: case 0x8E43: case 0x8E44: case 0x8E45: this.warn('texparam:' + pname, `glTexParameter(0x${pname.toString(16)}): not available in WebGL2 (ignored)`); return;
      case GL.TEXTURE_WRAP_S: case GL.TEXTURE_WRAP_T: case GL.TEXTURE_WRAP_R: if (v === GL.CLAMP || v === GL.CLAMP_TO_BORDER || v === GL.MIRROR_CLAMP_TO_EDGE) v = GL.CLAMP_TO_EDGE; gl.texParameteri(t, pname, v); return;
      case GL.TEXTURE_MIN_LOD: case GL.TEXTURE_MAX_LOD: gl.texParameterf(t, pname, v); return;
      case GL.TEXTURE_MIN_FILTER: case GL.TEXTURE_MAG_FILTER: { const ti = this.texInfo.get(this.curTex(target)); if (ti) { if (pname === GL.TEXTURE_MIN_FILTER) ti.minf = v | 0; else ti.magf = v | 0; } gl.texParameteri(t, pname, v | 0); return; }
      default: gl.texParameteri(t, pname, v | 0); } }
  private fbStatus(target: number): number { return this.gl!.checkFramebufferStatus(target === GL.FRAMEBUFFER ? this.gl!.FRAMEBUFFER : target); }
  private rboFormat(ifmt: number): number { switch (ifmt) { case GL.DEPTH_COMPONENT: case GL.DEPTH_COMPONENT32: return GL.DEPTH_COMPONENT24; case GL.DEPTH_STENCIL: return GL.DEPTH24_STENCIL8; case GL.RGBA: case 4: case 0x805B: return GL.RGBA8; case GL.RGB: case 3: case 0x8054: return GL.RGB8; case GL.STENCIL_INDEX: case 0x8D46: case 0x8D47: case 0x8D49: return GL.STENCIL_INDEX8; case GL.RED: return GL.R8; case GL.RG: return GL.RG8; case GL.SRGB_ALPHA: return GL.SRGB8_ALPHA8; default: return ifmt; } }

  // ---- entry points ----
  private def(name: string, f: Impl) { const id = this.byName.get(name); if (id === undefined) { this.log(`[gl] internal: ${name} is not in gltable.ts\n`); return; } this.impl[id] = f; }
  private callName(name: string, args: bigint[]) { const id = this.byName.get(name); const f = id !== undefined ? this.impl[id] : null; return f ? f(BigUint64Array.from(args)) : undefined; }
  private alias(name: string, of: string) { const id = this.byName.get(of); if (id !== undefined && this.impl[id]) this.def(name, this.impl[id]!); }
  private register() {
    const g = () => this.gl!;
    // -- state --
    this.def('glGetError', () => { const e = this.err; if (e) { this.err = 0; return e; } return g().getError(); });
    this.def('glEnable', (a) => this.enable(U(a[0]), true)); this.def('glDisable', (a) => this.enable(U(a[0]), false));
    this.def('glIsEnabled', (a) => { const cap = U(a[0]); const ffv = this.ff.isEnabled(cap); if (ffv !== undefined) return ffv; if (cap === GL.MULTISAMPLE || cap === GL.FRAMEBUFFER_SRGB || cap === GL.POINT_SMOOTH || cap === GL.LINE_SMOOTH) return 0; if (cap === GL.PROGRAM_POINT_SIZE || cap === GL.TEXTURE_CUBE_MAP_SEAMLESS) return 1; const r = g().isEnabled(cap); if (g().getError() === GL.INVALID_ENUM) { this.setErr(GL.INVALID_ENUM); return 0; } return r ? 1 : 0; });
    this.def('glEnablei', (a) => { if (U(a[1]) !== 0) { this.setErr(GL.INVALID_VALUE); this.warn('enablei', 'glEnablei/glDisablei with index > 0: per-draw-buffer blending is not available in WebGL2'); return; } this.enable(U(a[0]), true); }); this.def('glDisablei', (a) => { if (U(a[1]) !== 0) { this.setErr(GL.INVALID_VALUE); return; } this.enable(U(a[0]), false); }); this.def('glIsEnabledi', (a) => U(a[1]) === 0 ? (g().isEnabled(U(a[0])) ? 1 : 0) : 0);
    this.def('glHint', (a) => { const t = U(a[0]); if (t === 0x8192 /* GENERATE_MIPMAP_HINT */ || t === 0x8B8B /* FRAGMENT_SHADER_DERIVATIVE_HINT */) g().hint(t, U(a[1])); });
    this.def('glViewport', (a) => g().viewport(I(a[0]), I(a[1]), I(a[2]), I(a[3]))); this.def('glScissor', (a) => g().scissor(I(a[0]), I(a[1]), I(a[2]), I(a[3])));
    this.def('glClearColor', (a) => g().clearColor(F(a[0]), F(a[1]), F(a[2]), F(a[3]))); this.def('glClearDepth', (a) => g().clearDepth(D(a[0]))); this.def('glClearDepthf', (a) => g().clearDepth(F(a[0]))); this.def('glClearStencil', (a) => g().clearStencil(I(a[0])));
    this.def('glClear', (a) => g().clear(U(a[0]))); this.def('glFlush', () => g().flush()); this.def('glFinish', () => g().finish());
    this.def('glColorMask', (a) => g().colorMask(B(a[0]), B(a[1]), B(a[2]), B(a[3]))); this.def('glColorMaski', (a) => { if (U(a[0]) !== 0) { this.setErr(GL.INVALID_VALUE); return; } g().colorMask(B(a[1]), B(a[2]), B(a[3]), B(a[4])); });
    this.def('glDepthMask', (a) => g().depthMask(B(a[0]))); this.def('glDepthFunc', (a) => g().depthFunc(U(a[0]))); this.def('glDepthRange', (a) => g().depthRange(D(a[0]), D(a[1]))); this.def('glDepthRangef', (a) => g().depthRange(F(a[0]), F(a[1])));
    this.def('glStencilMask', (a) => g().stencilMask(U(a[0]))); this.def('glStencilMaskSeparate', (a) => g().stencilMaskSeparate(U(a[0]), U(a[1]))); this.def('glStencilFunc', (a) => g().stencilFunc(U(a[0]), I(a[1]), U(a[2]))); this.def('glStencilFuncSeparate', (a) => g().stencilFuncSeparate(U(a[0]), U(a[1]), I(a[2]), U(a[3]))); this.def('glStencilOp', (a) => g().stencilOp(U(a[0]), U(a[1]), U(a[2]))); this.def('glStencilOpSeparate', (a) => g().stencilOpSeparate(U(a[0]), U(a[1]), U(a[2]), U(a[3])));
    this.def('glBlendFunc', (a) => g().blendFunc(U(a[0]), U(a[1]))); this.def('glBlendFuncSeparate', (a) => g().blendFuncSeparate(U(a[0]), U(a[1]), U(a[2]), U(a[3]))); this.def('glBlendEquation', (a) => g().blendEquation(U(a[0]))); this.def('glBlendEquationSeparate', (a) => g().blendEquationSeparate(U(a[0]), U(a[1]))); this.def('glBlendColor', (a) => g().blendColor(F(a[0]), F(a[1]), F(a[2]), F(a[3])));
    this.def('glBlendFunci', (a) => { if (U(a[0]) === 0) g().blendFunc(U(a[1]), U(a[2])); else this.setErr(GL.INVALID_VALUE); }); this.def('glBlendFuncSeparatei', (a) => { if (U(a[0]) === 0) g().blendFuncSeparate(U(a[1]), U(a[2]), U(a[3]), U(a[4])); else this.setErr(GL.INVALID_VALUE); }); this.def('glBlendEquationi', (a) => { if (U(a[0]) === 0) g().blendEquation(U(a[1])); else this.setErr(GL.INVALID_VALUE); }); this.def('glBlendEquationSeparatei', (a) => { if (U(a[0]) === 0) g().blendEquationSeparate(U(a[1]), U(a[2])); else this.setErr(GL.INVALID_VALUE); });
    this.alias('glBlendFuncSeparateEXT', 'glBlendFuncSeparate'); this.alias('glBlendEquationEXT', 'glBlendEquation'); this.alias('glBlendEquationSeparateEXT', 'glBlendEquationSeparate');
    this.def('glCullFace', (a) => g().cullFace(U(a[0]))); this.def('glFrontFace', (a) => g().frontFace(U(a[0]))); this.def('glLineWidth', (a) => g().lineWidth(F(a[0]))); this.def('glPolygonOffset', (a) => g().polygonOffset(F(a[0]), F(a[1]))); this.def('glSampleCoverage', (a) => g().sampleCoverage(F(a[0]), B(a[1])));
    this.def('glPointParameterf', () => {}); this.def('glPointParameterfv', () => {}); this.def('glPointParameteri', () => {}); this.def('glPointParameteriv', () => {}); this.alias('glPointParameterfARB', 'glPointParameterf'); this.alias('glPointParameterfvARB', 'glPointParameterfv');
    this.def('glPixelStorei', (a) => this.pixelStore(U(a[0]), I(a[1]))); this.def('glPixelStoref', (a) => this.pixelStore(U(a[0]), F(a[1]) | 0));
    this.def('glReadBuffer', (a) => g().readBuffer(this.bound.fbRead ? U(a[0]) : this.defaultDrawBuffer(U(a[0])))); this.def('glDrawBuffer', (a) => { const m = U(a[0]); this.drawBuffer = m; g().drawBuffers([this.bound.fbDraw ? m : this.defaultDrawBuffer(m)]); });
    this.def('glDrawBuffers', (a) => { const n = I(a[0]); const v = this.i32(a[1], n); if (v) g().drawBuffers(Array.from(v, (x) => this.bound.fbDraw ? x >>> 0 : this.defaultDrawBuffer(x >>> 0))); });
    this.def('glClampColor', () => {}); this.def('glProvokingVertex', () => {}); this.def('glPrimitiveRestartIndex', () => this.warn('restart', 'glPrimitiveRestartIndex: WebGL2 always uses the fixed restart index (0xFFFF/0xFFFFFFFF)')); this.def('glMinSampleShading', () => {}); this.def('glSampleMaski', () => {}); this.def('glBlendBarrier', () => {});
    // -- getters --
    this.def('glGetString', (a) => this.getString(U(a[0]))); this.def('glGetStringi', (a) => { if (U(a[0]) !== GL.EXTENSIONS) { this.setErr(GL.INVALID_ENUM); return 0n; } const i = U(a[1]); if (i >= this.extList.length) { this.setErr(GL.INVALID_VALUE); return 0n; } return this.gstr(this.extList[i]); }); this.alias('glGetStringiEXT', 'glGetStringi');
    this.def('glGetIntegerv', (a) => { const v = this.paramArray(U(a[0])); if (v) v.forEach((x, i) => this.wr32(a[1] + BigInt(i * 4), typeof x === 'number' && !Number.isInteger(x) ? Math.round(x) : x)); });
    this.def('glGetInteger64v', (a) => { const v = this.paramArray(U(a[0])); if (v) v.forEach((x, i) => this.wr64(a[1] + BigInt(i * 8), BigInt(Math.round(x)))); });
    this.def('glGetFloatv', (a) => { const v = this.paramArray(U(a[0])); if (v) v.forEach((x, i) => this.wrf32(a[1] + BigInt(i * 4), x)); });
    this.def('glGetDoublev', (a) => { const v = this.paramArray(U(a[0])); if (v) v.forEach((x, i) => this.wrf64(a[1] + BigInt(i * 8), x)); });
    this.def('glGetBooleanv', (a) => { const v = this.paramArray(U(a[0])); if (v) { const d = this.u8(a[1], v.length, true); if (d) v.forEach((x, i) => { d[i] = x ? 1 : 0; }); } });
    this.def('glGetIntegeri_v', (a) => { const p = U(a[0]), i = U(a[1]); const v = g().getIndexedParameter(p, i); this.wr32(a[2], typeof v === 'number' ? v : v ? (this.bufIds.get(v as WebGLBuffer) ?? 0) : 0); });
    this.def('glGetInteger64i_v', (a) => { const p = U(a[0]), i = U(a[1]); const v = g().getIndexedParameter(p, i); this.wr64(a[2], BigInt(typeof v === 'number' ? v : v ? (this.bufIds.get(v as WebGLBuffer) ?? 0) : 0)); });
    this.def('glGetBooleani_v', (a) => { const v = g().getIndexedParameter(U(a[0]), U(a[1])); const d = this.u8(a[2], 1, true); if (d) d[0] = v ? 1 : 0; });
    this.def('glGetTexParameteriv', (a) => this.getTexParam(U(a[0]), U(a[1])).forEach((v, i) => this.wr32(a[2] + BigInt(i * 4), v))); this.def('glGetTexParameterfv', (a) => this.getTexParam(U(a[0]), U(a[1])).forEach((v, i) => this.wrf32(a[2] + BigInt(i * 4), v))); this.alias('glGetTexParameterIiv', 'glGetTexParameteriv'); this.alias('glGetTexParameterIuiv', 'glGetTexParameteriv');
    this.def('glGetTexLevelParameteriv', (a) => this.wr32(a[3], this.texLevelParam(U(a[0]), I(a[1]), U(a[2])))); this.def('glGetTexLevelParameterfv', (a) => this.wrf32(a[3], this.texLevelParam(U(a[0]), I(a[1]), U(a[2]))));
    this.def('glGetMultisamplefv', (a) => { this.wrf32(a[2], 0.5); this.wrf32(a[2] + 4n, 0.5); });
    this.def('glGetPointerv', (a) => this.wrp(a[1], 0n));
    // -- textures --
    this.def('glGenTextures', (a) => this.gen(I(a[0]), a[1], 'tex', () => { const id = this.next.tex++; this.texInfo.set(id, { target: 0, levels: new Map(), genMipmap: false }); return id; }));
    this.def('glDeleteTextures', (a) => { for (const id of this.ids(I(a[0]), a[1])) { const t = this.tex.get(id); if (t) { g().deleteTexture(t); this.texIds.delete(t); } this.tex.delete(id); this.texInfo.delete(id); for (const u of this.texUnits) for (const [k, v] of u) if (v === id) u.delete(k); } });
    this.def('glIsTexture', (a) => this.tex.has(U(a[0])) ? 1 : 0);
    this.def('glBindTexture', (a) => { const target = this.texTarget(U(a[0])), id = U(a[1]); if (target === GL.TEXTURE_RECTANGLE || target === GL.TEXTURE_BUFFER || target === GL.TEXTURE_2D_MULTISAMPLE) { this.setErr(GL.INVALID_ENUM); this.warn('textarget:' + target, `glBindTexture(0x${target.toString(16)}): rectangle / buffer / multisample textures are not available in WebGL2 → GL_INVALID_ENUM`); return; }
      let t: WebGLTexture | null = null; if (id) { t = this.tex.get(id) ?? null; if (!t) { t = g().createTexture()!; this.tex.set(id, t); this.texIds.set(t, id); if (!this.texInfo.has(id)) this.texInfo.set(id, { target, levels: new Map(), genMipmap: false }); } const ti = this.texInfo.get(id)!; if (!ti.target) ti.target = target; }
      g().bindTexture(target, t); this.texUnits[this.bound.unit].set(target, id); });
    this.def('glActiveTexture', (a) => { const u = U(a[0]) - GL.TEXTURE0; if (u < 0 || u >= 32) { this.setErr(GL.INVALID_ENUM); return; } this.bound.unit = u; g().activeTexture(GL.TEXTURE0 + u); }); this.alias('glActiveTextureARB', 'glActiveTexture'); this.def('glClientActiveTexture', () => {}); this.alias('glClientActiveTextureARB', 'glClientActiveTexture');
    this.def('glTexParameteri', (a) => this.texParam(U(a[0]), U(a[1]), I(a[2]))); this.def('glTexParameterf', (a) => this.texParam(U(a[0]), U(a[1]), F(a[2])));
    this.def('glTexParameteriv', (a) => { const v = this.i32(a[2], 1); if (v) this.texParam(U(a[0]), U(a[1]), v[0]); }); this.def('glTexParameterfv', (a) => { const v = this.f32(a[2], 1); if (v) this.texParam(U(a[0]), U(a[1]), v[0]); }); this.alias('glTexParameterIiv', 'glTexParameteriv'); this.alias('glTexParameterIuiv', 'glTexParameteriv');
    this.def('glTexImage2D', (a) => { const target = U(a[0]), level = I(a[1]), w = I(a[3]), h = I(a[4]); const ft = this.fmtType(U(a[2]), U(a[6]), U(a[7]));
      if (this.bound.unpack) { g().texImage2D(target, level, ft.ifmt, w, h, 0, ft.format, ft.type, Number(a[8])); this.noteLevel(target, level, w, h, 1, ft.ifmt); return; }
      const px = this.pixels(a[8], w, h, 1, ft.format, ft.type); if (!px) return; const tight = px.data !== null && px.format !== ft.format;
      this.withUnpackTight(tight, () => g().texImage2D(target, level, ft.ifmt, w, h, 0, px.format, px.type, px.data as ArrayBufferView | null)); this.noteLevel(target, level, w, h, 1, ft.ifmt); if (level === 0) this.noteUpload(target, 'img', U(a[6]), U(a[7]), px.data); });
    this.def('glTexSubImage2D', (a) => { const target = U(a[0]), level = I(a[1]), x = I(a[2]), y = I(a[3]), w = I(a[4]), h = I(a[5]); const format = U(a[6]); let type = U(a[7]); if (type === 0x8D61) type = GL.HALF_FLOAT;
      if (this.bound.unpack) { g().texSubImage2D(target, level, x, y, w, h, format, type, Number(a[8])); return; }
      const px = this.pixels(a[8], w, h, 1, format, type); if (!px || !px.data) return; this.withUnpackTight(px.format !== format, () => g().texSubImage2D(target, level, x, y, w, h, px.format, px.type, px.data as ArrayBufferView)); if (level === 0) this.noteUpload(target, `sub${x},${y},${w}x${h}`, format, type, px.data); });
    this.def('glTexImage3D', (a) => { const target = U(a[0]), level = I(a[1]), w = I(a[3]), h = I(a[4]), d = I(a[5]); const ft = this.fmtType(U(a[2]), U(a[7]), U(a[8]));
      if (this.bound.unpack) { g().texImage3D(target, level, ft.ifmt, w, h, d, 0, ft.format, ft.type, Number(a[9])); this.noteLevel(target, level, w, h, d, ft.ifmt); return; }
      const px = this.pixels(a[9], w, h, d, ft.format, ft.type); if (!px) return; this.withUnpackTight(px.data !== null && px.format !== ft.format, () => g().texImage3D(target, level, ft.ifmt, w, h, d, 0, px.format, px.type, px.data as ArrayBufferView | null)); this.noteLevel(target, level, w, h, d, ft.ifmt); });
    this.alias('glTexImage3DEXT', 'glTexImage3D');
    this.def('glTexSubImage3D', (a) => { const target = U(a[0]), level = I(a[1]), x = I(a[2]), y = I(a[3]), z = I(a[4]), w = I(a[5]), h = I(a[6]), d = I(a[7]), format = U(a[8]), type = U(a[9]);
      if (this.bound.unpack) { g().texSubImage3D(target, level, x, y, z, w, h, d, format, type, Number(a[10])); return; }
      const px = this.pixels(a[10], w, h, d, format, type); if (!px || !px.data) return; this.withUnpackTight(px.format !== format, () => g().texSubImage3D(target, level, x, y, z, w, h, d, px.format, px.type, px.data as ArrayBufferView)); });
    this.alias('glTexSubImage3DEXT', 'glTexSubImage3D');
    this.def('glTexImage1D', (a) => this.callName('glTexImage2D', [BigInt(GL.TEXTURE_2D), a[1], a[2], a[3], 1n, a[4], a[5], a[6], a[7]])); this.def('glTexSubImage1D', (a) => this.callName('glTexSubImage2D', [BigInt(GL.TEXTURE_2D), a[1], a[2], 0n, a[3], 1n, a[4], a[5], a[6]]));
    this.def('glCopyTexImage1D', (a) => this.callName('glCopyTexImage2D', [BigInt(GL.TEXTURE_2D), a[1], a[2], a[3], a[4], a[5], 1n, a[6]])); this.def('glCopyTexSubImage1D', (a) => this.callName('glCopyTexSubImage2D', [BigInt(GL.TEXTURE_2D), a[1], a[2], 0n, a[3], a[4], a[5], 1n]));
    this.def('glTexStorage2D', (a) => { const target = U(a[0]), levels = I(a[1]), ifmt = this.fmtType(U(a[2]), GL.RGBA, GL.UNSIGNED_BYTE).ifmt, w = I(a[3]), h = I(a[4]); g().texStorage2D(target, levels, ifmt, w, h); for (let l = 0; l < levels; l++) this.noteLevel(target, l, Math.max(1, w >> l), Math.max(1, h >> l), 1, ifmt); }); this.alias('glTexStorage2DEXT', 'glTexStorage2D');
    this.def('glTexStorage3D', (a) => { const target = U(a[0]), levels = I(a[1]), ifmt = this.fmtType(U(a[2]), GL.RGBA, GL.UNSIGNED_BYTE).ifmt, w = I(a[3]), h = I(a[4]), d = I(a[5]); g().texStorage3D(target, levels, ifmt, w, h, d); for (let l = 0; l < levels; l++) this.noteLevel(target, l, Math.max(1, w >> l), Math.max(1, h >> l), d, ifmt); });
    this.def('glCopyTexImage2D', (a) => { const ft = this.fmtType(U(a[2]), GL.RGBA, GL.UNSIGNED_BYTE); g().copyTexImage2D(U(a[0]), I(a[1]), ft.ifmt, I(a[3]), I(a[4]), I(a[5]), I(a[6]), 0); this.noteLevel(U(a[0]), I(a[1]), I(a[5]), I(a[6]), 1, ft.ifmt); });
    this.def('glCopyTexSubImage2D', (a) => g().copyTexSubImage2D(U(a[0]), I(a[1]), I(a[2]), I(a[3]), I(a[4]), I(a[5]), I(a[6]), I(a[7]))); this.def('glCopyTexSubImage3D', (a) => g().copyTexSubImage3D(U(a[0]), I(a[1]), I(a[2]), I(a[3]), I(a[4]), I(a[5]), I(a[6]), I(a[7]), I(a[8])));
    this.def('glCompressedTexImage2D', (a) => { const target = U(a[0]), level = I(a[1]), ifmt = U(a[2]), w = I(a[3]), h = I(a[4]), size = I(a[6]); if (!this.compressedOk(ifmt)) return; if (this.bound.unpack) { g().compressedTexImage2D(target, level, ifmt, w, h, 0, size, Number(a[7])); } else { const d = this.u8(a[7], size); if (!d) return; g().compressedTexImage2D(target, level, ifmt, w, h, 0, d); if (level === 0) this.noteUpload(target, 'dxt', ifmt, 0, d); } this.noteLevel(target, level, w, h, 1, ifmt); }); this.alias('glCompressedTexImage2DARB', 'glCompressedTexImage2D');
    this.def('glCompressedTexSubImage2D', (a) => { const target = U(a[0]), level = I(a[1]), x = I(a[2]), y = I(a[3]), w = I(a[4]), h = I(a[5]), fmt = U(a[6]), size = I(a[7]); if (!this.compressedOk(fmt)) return; if (this.bound.unpack) g().compressedTexSubImage2D(target, level, x, y, w, h, fmt, size, Number(a[8])); else { const d = this.u8(a[8], size); if (d) g().compressedTexSubImage2D(target, level, x, y, w, h, fmt, d); } }); this.alias('glCompressedTexSubImage2DARB', 'glCompressedTexSubImage2D');
    this.def('glCompressedTexImage3D', (a) => { const ifmt = U(a[2]), size = I(a[7]); if (!this.compressedOk(ifmt)) return; if (this.bound.unpack) g().compressedTexImage3D(U(a[0]), I(a[1]), ifmt, I(a[3]), I(a[4]), I(a[5]), 0, size, Number(a[8])); else { const d = this.u8(a[8], size); if (d) g().compressedTexImage3D(U(a[0]), I(a[1]), ifmt, I(a[3]), I(a[4]), I(a[5]), 0, d); } this.noteLevel(U(a[0]), I(a[1]), I(a[3]), I(a[4]), I(a[5]), ifmt); });
    this.def('glCompressedTexSubImage3D', (a) => { const fmt = U(a[8]), size = I(a[9]); if (!this.compressedOk(fmt)) return; if (this.bound.unpack) g().compressedTexSubImage3D(U(a[0]), I(a[1]), I(a[2]), I(a[3]), I(a[4]), I(a[5]), I(a[6]), I(a[7]), fmt, size, Number(a[10])); else { const d = this.u8(a[10], size); if (d) g().compressedTexSubImage3D(U(a[0]), I(a[1]), I(a[2]), I(a[3]), I(a[4]), I(a[5]), I(a[6]), I(a[7]), fmt, d); } });
    this.def('glCompressedTexImage1D', () => this.setErr(GL.INVALID_ENUM)); this.def('glCompressedTexSubImage1D', () => this.setErr(GL.INVALID_ENUM)); this.def('glGetCompressedTexImage', () => { this.setErr(GL.INVALID_OPERATION); this.warn('getcomp', 'glGetCompressedTexImage: texture readback of compressed data is not available in WebGL2'); });
    this.def('glGenerateMipmap', (a) => g().generateMipmap(this.texTarget(U(a[0])))); this.alias('glGenerateMipmapEXT', 'glGenerateMipmap');
    this.def('glGetTexImage', (a) => this.getTexImage(U(a[0]), I(a[1]), U(a[2]), U(a[3]), a[4]));
    this.def('glTexBuffer', () => { this.setErr(GL.INVALID_ENUM); this.warn('texbuffer', 'glTexBuffer: buffer textures are not available in WebGL2'); });
    this.def('glTexImage2DMultisample', () => { this.setErr(GL.INVALID_OPERATION); this.warn('texms', 'glTexImage2DMultisample: multisampled textures are not available in WebGL2 (use multisampled renderbuffers + glBlitFramebuffer)'); }); this.def('glTexImage3DMultisample', () => this.setErr(GL.INVALID_OPERATION));
    // -- buffers --
    this.def('glGenBuffers', (a) => this.gen(I(a[0]), a[1], 'buf', () => this.next.buf++)); this.alias('glGenBuffersARB', 'glGenBuffers');
    this.def('glDeleteBuffers', (a) => { for (const id of this.ids(I(a[0]), a[1])) { const b = this.buf.get(id); if (b) { g().deleteBuffer(b); this.bufIds.delete(b); } const tw = this.bufInfo.get(id)?.twin; if (tw) g().deleteBuffer(tw); this.buf.delete(id); this.bufInfo.delete(id); this.unmapIfMapped(id); for (const k of Object.keys(this.bound) as (keyof typeof this.bound)[]) if ((k === 'array' || k === 'element' || k === 'pack' || k === 'unpack' || k === 'uniform' || k === 'copyRead' || k === 'copyWrite' || k === 'tfb') && this.bound[k] === id) this.bound[k] = 0; } }); this.alias('glDeleteBuffersARB', 'glDeleteBuffers');
    this.def('glIsBuffer', (a) => this.buf.has(U(a[0])) ? 1 : 0);
    this.def('glBindBuffer', (a) => this.bindBuffer(U(a[0]), U(a[1]))); this.alias('glBindBufferARB', 'glBindBuffer');
    this.def('glBufferData', (a) => { const target = U(a[0]), size = Q(a[1]), usage = U(a[3]); const id = this.bufTargetId(target); if (!id) { this.setErr(GL.INVALID_OPERATION); return; } const info = this.bufInfo.get(id)!; info.size = size; info.usage = usage; info.version++; info.shadow = null; this.unmapIfMapped(id);
      if (!a[2]) g().bufferData(target, size, usage); else { const d = this.u8(a[2], size); if (d) g().bufferData(target, d, usage); } this.touched(target); }); this.alias('glBufferDataARB', 'glBufferData');
    this.def('glBufferSubData', (a) => { const target = U(a[0]), off = Q(a[1]), size = Q(a[2]); const id = this.bufTargetId(target); if (!id) { this.setErr(GL.INVALID_OPERATION); return; } const info = this.bufInfo.get(id); if (info) { info.version++; info.shadow = null; } const d = this.u8(a[3], size); if (d) { g().bufferSubData(target, off, d); this.touched(target); } }); this.alias('glBufferSubDataARB', 'glBufferSubData');
    this.def('glGetBufferSubData', (a) => { const d = this.u8(a[3], Q(a[2]), true); if (d) g().getBufferSubData(U(a[0]), Q(a[1]), d); });
    this.def('glCopyBufferSubData', (a) => { const rt = U(a[0]), wt = U(a[1]), n = Q(a[4]); if (this.kindOf(rt) !== this.kindOf(wt)) { const tmp = new Uint8Array(n); g().getBufferSubData(rt, Q(a[2]), tmp); g().bufferSubData(wt, Q(a[3]), tmp); } else g().copyBufferSubData(rt, wt, Q(a[2]), Q(a[3]), n); const info = this.bufInfo.get(this.bufTargetId(wt)); if (info) { info.version++; info.shadow = null; } this.touched(wt); });
    this.def('glGetBufferParameteriv', (a) => { const target = U(a[0]), p = U(a[1]); const id = this.bufTargetId(target); const info = this.bufInfo.get(id); if (p === GL.BUFFER_MAPPED) { this.wr32(a[2], this.maps.some((m) => m.buffer === id) ? 1 : 0); return; } if (p === GL.BUFFER_ACCESS) { const m = this.maps.find((m) => m.buffer === id); this.wr32(a[2], m ? (m.access & 1 && m.access & 2 ? GL.READ_WRITE : m.access & 1 ? GL.READ_ONLY : GL.WRITE_ONLY) : GL.READ_WRITE); return; } if (p === GL.BUFFER_SIZE && info) { this.wr32(a[2], info.size); return; } this.wr32(a[2], (g().getBufferParameter(target, p) as number) | 0); });
    this.def('glGetBufferParameteri64v', (a) => { const info = this.bufInfo.get(this.bufTargetId(U(a[0]))); this.wr64(a[2], BigInt(U(a[1]) === GL.BUFFER_SIZE && info ? info.size : (g().getBufferParameter(U(a[0]), U(a[1])) as number) | 0)); });
    this.def('glGetBufferPointerv', (a) => { const m = this.maps.find((m) => m.buffer === this.bufTargetId(U(a[0]))); this.wrp(a[2], m ? m.ptr : 0n); });
    this.def('glMapBuffer', (a) => this.mapBuffer(U(a[0]), 0, -1, U(a[1]) === GL.READ_ONLY ? 1 : U(a[1]) === GL.WRITE_ONLY ? 2 : 3)); this.alias('glMapBufferARB', 'glMapBuffer');
    this.def('glMapBufferRange', (a) => this.mapBuffer(U(a[0]), Q(a[1]), Q(a[2]), U(a[3]))); this.alias('glMapBufferRangeEXT', 'glMapBufferRange');
    this.def('glUnmapBuffer', (a) => this.unmapBuffer(U(a[0]))); this.alias('glUnmapBufferARB', 'glUnmapBuffer');
    this.def('glFlushMappedBufferRange', (a) => { const target = U(a[0]), off = Q(a[1]), len = Q(a[2]); const id = this.bufTargetId(target); const m = this.maps.find((m) => m.buffer === id); if (!m) { this.setErr(GL.INVALID_OPERATION); return; } const d = this.u8(m.ptr + BigInt(off), len); if (d) { g().bufferSubData(target, m.offset + off, d); this.touched(target); } }); this.alias('glFlushMappedBufferRangeEXT', 'glFlushMappedBufferRange');
    this.def('glBindBufferBase', (a) => { const target = U(a[0]), id = U(a[2]); const b = id ? this.objFor(id, target) : null; g().bindBufferBase(target, U(a[1]), b); this.setBound(target, id); });
    this.def('glBindBufferRange', (a) => { const target = U(a[0]), id = U(a[2]); const b = id ? this.objFor(id, target) : null; g().bindBufferRange(target, U(a[1]), b, Q(a[3]), Q(a[4])); this.setBound(target, id); });
    // -- vertex arrays / attributes --
    this.def('glGenVertexArrays', (a) => this.gen(I(a[0]), a[1], 'vao', () => this.next.vao++)); this.alias('glGenVertexArraysAPPLE', 'glGenVertexArrays');
    this.def('glDeleteVertexArrays', (a) => { for (const id of this.ids(I(a[0]), a[1])) { const v = this.vao.get(id); if (v) { g().deleteVertexArray(v); this.vaoIds.delete(v); } this.vao.delete(id); this.attribs.delete(id); if (this.bound.vao === id) { this.bound.vao = 0; this.bound.element = 0; } } }); this.alias('glDeleteVertexArraysAPPLE', 'glDeleteVertexArrays');
    this.def('glIsVertexArray', (a) => this.vao.has(U(a[0])) ? 1 : 0);
    this.def('glBindVertexArray', (a) => { const id = U(a[0]); let v: WebGLVertexArrayObject | null = null; if (id) { v = this.vao.get(id) ?? null; if (!v) { v = g().createVertexArray()!; this.vao.set(id, v); this.vaoIds.set(v, id); } } g().bindVertexArray(v); this.bound.vao = id; const eb = g().getParameter(g().ELEMENT_ARRAY_BUFFER_BINDING) as WebGLBuffer | null; this.bound.element = eb ? this.bufIds.get(eb) ?? 0 : 0; }); this.alias('glBindVertexArrayAPPLE', 'glBindVertexArray');
    this.def('glEnableVertexAttribArray', (a) => { const i = U(a[0]); if (i >= this.maxAttribs) { this.setErr(GL.INVALID_VALUE); return; } this.curAttribs()[i].enabled = true; g().enableVertexAttribArray(i); }); this.alias('glEnableVertexAttribArrayARB', 'glEnableVertexAttribArray');
    this.def('glDisableVertexAttribArray', (a) => { const i = U(a[0]); if (i >= this.maxAttribs) { this.setErr(GL.INVALID_VALUE); return; } this.curAttribs()[i].enabled = false; g().disableVertexAttribArray(i); }); this.alias('glDisableVertexAttribArrayARB', 'glDisableVertexAttribArray');
    this.def('glVertexAttribPointer', (a) => { const i = U(a[0]); if (i >= this.maxAttribs) { this.setErr(GL.INVALID_VALUE); return; } const type = U(a[2]); if (type === GL.DOUBLE) { this.warn('attribdouble', 'glVertexAttribPointer(GL_DOUBLE): double vertex data is not supported by WebGL2 → GL_INVALID_ENUM'); this.setErr(GL.INVALID_ENUM); return; } const s = this.curAttribs()[i]; s.size = I(a[1]) === 0x80E1 ? 4 : I(a[1]); s.type = type; s.normalized = B(a[3]); s.integer = false; s.stride = I(a[4]); s.pointer = a[5]; s.buffer = this.bound.array; if (this.bound.array) g().vertexAttribPointer(i, s.size, type, s.normalized, s.stride, Number(a[5])); }); this.alias('glVertexAttribPointerARB', 'glVertexAttribPointer');
    this.def('glVertexAttribIPointer', (a) => { const i = U(a[0]); if (i >= this.maxAttribs) { this.setErr(GL.INVALID_VALUE); return; } const s = this.curAttribs()[i]; s.size = I(a[1]); s.type = U(a[2]); s.normalized = false; s.integer = true; s.stride = I(a[3]); s.pointer = a[4]; s.buffer = this.bound.array; if (this.bound.array) g().vertexAttribIPointer(i, s.size, s.type, s.stride, Number(a[4])); });
    this.def('glVertexAttribDivisor', (a) => { const i = U(a[0]); if (i < this.maxAttribs) { this.curAttribs()[i].divisor = U(a[1]); g().vertexAttribDivisor(i, U(a[1])); } }); this.alias('glVertexAttribDivisorARB', 'glVertexAttribDivisor');
    this.def('glGetVertexAttribiv', (a) => this.wr32(a[2], this.vertexAttribParam(U(a[0]), U(a[1])))); this.def('glGetVertexAttribfv', (a) => { const p = U(a[1]); if (p === GL.CURRENT_VERTEX_ATTRIB) { const v = g().getVertexAttrib(U(a[0]), p) as Float32Array; for (let i = 0; i < 4; i++) this.wrf32(a[2] + BigInt(i * 4), v[i]); } else this.wrf32(a[2], this.vertexAttribParam(U(a[0]), p)); }); this.def('glGetVertexAttribdv', (a) => { const p = U(a[1]); if (p === GL.CURRENT_VERTEX_ATTRIB) { const v = g().getVertexAttrib(U(a[0]), p) as Float32Array; for (let i = 0; i < 4; i++) this.wrf64(a[2] + BigInt(i * 8), v[i]); } else this.wrf64(a[2], this.vertexAttribParam(U(a[0]), p)); }); this.alias('glGetVertexAttribIiv', 'glGetVertexAttribiv'); this.alias('glGetVertexAttribIuiv', 'glGetVertexAttribiv');
    this.def('glGetVertexAttribPointerv', (a) => { const i = U(a[0]); this.wrp(a[2], i < this.maxAttribs ? this.curAttribs()[i].pointer : 0n); });
    const va = (name: string, f: (gl: WebGL2RenderingContext, i: number, a: BigUint64Array) => void) => this.def(name, (a) => { const i = U(a[0]); if (i >= this.maxAttribs) { this.setErr(GL.INVALID_VALUE); return; } f(g(), i, a); });
    va('glVertexAttrib1f', (gl, i, a) => gl.vertexAttrib1f(i, F(a[1]))); va('glVertexAttrib2f', (gl, i, a) => gl.vertexAttrib2f(i, F(a[1]), F(a[2]))); va('glVertexAttrib3f', (gl, i, a) => gl.vertexAttrib3f(i, F(a[1]), F(a[2]), F(a[3]))); va('glVertexAttrib4f', (gl, i, a) => gl.vertexAttrib4f(i, F(a[1]), F(a[2]), F(a[3]), F(a[4])));
    va('glVertexAttrib1d', (gl, i, a) => gl.vertexAttrib1f(i, D(a[1]))); va('glVertexAttrib2d', (gl, i, a) => gl.vertexAttrib2f(i, D(a[1]), D(a[2]))); va('glVertexAttrib3d', (gl, i, a) => gl.vertexAttrib3f(i, D(a[1]), D(a[2]), D(a[3]))); va('glVertexAttrib4d', (gl, i, a) => gl.vertexAttrib4f(i, D(a[1]), D(a[2]), D(a[3]), D(a[4])));
    va('glVertexAttrib1s', (gl, i, a) => gl.vertexAttrib1f(i, I(a[1]))); va('glVertexAttrib2s', (gl, i, a) => gl.vertexAttrib2f(i, I(a[1]), I(a[2]))); va('glVertexAttrib3s', (gl, i, a) => gl.vertexAttrib3f(i, I(a[1]), I(a[2]), I(a[3]))); va('glVertexAttrib4s', (gl, i, a) => gl.vertexAttrib4f(i, I(a[1]), I(a[2]), I(a[3]), I(a[4])));
    va('glVertexAttrib4Nub', (gl, i, a) => gl.vertexAttrib4f(i, U(a[1]) / 255, U(a[2]) / 255, U(a[3]) / 255, U(a[4]) / 255));
    va('glVertexAttrib1fv', (gl, i, a) => { const v = this.f32(a[1], 1); if (v) gl.vertexAttrib1fv(i, v); }); va('glVertexAttrib2fv', (gl, i, a) => { const v = this.f32(a[1], 2); if (v) gl.vertexAttrib2fv(i, v); }); va('glVertexAttrib3fv', (gl, i, a) => { const v = this.f32(a[1], 3); if (v) gl.vertexAttrib3fv(i, v); }); va('glVertexAttrib4fv', (gl, i, a) => { const v = this.f32(a[1], 4); if (v) gl.vertexAttrib4fv(i, v); });
    for (const n of [1, 2, 3, 4]) { va(`glVertexAttrib${n}dv`, (gl, i, a) => { const o = this.mem!.ptr(a[1], n * 8, false); if (!o) return; const v = new Float64Array(this.mem!.buffer().slice(o, o + n * 8)); gl.vertexAttrib4f(i, v[0], n > 1 ? v[1] : 0, n > 2 ? v[2] : 0, n > 3 ? v[3] : 1); }); va(`glVertexAttrib${n}sv`, (gl, i, a) => { const o = this.mem!.ptr(a[1], n * 2, false); if (!o) return; const v = new Int16Array(this.mem!.buffer().slice(o, o + n * 2)); gl.vertexAttrib4f(i, v[0], n > 1 ? v[1] : 0, n > 2 ? v[2] : 0, n > 3 ? v[3] : 1); }); }
    va('glVertexAttrib4Nubv', (gl, i, a) => { const v = this.u8(a[1], 4); if (v) gl.vertexAttrib4f(i, v[0] / 255, v[1] / 255, v[2] / 255, v[3] / 255); }); va('glVertexAttrib4ubv', (gl, i, a) => { const v = this.u8(a[1], 4); if (v) gl.vertexAttrib4f(i, v[0], v[1], v[2], v[3]); });
    va('glVertexAttrib4bv', (gl, i, a) => { const v = this.u8(a[1], 4); if (v) gl.vertexAttrib4f(i, (v[0] << 24) >> 24, (v[1] << 24) >> 24, (v[2] << 24) >> 24, (v[3] << 24) >> 24); }); va('glVertexAttrib4Nbv', (gl, i, a) => { const v = this.u8(a[1], 4); if (v) gl.vertexAttrib4f(i, Math.max(-1, ((v[0] << 24) >> 24) / 127), Math.max(-1, ((v[1] << 24) >> 24) / 127), Math.max(-1, ((v[2] << 24) >> 24) / 127), Math.max(-1, ((v[3] << 24) >> 24) / 127)); });
    va('glVertexAttrib4iv', (gl, i, a) => { const v = this.i32(a[1], 4); if (v) gl.vertexAttrib4f(i, v[0], v[1], v[2], v[3]); }); va('glVertexAttrib4uiv', (gl, i, a) => { const v = this.i32(a[1], 4); if (v) gl.vertexAttrib4f(i, v[0] >>> 0, v[1] >>> 0, v[2] >>> 0, v[3] >>> 0); }); va('glVertexAttrib4Niv', (gl, i, a) => { const v = this.i32(a[1], 4); if (v) gl.vertexAttrib4f(i, v[0] / 2147483647, v[1] / 2147483647, v[2] / 2147483647, v[3] / 2147483647); }); va('glVertexAttrib4Nuiv', (gl, i, a) => { const v = this.i32(a[1], 4); if (v) gl.vertexAttrib4f(i, (v[0] >>> 0) / 4294967295, (v[1] >>> 0) / 4294967295, (v[2] >>> 0) / 4294967295, (v[3] >>> 0) / 4294967295); });
    va('glVertexAttrib4Nsv', (gl, i, a) => { const o = this.mem!.ptr(a[1], 8, false); if (!o) return; const v = new Int16Array(this.mem!.buffer().slice(o, o + 8)); gl.vertexAttrib4f(i, Math.max(-1, v[0] / 32767), Math.max(-1, v[1] / 32767), Math.max(-1, v[2] / 32767), Math.max(-1, v[3] / 32767)); }); va('glVertexAttrib4usv', (gl, i, a) => { const o = this.mem!.ptr(a[1], 8, false); if (!o) return; const v = new Uint16Array(this.mem!.buffer().slice(o, o + 8)); gl.vertexAttrib4f(i, v[0], v[1], v[2], v[3]); }); va('glVertexAttrib4Nusv', (gl, i, a) => { const o = this.mem!.ptr(a[1], 8, false); if (!o) return; const v = new Uint16Array(this.mem!.buffer().slice(o, o + 8)); gl.vertexAttrib4f(i, v[0] / 65535, v[1] / 65535, v[2] / 65535, v[3] / 65535); });
    va('glVertexAttribI1i', (gl, i, a) => gl.vertexAttribI4i(i, I(a[1]), 0, 0, 1)); va('glVertexAttribI2i', (gl, i, a) => gl.vertexAttribI4i(i, I(a[1]), I(a[2]), 0, 1)); va('glVertexAttribI3i', (gl, i, a) => gl.vertexAttribI4i(i, I(a[1]), I(a[2]), I(a[3]), 1)); va('glVertexAttribI4i', (gl, i, a) => gl.vertexAttribI4i(i, I(a[1]), I(a[2]), I(a[3]), I(a[4])));
    va('glVertexAttribI1ui', (gl, i, a) => gl.vertexAttribI4ui(i, U(a[1]), 0, 0, 1)); va('glVertexAttribI2ui', (gl, i, a) => gl.vertexAttribI4ui(i, U(a[1]), U(a[2]), 0, 1)); va('glVertexAttribI3ui', (gl, i, a) => gl.vertexAttribI4ui(i, U(a[1]), U(a[2]), U(a[3]), 1)); va('glVertexAttribI4ui', (gl, i, a) => gl.vertexAttribI4ui(i, U(a[1]), U(a[2]), U(a[3]), U(a[4])));
    for (const n of [1, 2, 3, 4]) { va(`glVertexAttribI${n}iv`, (gl, i, a) => { const v = this.i32(a[1], n); if (v) gl.vertexAttribI4i(i, v[0], n > 1 ? v[1] : 0, n > 2 ? v[2] : 0, n > 3 ? v[3] : 1); }); va(`glVertexAttribI${n}uiv`, (gl, i, a) => { const v = this.i32(a[1], n); if (v) gl.vertexAttribI4ui(i, v[0] >>> 0, n > 1 ? v[1] >>> 0 : 0, n > 2 ? v[2] >>> 0 : 0, n > 3 ? v[3] >>> 0 : 1); }); }
    va('glVertexAttribI4bv', (gl, i, a) => { const v = this.u8(a[1], 4); if (v) gl.vertexAttribI4i(i, (v[0] << 24) >> 24, (v[1] << 24) >> 24, (v[2] << 24) >> 24, (v[3] << 24) >> 24); }); va('glVertexAttribI4ubv', (gl, i, a) => { const v = this.u8(a[1], 4); if (v) gl.vertexAttribI4ui(i, v[0], v[1], v[2], v[3]); });
    va('glVertexAttribI4sv', (gl, i, a) => { const o = this.mem!.ptr(a[1], 8, false); if (!o) return; const v = new Int16Array(this.mem!.buffer().slice(o, o + 8)); gl.vertexAttribI4i(i, v[0], v[1], v[2], v[3]); }); va('glVertexAttribI4usv', (gl, i, a) => { const o = this.mem!.ptr(a[1], 8, false); if (!o) return; const v = new Uint16Array(this.mem!.buffer().slice(o, o + 8)); gl.vertexAttribI4ui(i, v[0], v[1], v[2], v[3]); });
    for (const n of [1, 2, 3, 4]) { this.def(`glVertexAttribP${n}ui`, () => { this.setErr(GL.INVALID_OPERATION); this.warn('attribP', 'glVertexAttribP*: packed 2_10_10_10 generic attribute values are not available in WebGL2'); }); this.alias(`glVertexAttribP${n}uiv`, `glVertexAttribP${n}ui`); }
    // -- draws --
    this.def('glDrawArrays', (a) => this.drawArrays(U(a[0]), I(a[1]), I(a[2]), 1));
    this.def('glDrawArraysInstanced', (a) => this.drawArrays(U(a[0]), I(a[1]), I(a[2]), I(a[3]))); this.alias('glDrawArraysInstancedARB', 'glDrawArraysInstanced');
    this.def('glDrawElements', (a) => this.drawElements(U(a[0]), I(a[1]), U(a[2]), a[3], 1, 0));
    this.def('glDrawElementsInstanced', (a) => this.drawElements(U(a[0]), I(a[1]), U(a[2]), a[3], I(a[4]), 0)); this.alias('glDrawElementsInstancedARB', 'glDrawElementsInstanced');
    this.def('glDrawRangeElements', (a) => this.drawElements(U(a[0]), I(a[3]), U(a[4]), a[5], 1, 0)); this.alias('glDrawRangeElementsEXT', 'glDrawRangeElements');
    this.def('glDrawElementsBaseVertex', (a) => this.drawElements(U(a[0]), I(a[1]), U(a[2]), a[3], 1, I(a[4]))); this.def('glDrawRangeElementsBaseVertex', (a) => this.drawElements(U(a[0]), I(a[3]), U(a[4]), a[5], 1, I(a[6]))); this.def('glDrawElementsInstancedBaseVertex', (a) => this.drawElements(U(a[0]), I(a[1]), U(a[2]), a[3], I(a[4]), I(a[5])));
    this.def('glMultiDrawArrays', (a) => { const n = I(a[3]); const first = this.i32(a[1], n), count = this.i32(a[2], n); if (first && count) for (let i = 0; i < n; i++) this.drawArrays(U(a[0]), first[i], count[i], 1); });
    this.def('glMultiDrawElements', (a) => { const n = I(a[4]); const count = this.i32(a[1], n); if (!count) return; for (let i = 0; i < n; i++) this.drawElements(U(a[0]), count[i], U(a[2]), this.rdp(a[3] + BigInt(i * this.mem!.psize())), 1, 0); });
    this.def('glArrayElement', () => this.legacy('glArrayElement'));
    // -- shaders / programs --
    this.def('glCreateShader', (a) => { const type = U(a[0]); if (type !== GL.VERTEX_SHADER && type !== GL.FRAGMENT_SHADER) { this.setErr(GL.INVALID_ENUM); this.warn('shadertype:' + type, `glCreateShader(0x${type.toString(16)}): only vertex and fragment stages exist in WebGL2 (no geometry/tessellation/compute) → 0`); return 0; } const s = g().createShader(type)!; const id = this.next.shader++; this.shader.set(id, s); this.shaderIds.set(s, id); this.shaderType.set(id, type); return id; }); this.alias('glCreateShaderObjectARB', 'glCreateShader');
    this.def('glDeleteShader', (a) => { const id = U(a[0]); const s = this.shader.get(id); if (s) { g().deleteShader(s); this.shaderIds.delete(s); } this.shader.delete(id); this.shaderSrc.delete(id); });
    this.def('glIsShader', (a) => this.shader.has(U(a[0])) ? 1 : 0);
    this.def('glShaderSource', (a) => { const id = U(a[0]), n = I(a[1]); const s = this.shader.get(id); if (!s) { this.setErr(GL.INVALID_VALUE); return; } let src = ''; for (let i = 0; i < n; i++) { const p = this.rdp(a[2] + BigInt(i * this.mem!.psize())); let len = -1; if (a[3]) { const l = this.i32(a[3] + BigInt(i * 4), 1); if (l && l[0] >= 0) len = l[0]; } src += this.rdstr(p, len); } this.shaderSrc.set(id, src); g().shaderSource(s, this.translate(src, this.shaderType.get(id)!)); }); this.alias('glShaderSourceARB', 'glShaderSource');
    this.def('glCompileShader', (a) => { const id = U(a[0]); const s = this.shader.get(id); if (!s) { this.setErr(GL.INVALID_VALUE); return; } g().compileShader(s); if (!g().getShaderParameter(s, GL.COMPILE_STATUS)) { const log = g().getShaderInfoLog(s) || ''; this.badShaders++; this.warn('compile:' + log.slice(0, 80), `shader ${id} failed to compile: ${log.trim().slice(0, 400)}\n--- translated source ---\n${g().getShaderSource(s)?.slice(0, 1200)}`); } }); this.alias('glCompileShaderARB', 'glCompileShader');
    this.def('glGetShaderiv', (a) => { const id = U(a[0]), p = U(a[1]); const s = this.shader.get(id); if (!s) { this.setErr(GL.INVALID_VALUE); return; } if (p === GL.INFO_LOG_LENGTH) { const l = g().getShaderInfoLog(s) || ''; this.wr32(a[2], l.length ? l.length + 1 : 0); return; } if (p === GL.SHADER_SOURCE_LENGTH) { const src = this.shaderSrc.get(id) || ''; this.wr32(a[2], src.length ? src.length + 1 : 0); return; } if (p === GL.SHADER_TYPE) { this.wr32(a[2], this.shaderType.get(id)!); return; } const v = g().getShaderParameter(s, p); this.wr32(a[2], typeof v === 'boolean' ? (v ? 1 : 0) : (v as number) | 0); });
    this.def('glGetShaderInfoLog', (a) => { const s = this.shader.get(U(a[0])); this.wrstr(a[3], I(a[1]), s ? g().getShaderInfoLog(s) || '' : '', a[2]); });
    this.def('glGetShaderSource', (a) => this.wrstr(a[3], I(a[1]), this.shaderSrc.get(U(a[0])) || '', a[2]));
    this.def('glReleaseShaderCompiler', () => {}); this.def('glShaderBinary', () => this.setErr(GL.INVALID_ENUM)); this.def('glGetShaderPrecisionFormat', (a) => { const f = g().getShaderPrecisionFormat(U(a[0]), U(a[1])); if (f) { this.wr32(a[2], f.rangeMin); this.wr32(a[2] + 4n, f.rangeMax); this.wr32(a[3], f.precision); } });
    this.def('glCreateProgram', () => { const p = g().createProgram()!; const id = this.next.prog++; this.prog.set(id, p); this.progIds.set(p, id); this.progInfoOf(id); return id; }); this.alias('glCreateProgramObjectARB', 'glCreateProgram');
    this.def('glDeleteProgram', (a) => { const id = U(a[0]); const p = this.prog.get(id); if (p) { g().deleteProgram(p); this.progIds.delete(p); } this.prog.delete(id); this.progInfo.delete(id); });
    this.def('glDeleteObjectARB', (a) => { const id = U(a[0]); if (this.prog.has(id)) this.impl[this.byName.get('glDeleteProgram')!]!(a); else this.impl[this.byName.get('glDeleteShader')!]!(a); });
    this.def('glIsProgram', (a) => this.prog.has(U(a[0])) ? 1 : 0);
    this.def('glAttachShader', (a) => { const p = this.prog.get(U(a[0])), s = this.shader.get(U(a[1])); if (!p || !s) { this.setErr(GL.INVALID_VALUE); return; } g().attachShader(p, s); }); this.alias('glAttachObjectARB', 'glAttachShader');
    this.def('glDetachShader', (a) => { const p = this.prog.get(U(a[0])), s = this.shader.get(U(a[1])); if (p && s) g().detachShader(p, s); });
    this.def('glLinkProgram', (a) => { const id = U(a[0]); const p = this.prog.get(id); if (!p) { this.setErr(GL.INVALID_VALUE); return; } g().linkProgram(p); this.linkCount++; const info = this.progInfoOf(id); info.uniforms.clear(); info.locs = []; try { for (const sh of g().getAttachedShaders(p) ?? []) { const sid = this.shaderIds.get(sh) ?? 0; const st = this.shaderType.get(sid) ?? 0; if (st) info.sources.set(st, { sid, src: this.shaderSrc.get(sid) ?? '', compiled: g().getShaderSource(sh) ?? '' }); } } catch { /* lost */ } if (!g().getProgramParameter(p, GL.LINK_STATUS)) { this.badShaders++; this.warn('link:' + id, `program ${id} failed to link: ${(g().getProgramInfoLog(p) || '').trim().slice(0, 400)}`); } }); this.alias('glLinkProgramARB', 'glLinkProgram');
    this.def('glUseProgram', (a) => { const id = U(a[0]); const p = id ? this.prog.get(id) : null; if (id && !p) { this.setErr(GL.INVALID_VALUE); return; } g().useProgram(p ?? null); this.bound.program = id; }); this.alias('glUseProgramObjectARB', 'glUseProgram');
    this.def('glValidateProgram', (a) => { const p = this.prog.get(U(a[0])); if (p) g().validateProgram(p); });
    this.def('glGetProgramiv', (a) => { const id = U(a[0]), pn = U(a[1]); const p = this.prog.get(id); if (!p) { this.setErr(GL.INVALID_VALUE); return; }
      switch (pn) { case GL.INFO_LOG_LENGTH: { const l = g().getProgramInfoLog(p) || ''; this.wr32(a[2], l.length ? l.length + 1 : 0); return; } case GL.ATTACHED_SHADERS: this.wr32(a[2], (g().getAttachedShaders(p) || []).length); return; case GL.PROGRAM_BINARY_LENGTH: this.wr32(a[2], 0); return;
        case GL.ACTIVE_UNIFORM_MAX_LENGTH: { let m = 0; const n = g().getProgramParameter(p, GL.ACTIVE_UNIFORMS) as number; for (let i = 0; i < n; i++) { const u = g().getActiveUniform(p, i); if (u && u.name.length + 1 > m) m = u.name.length + 1; } this.wr32(a[2], m); return; }
        case GL.ACTIVE_ATTRIBUTE_MAX_LENGTH: { let m = 0; const n = g().getProgramParameter(p, GL.ACTIVE_ATTRIBUTES) as number; for (let i = 0; i < n; i++) { const u = g().getActiveAttrib(p, i); if (u && u.name.length + 1 > m) m = u.name.length + 1; } this.wr32(a[2], m); return; }
        case GL.ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH: { let m = 0; const n = g().getProgramParameter(p, GL.ACTIVE_UNIFORM_BLOCKS) as number; for (let i = 0; i < n; i++) { const nm = g().getActiveUniformBlockName(p, i) || ''; if (nm.length + 1 > m) m = nm.length + 1; } this.wr32(a[2], m); return; }
        default: { const v = g().getProgramParameter(p, pn); if (v === null && g().getError()) { this.setErr(GL.INVALID_ENUM); this.wr32(a[2], 0); return; } this.wr32(a[2], typeof v === 'boolean' ? (v ? 1 : 0) : (v as number) | 0); } } });
    this.def('glGetObjectParameterivARB', (a) => { const id = U(a[0]); if (this.prog.has(id)) this.impl[this.byName.get('glGetProgramiv')!]!(a); else this.impl[this.byName.get('glGetShaderiv')!]!(a); });
    this.def('glGetProgramInfoLog', (a) => { const p = this.prog.get(U(a[0])); this.wrstr(a[3], I(a[1]), p ? g().getProgramInfoLog(p) || '' : '', a[2]); });
    this.def('glGetInfoLogARB', (a) => { const id = U(a[0]); const s = this.shader.get(id), p = this.prog.get(id); this.wrstr(a[3], I(a[1]), s ? g().getShaderInfoLog(s) || '' : p ? g().getProgramInfoLog(p) || '' : '', a[2]); });
    this.def('glGetAttachedShaders', (a) => { const p = this.prog.get(U(a[0])); const max = I(a[1]); const sh = p ? g().getAttachedShaders(p) || [] : []; const n = Math.min(max, sh.length); for (let i = 0; i < n; i++) this.wru32(a[3] + BigInt(i * 4), this.shaderIds.get(sh[i]) ?? 0); this.wr32(a[2], n); });
    this.def('glBindAttribLocation', (a) => { const p = this.prog.get(U(a[0])); if (p) g().bindAttribLocation(p, U(a[1]), this.rdstr(a[2])); }); this.alias('glBindAttribLocationARB', 'glBindAttribLocation');
    this.def('glGetAttribLocation', (a) => { const p = this.prog.get(U(a[0])); return p ? g().getAttribLocation(p, this.rdstr(a[1])) : -1; }); this.alias('glGetAttribLocationARB', 'glGetAttribLocation');
    this.def('glGetUniformLocation', (a) => { const id = U(a[0]); const p = this.prog.get(id); if (!p) { this.setErr(GL.INVALID_VALUE); return -1; } const name = this.rdstr(a[1]); const info = this.progInfoOf(id); const c = info.uniforms.get(name); if (c !== undefined) return c; const loc = g().getUniformLocation(p, name); const idx = loc ? info.locs.push(loc) - 1 : -1; info.uniforms.set(name, idx); return idx; }); this.alias('glGetUniformLocationARB', 'glGetUniformLocation');
    this.def('glGetActiveUniform', (a) => { const p = this.prog.get(U(a[0])); const u = p ? g().getActiveUniform(p, U(a[1])) : null; if (!u) { this.setErr(GL.INVALID_VALUE); return; } this.wr32(a[4], u.size); this.wru32(a[5], u.type); this.wrstr(a[6], I(a[2]), u.name, a[3]); });
    this.def('glGetActiveAttrib', (a) => { const p = this.prog.get(U(a[0])); const u = p ? g().getActiveAttrib(p, U(a[1])) : null; if (!u) { this.setErr(GL.INVALID_VALUE); return; } this.wr32(a[4], u.size); this.wru32(a[5], u.type); this.wrstr(a[6], I(a[2]), u.name, a[3]); });
    this.def('glGetUniformfv', (a) => { const p = this.prog.get(U(a[0])); const info = this.progInfo.get(U(a[0])); const loc = info?.locs[I(a[1])]; if (!p || !loc) { this.setErr(GL.INVALID_OPERATION); return; } const v = g().getUniform(p, loc); const arr = typeof v === 'number' ? [v] : typeof v === 'boolean' ? [v ? 1 : 0] : Array.from(v as Float32Array); arr.forEach((x, i) => this.wrf32(a[2] + BigInt(i * 4), x)); });
    this.def('glGetUniformiv', (a) => { const p = this.prog.get(U(a[0])); const info = this.progInfo.get(U(a[0])); const loc = info?.locs[I(a[1])]; if (!p || !loc) { this.setErr(GL.INVALID_OPERATION); return; } const v = g().getUniform(p, loc); const arr = typeof v === 'number' ? [v] : typeof v === 'boolean' ? [v ? 1 : 0] : Array.from(v as Int32Array); arr.forEach((x, i) => this.wr32(a[2] + BigInt(i * 4), x | 0)); }); this.alias('glGetUniformuiv', 'glGetUniformiv');
    this.def('glGetnUniformfv', (a) => { a[2] = a[3]; this.impl[this.byName.get('glGetUniformfv')!]!(a); }); this.def('glGetnUniformiv', (a) => { a[2] = a[3]; this.impl[this.byName.get('glGetUniformiv')!]!(a); }); this.alias('glGetnUniformuiv', 'glGetnUniformiv');
    const uf = (n: number) => (a: BigUint64Array) => { const l = this.uloc(I(a[0])); if (!l) { if (I(a[0]) !== -1) this.setErr(GL.INVALID_OPERATION); return; } const gl = g(); if (n === 1) gl.uniform1f(l, F(a[1])); else if (n === 2) gl.uniform2f(l, F(a[1]), F(a[2])); else if (n === 3) gl.uniform3f(l, F(a[1]), F(a[2]), F(a[3])); else gl.uniform4f(l, F(a[1]), F(a[2]), F(a[3]), F(a[4])); };
    const ui = (n: number, unsigned: boolean) => (a: BigUint64Array) => { const l = this.uloc(I(a[0])); if (!l) { if (I(a[0]) !== -1) this.setErr(GL.INVALID_OPERATION); return; } const gl = g(); const c = unsigned ? U : I; if (n === 1) { if (unsigned) gl.uniform1ui(l, c(a[1])); else gl.uniform1i(l, c(a[1])); } else if (n === 2) { if (unsigned) gl.uniform2ui(l, c(a[1]), c(a[2])); else gl.uniform2i(l, c(a[1]), c(a[2])); } else if (n === 3) { if (unsigned) gl.uniform3ui(l, c(a[1]), c(a[2]), c(a[3])); else gl.uniform3i(l, c(a[1]), c(a[2]), c(a[3])); } else { if (unsigned) gl.uniform4ui(l, c(a[1]), c(a[2]), c(a[3]), c(a[4])); else gl.uniform4i(l, c(a[1]), c(a[2]), c(a[3]), c(a[4])); } };
    const ufv = (n: number) => (a: BigUint64Array) => { const l = this.uloc(I(a[0])); const cnt = I(a[1]); if (!l) { if (I(a[0]) !== -1) this.setErr(GL.INVALID_OPERATION); return; } const v = this.f32(a[2], cnt * n); if (!v) return; const gl = g(); if (n === 1) gl.uniform1fv(l, v); else if (n === 2) gl.uniform2fv(l, v); else if (n === 3) gl.uniform3fv(l, v); else gl.uniform4fv(l, v); };
    const uiv = (n: number, unsigned: boolean) => (a: BigUint64Array) => { const l = this.uloc(I(a[0])); const cnt = I(a[1]); if (!l) { if (I(a[0]) !== -1) this.setErr(GL.INVALID_OPERATION); return; } const v = this.i32(a[2], cnt * n); if (!v) return; const gl = g(); const u = unsigned ? new Uint32Array(v.buffer, v.byteOffset, v.length) : v; if (n === 1) { if (unsigned) gl.uniform1uiv(l, u as Uint32Array); else gl.uniform1iv(l, v); } else if (n === 2) { if (unsigned) gl.uniform2uiv(l, u as Uint32Array); else gl.uniform2iv(l, v); } else if (n === 3) { if (unsigned) gl.uniform3uiv(l, u as Uint32Array); else gl.uniform3iv(l, v); } else { if (unsigned) gl.uniform4uiv(l, u as Uint32Array); else gl.uniform4iv(l, v); } };
    for (const n of [1, 2, 3, 4]) { this.def(`glUniform${n}f`, uf(n)); this.def(`glUniform${n}i`, ui(n, false)); this.def(`glUniform${n}ui`, ui(n, true)); this.def(`glUniform${n}fv`, ufv(n)); this.def(`glUniform${n}iv`, uiv(n, false)); this.def(`glUniform${n}uiv`, uiv(n, true)); }
    const um = (name: string, rows: number, cols: number, fn: (gl: WebGL2RenderingContext, l: WebGLUniformLocation, t: boolean, v: Float32Array) => void) => this.def(name, (a) => { const l = this.uloc(I(a[0])); const cnt = I(a[1]); if (!l) { if (I(a[0]) !== -1) this.setErr(GL.INVALID_OPERATION); return; } const v = this.f32(a[3], cnt * rows * cols); if (v) { fn(g(), l, B(a[2]), v); if (rows === 4 && cols === 4) this.progInfoOf(this.bound.program).mats.set(I(a[0]), v.slice(0, 16)); } });
    um('glUniformMatrix2fv', 2, 2, (gl, l, t, v) => gl.uniformMatrix2fv(l, t, v)); um('glUniformMatrix3fv', 3, 3, (gl, l, t, v) => gl.uniformMatrix3fv(l, t, v)); um('glUniformMatrix4fv', 4, 4, (gl, l, t, v) => gl.uniformMatrix4fv(l, t, v));
    um('glUniformMatrix2x3fv', 2, 3, (gl, l, t, v) => gl.uniformMatrix2x3fv(l, t, v)); um('glUniformMatrix3x2fv', 3, 2, (gl, l, t, v) => gl.uniformMatrix3x2fv(l, t, v)); um('glUniformMatrix2x4fv', 2, 4, (gl, l, t, v) => gl.uniformMatrix2x4fv(l, t, v)); um('glUniformMatrix4x2fv', 4, 2, (gl, l, t, v) => gl.uniformMatrix4x2fv(l, t, v)); um('glUniformMatrix3x4fv', 3, 4, (gl, l, t, v) => gl.uniformMatrix3x4fv(l, t, v)); um('glUniformMatrix4x3fv', 4, 3, (gl, l, t, v) => gl.uniformMatrix4x3fv(l, t, v));
    this.def('glGetUniformBlockIndex', (a) => { const p = this.prog.get(U(a[0])); return p ? g().getUniformBlockIndex(p, this.rdstr(a[1])) : GL.INVALID_INDEX; });
    this.def('glUniformBlockBinding', (a) => { const p = this.prog.get(U(a[0])); if (p) g().uniformBlockBinding(p, U(a[1]), U(a[2])); });
    this.def('glGetActiveUniformBlockiv', (a) => { const p = this.prog.get(U(a[0])); if (!p) { this.setErr(GL.INVALID_VALUE); return; } const v = g().getActiveUniformBlockParameter(p, U(a[1]), U(a[2])); if (typeof v === 'number') this.wr32(a[3], v); else if (typeof v === 'boolean') this.wr32(a[3], v ? 1 : 0); else if (v) (v as Uint32Array).forEach((x, i) => this.wru32(a[3] + BigInt(i * 4), x)); });
    this.def('glGetActiveUniformBlockName', (a) => { const p = this.prog.get(U(a[0])); this.wrstr(a[4], I(a[2]), p ? g().getActiveUniformBlockName(p, U(a[1])) || '' : '', a[3]); });
    this.def('glGetUniformIndices', (a) => { const p = this.prog.get(U(a[0])); const n = I(a[1]); if (!p) { this.setErr(GL.INVALID_VALUE); return; } const names: string[] = []; for (let i = 0; i < n; i++) names.push(this.rdstr(this.rdp(a[2] + BigInt(i * this.mem!.psize())))); const idx = g().getUniformIndices(p, names) || []; idx.forEach((x, i) => this.wru32(a[3] + BigInt(i * 4), x)); });
    this.def('glGetActiveUniformsiv', (a) => { const p = this.prog.get(U(a[0])); const n = I(a[1]); const idx = this.i32(a[2], n); if (!p || !idx) { this.setErr(GL.INVALID_VALUE); return; } const v = g().getActiveUniforms(p, Array.from(idx, (x) => x >>> 0), U(a[3])) as (number | boolean)[]; v.forEach((x, i) => this.wr32(a[4] + BigInt(i * 4), typeof x === 'boolean' ? (x ? 1 : 0) : x)); });
    this.def('glGetFragDataLocation', (a) => { const p = this.prog.get(U(a[0])); return p ? g().getFragDataLocation(p, this.rdstr(a[1]).replace(/^gl_FragColor$/, 'nebula_FragColor')) : -1; });
    this.def('glBindFragDataLocation', (a) => { if (U(a[1]) !== 0) this.warn('fragdata', 'glBindFragDataLocation with colour number > 0: WebGL2 output locations come from layout() qualifiers only — declare them in the shader'); }); this.def('glBindFragDataLocationIndexed', () => this.warn('fragdataidx', 'glBindFragDataLocationIndexed: dual-source blending is not available in WebGL2')); this.def('glGetFragDataIndex', () => 0);
    this.def('glTransformFeedbackVaryings', (a) => { const p = this.prog.get(U(a[0])); const n = I(a[1]); if (!p) { this.setErr(GL.INVALID_VALUE); return; } const names: string[] = []; for (let i = 0; i < n; i++) names.push(this.rdstr(this.rdp(a[2] + BigInt(i * this.mem!.psize())))); g().transformFeedbackVaryings(p, names, U(a[3])); });
    this.def('glGetTransformFeedbackVarying', (a) => { const p = this.prog.get(U(a[0])); const v = p ? g().getTransformFeedbackVarying(p, U(a[1])) : null; if (!v) { this.setErr(GL.INVALID_VALUE); return; } this.wr32(a[4], v.size); this.wru32(a[5], v.type); this.wrstr(a[6], I(a[2]), v.name, a[3]); });
    this.def('glGetProgramBinary', (a) => { this.wr32(a[2], 0); this.setErr(GL.INVALID_OPERATION); this.warn('progbin', 'glGetProgramBinary: program binaries are not available in WebGL2 (GL_NUM_PROGRAM_BINARY_FORMATS = 0)'); }); this.def('glProgramBinary', () => this.setErr(GL.INVALID_ENUM)); this.def('glProgramParameteri', () => {});
    // -- framebuffers / renderbuffers --
    this.def('glGenFramebuffers', (a) => this.gen(I(a[0]), a[1], 'fbo', () => this.next.fbo++)); this.alias('glGenFramebuffersEXT', 'glGenFramebuffers');
    this.def('glDeleteFramebuffers', (a) => { for (const id of this.ids(I(a[0]), a[1])) { const f = this.fbo.get(id); if (f) { g().deleteFramebuffer(f); this.fboIds.delete(f); } this.fbo.delete(id); if (this.bound.fbDraw === id) this.bound.fbDraw = 0; if (this.bound.fbRead === id) this.bound.fbRead = 0; } }); this.alias('glDeleteFramebuffersEXT', 'glDeleteFramebuffers');
    this.def('glIsFramebuffer', (a) => this.fbo.has(U(a[0])) ? 1 : 0); this.alias('glIsFramebufferEXT', 'glIsFramebuffer');
    this.def('glBindFramebuffer', (a) => { const target = U(a[0]), id = U(a[1]); let f: WebGLFramebuffer | null = null; if (id) { f = this.fbo.get(id) ?? null; if (!f) { f = g().createFramebuffer()!; this.fbo.set(id, f); this.fboIds.set(f, id); } } g().bindFramebuffer(target, f); if (target === GL.FRAMEBUFFER || target === GL.DRAW_FRAMEBUFFER) this.bound.fbDraw = id; if (target === GL.FRAMEBUFFER || target === GL.READ_FRAMEBUFFER) this.bound.fbRead = id; }); this.alias('glBindFramebufferEXT', 'glBindFramebuffer');
    this.def('glGenRenderbuffers', (a) => this.gen(I(a[0]), a[1], 'rbo', () => this.next.rbo++)); this.alias('glGenRenderbuffersEXT', 'glGenRenderbuffers');
    this.def('glDeleteRenderbuffers', (a) => { for (const id of this.ids(I(a[0]), a[1])) { const r = this.rbo.get(id); if (r) { g().deleteRenderbuffer(r); this.rboIds.delete(r); } this.rbo.delete(id); this.rboInfo.delete(id); if (this.bound.rbo === id) this.bound.rbo = 0; } }); this.alias('glDeleteRenderbuffersEXT', 'glDeleteRenderbuffers');
    this.def('glIsRenderbuffer', (a) => this.rbo.has(U(a[0])) ? 1 : 0); this.alias('glIsRenderbufferEXT', 'glIsRenderbuffer');
    this.def('glBindRenderbuffer', (a) => { const id = U(a[1]); let r: WebGLRenderbuffer | null = null; if (id) { r = this.rbo.get(id) ?? null; if (!r) { r = g().createRenderbuffer()!; this.rbo.set(id, r); this.rboIds.set(r, id); } } g().bindRenderbuffer(GL.RENDERBUFFER, r); this.bound.rbo = id; }); this.alias('glBindRenderbufferEXT', 'glBindRenderbuffer');
    this.def('glRenderbufferStorage', (a) => { const f = this.rboFormat(U(a[1])); g().renderbufferStorage(GL.RENDERBUFFER, f, I(a[2]), I(a[3])); this.rboInfo.set(this.bound.rbo, { w: I(a[2]), h: I(a[3]), ifmt: f, samples: 0 }); }); this.alias('glRenderbufferStorageEXT', 'glRenderbufferStorage');
    this.def('glRenderbufferStorageMultisample', (a) => { const f = this.rboFormat(U(a[2])); const s = Math.min(I(a[1]), g().getParameter(g().MAX_SAMPLES) as number); g().renderbufferStorageMultisample(GL.RENDERBUFFER, s, f, I(a[3]), I(a[4])); this.rboInfo.set(this.bound.rbo, { w: I(a[3]), h: I(a[4]), ifmt: f, samples: s }); }); this.alias('glRenderbufferStorageMultisampleEXT', 'glRenderbufferStorageMultisample');
    this.def('glGetRenderbufferParameteriv', (a) => { const p = U(a[1]); const info = this.rboInfo.get(this.bound.rbo); if (p === GL.RENDERBUFFER_INTERNAL_FORMAT && info) { this.wr32(a[2], info.ifmt); return; } this.wr32(a[2], (g().getRenderbufferParameter(GL.RENDERBUFFER, p) as number) | 0); }); this.alias('glGetRenderbufferParameterivEXT', 'glGetRenderbufferParameteriv');
    this.def('glFramebufferTexture2D', (a) => { const id = U(a[3]); g().framebufferTexture2D(U(a[0]), U(a[1]), U(a[2]), id ? this.tex.get(id) ?? null : null, I(a[4])); }); this.alias('glFramebufferTexture2DEXT', 'glFramebufferTexture2D');
    this.def('glFramebufferTexture', (a) => { const id = U(a[2]); const ti = this.texInfo.get(id); const t = id ? this.tex.get(id) ?? null : null; if (ti && (ti.target === GL.TEXTURE_2D_ARRAY || ti.target === GL.TEXTURE_3D)) g().framebufferTextureLayer(U(a[0]), U(a[1]), t, I(a[3]), 0); else g().framebufferTexture2D(U(a[0]), U(a[1]), ti?.target === GL.TEXTURE_CUBE_MAP ? GL.TEXTURE_CUBE_MAP_POSITIVE_X : GL.TEXTURE_2D, t, I(a[3])); });
    this.def('glFramebufferTextureLayer', (a) => { const id = U(a[2]); g().framebufferTextureLayer(U(a[0]), U(a[1]), id ? this.tex.get(id) ?? null : null, I(a[3]), I(a[4])); });
    this.def('glFramebufferTexture1D', () => this.setErr(GL.INVALID_ENUM)); this.def('glFramebufferTexture3D', (a) => { const id = U(a[3]); g().framebufferTextureLayer(U(a[0]), U(a[1]), id ? this.tex.get(id) ?? null : null, I(a[4]), I(a[5])); });
    this.def('glFramebufferRenderbuffer', (a) => { const id = U(a[3]); g().framebufferRenderbuffer(U(a[0]), U(a[1]), GL.RENDERBUFFER, id ? this.rbo.get(id) ?? null : null); }); this.alias('glFramebufferRenderbufferEXT', 'glFramebufferRenderbuffer');
    this.def('glCheckFramebufferStatus', (a) => this.fbStatus(U(a[0]))); this.alias('glCheckFramebufferStatusEXT', 'glCheckFramebufferStatus');
    this.def('glGetFramebufferAttachmentParameteriv', (a) => { const v = g().getFramebufferAttachmentParameter(U(a[0]), U(a[1]), U(a[2])); if (v && typeof v === 'object') this.wr32(a[3], this.texIds.get(v as WebGLTexture) ?? this.rboIds.get(v as WebGLRenderbuffer) ?? 0); else this.wr32(a[3], typeof v === 'number' ? v : 0); }); this.alias('glGetFramebufferAttachmentParameterivEXT', 'glGetFramebufferAttachmentParameteriv');
    this.def('glBlitFramebuffer', (a) => g().blitFramebuffer(I(a[0]), I(a[1]), I(a[2]), I(a[3]), I(a[4]), I(a[5]), I(a[6]), I(a[7]), U(a[8]), U(a[9]))); this.alias('glBlitFramebufferEXT', 'glBlitFramebuffer');
    this.def('glInvalidateFramebuffer', (a) => { const v = this.i32(a[2], I(a[1])); if (v) g().invalidateFramebuffer(U(a[0]), Array.from(v, (x) => x >>> 0)); }); this.def('glInvalidateSubFramebuffer', (a) => { const v = this.i32(a[2], I(a[1])); if (v) g().invalidateSubFramebuffer(U(a[0]), Array.from(v, (x) => x >>> 0), I(a[3]), I(a[4]), I(a[5]), I(a[6])); });
    this.def('glClearBufferfv', (a) => { const b = U(a[0]); const v = this.f32(a[2], b === 0x1801 /* DEPTH */ ? 1 : 4); if (v) g().clearBufferfv(b, I(a[1]), v); }); this.def('glClearBufferiv', (a) => { const b = U(a[0]); const v = this.i32(a[2], b === 0x1802 /* STENCIL */ ? 1 : 4); if (v) g().clearBufferiv(b, I(a[1]), v); }); this.def('glClearBufferuiv', (a) => { const v = this.i32(a[2], 4); if (v) g().clearBufferuiv(U(a[0]), I(a[1]), new Uint32Array(v.buffer, v.byteOffset, 4)); }); this.def('glClearBufferfi', (a) => g().clearBufferfi(U(a[0]), I(a[1]), F(a[2]), I(a[3])));
    this.def('glReadPixels', (a) => this.readPixels(I(a[0]), I(a[1]), I(a[2]), I(a[3]), U(a[4]), U(a[5]), a[6])); this.def('glReadnPixels', (a) => this.readPixels(I(a[0]), I(a[1]), I(a[2]), I(a[3]), U(a[4]), U(a[5]), a[7]));
    this.def('glCopyImageSubData', () => { this.setErr(GL.INVALID_OPERATION); this.warn('copyimage', 'glCopyImageSubData: not available in WebGL2 (use glBlitFramebuffer / glCopyTexSubImage2D)'); });
    this.def('glGetInternalformativ', (a) => { const v = g().getInternalformatParameter(U(a[0]), U(a[1]), U(a[2])) as Int32Array | null; const n = I(a[3]); if (v) for (let i = 0; i < Math.min(n, v.length); i++) this.wr32(a[4] + BigInt(i * 4), v[i]); else this.wr32(a[4], 0); });
    // -- queries / sync / samplers / transform feedback --
    this.def('glGenQueries', (a) => this.gen(I(a[0]), a[1], 'query', () => this.next.query++)); this.alias('glGenQueriesARB', 'glGenQueries');
    this.def('glDeleteQueries', (a) => { for (const id of this.ids(I(a[0]), a[1])) { const q = this.query.get(id); if (q) { g().deleteQuery(q); this.queryIds.delete(q); } this.query.delete(id); this.queryTarget.delete(id); } }); this.alias('glDeleteQueriesARB', 'glDeleteQueries');
    this.def('glIsQuery', (a) => this.query.has(U(a[0])) ? 1 : 0); this.alias('glIsQueryARB', 'glIsQuery');
    this.def('glBeginQuery', (a) => { const t = this.queryTargetOf(U(a[0])), id = U(a[1]); if (t === GL.TIME_ELAPSED || t === GL.TIMESTAMP) { this.setErr(GL.INVALID_ENUM); this.warn('timerq', 'GL_TIME_ELAPSED queries are not available (no EXT_disjoint_timer_query_webgl2 on this device)'); return; } let q = this.query.get(id); if (!q) { q = g().createQuery()!; this.query.set(id, q); this.queryIds.set(q, id); } this.queryTarget.set(id, t); g().beginQuery(t, q); }); this.alias('glBeginQueryARB', 'glBeginQuery');
    this.def('glEndQuery', (a) => { const t = this.queryTargetOf(U(a[0])); if (t === GL.TIME_ELAPSED) return; g().endQuery(t); }); this.alias('glEndQueryARB', 'glEndQuery');
    this.def('glQueryCounter', () => { this.setErr(GL.INVALID_ENUM); this.warn('timerq', 'glQueryCounter(GL_TIMESTAMP): timer queries are not available in WebGL2'); });
    this.def('glGetQueryiv', (a) => { const p = U(a[1]); if (p === GL.CURRENT_QUERY) { const q = g().getQuery(this.queryTargetOf(U(a[0])), p) as WebGLQuery | null; this.wr32(a[2], q ? this.queryIds.get(q) ?? 0 : 0); } else this.wr32(a[2], p === 0x8864 /* QUERY_COUNTER_BITS */ ? 32 : 0); }); this.alias('glGetQueryivARB', 'glGetQueryiv');
    const qr = (a: BigUint64Array) => { const q = this.query.get(U(a[0])); if (!q) { this.setErr(GL.INVALID_OPERATION); return 0; } const v = g().getQueryParameter(q, U(a[1]) === GL.QUERY_RESULT_AVAILABLE ? GL.QUERY_RESULT_AVAILABLE : GL.QUERY_RESULT); return typeof v === 'boolean' ? (v ? 1 : 0) : (v as number); };
    this.def('glGetQueryObjectiv', (a) => this.wr32(a[2], qr(a))); this.def('glGetQueryObjectuiv', (a) => this.wru32(a[2], qr(a))); this.def('glGetQueryObjecti64v', (a) => this.wr64(a[2], BigInt(qr(a)))); this.def('glGetQueryObjectui64v', (a) => this.wr64(a[2], BigInt(qr(a)))); this.alias('glGetQueryObjectivARB', 'glGetQueryObjectiv'); this.alias('glGetQueryObjectuivARB', 'glGetQueryObjectuiv');
    this.def('glBeginConditionalRender', () => this.warn('condrender', 'glBeginConditionalRender: conditional rendering is not available in WebGL2 (draws always happen)')); this.def('glEndConditionalRender', () => {});
    this.def('glFenceSync', (a) => { const s = g().fenceSync(U(a[0]), U(a[1])); if (!s) return 0n; const h = this.nextSync; this.nextSync += 8n; this.sync.set(h, s); return h; }); this.alias('glFenceSyncARB', 'glFenceSync');
    this.def('glDeleteSync', (a) => { const s = this.sync.get(a[0]); if (s) { g().deleteSync(s); this.sync.delete(a[0]); } }); this.alias('glDeleteSyncARB', 'glDeleteSync');
    this.def('glIsSync', (a) => this.sync.has(a[0]) ? 1 : 0);
    this.def('glClientWaitSync', (a) => { const s = this.sync.get(a[0]); if (!s) { this.setErr(GL.INVALID_VALUE); return GL.WAIT_FAILED; } const r = g().clientWaitSync(s, U(a[1]) & 1, 0); if (r === GL.TIMEOUT_EXPIRED && a[2] > 0n) { g().flush(); return GL.TIMEOUT_EXPIRED; } return r; }); this.alias('glClientWaitSyncARB', 'glClientWaitSync');
    this.def('glWaitSync', (a) => { const s = this.sync.get(a[0]); if (s) g().waitSync(s, 0, -1); }); this.alias('glWaitSyncARB', 'glWaitSync');
    this.def('glGetSynciv', (a) => { const s = this.sync.get(a[0]); if (!s) { this.setErr(GL.INVALID_VALUE); return; } const v = g().getSyncParameter(s, U(a[1])) as number; this.wr32(a[3], 1); this.wr32(a[4], v); }); this.alias('glGetSyncivARB', 'glGetSynciv');
    this.def('glGenSamplers', (a) => this.gen(I(a[0]), a[1], 'sampler', () => this.next.sampler++));
    this.def('glDeleteSamplers', (a) => { for (const id of this.ids(I(a[0]), a[1])) { const s = this.sampler.get(id); if (s) { g().deleteSampler(s); this.samplerIds.delete(s); } this.sampler.delete(id); } });
    this.def('glIsSampler', (a) => this.sampler.has(U(a[0])) ? 1 : 0);
    this.def('glBindSampler', (a) => { const id = U(a[1]); let s: WebGLSampler | null = null; if (id) { s = this.sampler.get(id) ?? null; if (!s) { s = g().createSampler()!; this.sampler.set(id, s); this.samplerIds.set(s, id); } } g().bindSampler(U(a[0]), s); });
    const sp = (a: BigUint64Array, v: number) => { const id = U(a[0]); let s = this.sampler.get(id); if (!s) { s = g().createSampler()!; this.sampler.set(id, s); this.samplerIds.set(s, id); } const p = U(a[1]); if (p === GL.TEXTURE_BORDER_COLOR || p === GL.TEXTURE_LOD_BIAS) return; if (p === GL.TEXTURE_MAX_ANISOTROPY_EXT) { if (this.aniso) g().samplerParameterf(s, this.aniso.TEXTURE_MAX_ANISOTROPY_EXT, v); return; } if ((p === GL.TEXTURE_WRAP_S || p === GL.TEXTURE_WRAP_T || p === GL.TEXTURE_WRAP_R) && (v === GL.CLAMP || v === GL.CLAMP_TO_BORDER || v === GL.MIRROR_CLAMP_TO_EDGE)) v = GL.CLAMP_TO_EDGE; if (p === GL.TEXTURE_MIN_LOD || p === GL.TEXTURE_MAX_LOD) g().samplerParameterf(s, p, v); else g().samplerParameteri(s, p, v | 0); };
    this.def('glSamplerParameteri', (a) => sp(a, I(a[2]))); this.def('glSamplerParameterf', (a) => sp(a, F(a[2]))); this.def('glSamplerParameteriv', (a) => { const v = this.i32(a[2], 1); if (v) sp(a, v[0]); }); this.def('glSamplerParameterfv', (a) => { const v = this.f32(a[2], 1); if (v) sp(a, v[0]); }); this.alias('glSamplerParameterIiv', 'glSamplerParameteriv'); this.alias('glSamplerParameterIuiv', 'glSamplerParameteriv');
    this.def('glGetSamplerParameteriv', (a) => { const s = this.sampler.get(U(a[0])); const v = s ? g().getSamplerParameter(s, U(a[1])) as number : 0; this.wr32(a[2], v | 0); }); this.def('glGetSamplerParameterfv', (a) => { const s = this.sampler.get(U(a[0])); const v = s ? g().getSamplerParameter(s, U(a[1])) as number : 0; this.wrf32(a[2], v); });
    this.def('glGenTransformFeedbacks', (a) => this.gen(I(a[0]), a[1], 'tfb', () => this.next.tfb++)); this.def('glDeleteTransformFeedbacks', (a) => { for (const id of this.ids(I(a[0]), a[1])) { const t = this.tfb.get(id); if (t) { g().deleteTransformFeedback(t); this.tfbIds.delete(t); } this.tfb.delete(id); } }); this.def('glIsTransformFeedback', (a) => this.tfb.has(U(a[0])) ? 1 : 0);
    this.def('glBindTransformFeedback', (a) => { const id = U(a[1]); let t: WebGLTransformFeedback | null = null; if (id) { t = this.tfb.get(id) ?? null; if (!t) { t = g().createTransformFeedback()!; this.tfb.set(id, t); this.tfbIds.set(t, id); } } g().bindTransformFeedback(U(a[0]), t); });
    this.def('glBeginTransformFeedback', (a) => g().beginTransformFeedback(U(a[0]))); this.def('glEndTransformFeedback', () => g().endTransformFeedback()); this.def('glPauseTransformFeedback', () => g().pauseTransformFeedback()); this.def('glResumeTransformFeedback', () => g().resumeTransformFeedback());
    // -- debug output (KHR_debug): accepted, messages go to the emulator log; no callbacks into the guest from here --
    this.def('glDebugMessageCallback', () => this.warn('debugcb', 'glDebugMessageCallback: GL debug messages are written to the emulator log instead of calling back into the program')); this.alias('glDebugMessageCallbackARB', 'glDebugMessageCallback'); this.def('glDebugMessageControl', () => {}); this.alias('glDebugMessageControlARB', 'glDebugMessageControl'); this.def('glDebugMessageInsert', (a) => this.log(`[gl] app: ${this.rdstr(a[5], I(a[4]) >= 0 ? I(a[4]) : -1)}\n`)); this.def('glObjectLabel', () => {}); this.def('glGetObjectLabel', (a) => this.wrstr(a[4], I(a[2]), '', a[3])); this.def('glPushDebugGroup', () => {}); this.def('glPopDebugGroup', () => {});
    // -- the fixed-function pipeline of GL 1.x + GLU: emulated on top of WebGL2 (glff.ts) --
    this.ff.register();
    // -- whatever GL 1.1 export is still missing: a core-profile error, logged once per entry point --
    GL_FUNCS.forEach(([n, , exported], id) => { if (exported && n.startsWith('gl') && !this.impl[id]) this.impl[id] = () => this.legacy(n); });   // GL 1.1 exports always resolve; unimplemented extensions stay unprobed (wglGetProcAddress → NULL)
  }
  // ---- implementations shared by several entry points ----
  private enable(cap: number, on: boolean) {
    const gl = this.gl!;
    if (this.ff.enable(cap, on)) return;
    switch (cap) {
      case GL.TEXTURE_3D: case GL.TEXTURE_CUBE_MAP: case GL.TEXTURE_RECTANGLE: case GL.MULTISAMPLE: case GL.POINT_SMOOTH: case GL.LINE_SMOOTH: case GL.POLYGON_SMOOTH: case GL.TEXTURE_CUBE_MAP_SEAMLESS: case GL.PROGRAM_POINT_SIZE: case GL.POINT_SPRITE: case GL.FRAMEBUFFER_SRGB: case GL.SAMPLE_ALPHA_TO_ONE: case GL.DEBUG_OUTPUT: case GL.DEBUG_OUTPUT_SYNCHRONOUS: case GL.SAMPLE_SHADING: case GL.PRIMITIVE_RESTART_FIXED_INDEX: return;   // always-on / not-applicable in WebGL2: silently accepted like a permissive driver
      case GL.PRIMITIVE_RESTART: this.warn('restart', 'GL_PRIMITIVE_RESTART: WebGL2 always restarts on the fixed index (0xFFFF/0xFFFFFFFF); glPrimitiveRestartIndex cannot change it'); return;
      case GL.DEPTH_CLAMP: if (on) this.warn('depthclamp', 'GL_DEPTH_CLAMP: not available in WebGL2 — ignored'); return;
      case GL.LIGHTING: case GL.COLOR_MATERIAL: case GL.NORMALIZE: case GL.FOG: case GL.TEXTURE_GEN_S: case GL.TEXTURE_GEN_T: case GL.TEXTURE_GEN_R: case GL.TEXTURE_GEN_Q: case GL.COLOR_SUM: case GL.LINE_STIPPLE: case GL.POLYGON_STIPPLE: case GL.COLOR_LOGIC_OP: case GL.INDEX_LOGIC_OP: if (on) this.legacy(`glEnable(0x${cap.toString(16)})`); return;
    }
    if (cap >= GL.CLIP_PLANE0 && cap < GL.CLIP_PLANE0 + 8) { if (on) this.legacy('glEnable(GL_CLIP_PLANEi)'); return; }
    if (WEBGL_CAPS.has(cap)) { if (on) gl.enable(cap); else gl.disable(cap); return; }   // no getError round trip (a synchronous GPU-process query) on the hot path
    if (this.badCaps.has(cap)) { this.setErr(GL.INVALID_ENUM); return; }
    if (on) gl.enable(cap); else gl.disable(cap);
    if (gl.getError() === GL.INVALID_ENUM) { this.badCaps.add(cap); this.setErr(GL.INVALID_ENUM); this.warn('cap:' + cap, `glEnable/glDisable(0x${cap.toString(16)}): unknown capability → GL_INVALID_ENUM`); } }
  private badCaps = new Set<number>();
  private pixelStore(p: number, v: number) {
    const gl = this.gl!;
    switch (p) { case GL.UNPACK_ALIGNMENT: this.unpackAlign = v; break; case GL.PACK_ALIGNMENT: this.packAlign = v; break; case GL.UNPACK_ROW_LENGTH: this.unpackRowLength = v; break; case GL.UNPACK_SKIP_ROWS: this.unpackSkipRows = v; break; case GL.UNPACK_SKIP_PIXELS: this.unpackSkipPixels = v; break; case GL.UNPACK_IMAGE_HEIGHT: this.unpackImageHeight = v; break; case GL.PACK_ROW_LENGTH: this.packRowLength = v; break;
      case GL.UNPACK_SWAP_BYTES: case GL.UNPACK_LSB_FIRST: case GL.PACK_SWAP_BYTES: case GL.PACK_LSB_FIRST: if (v) this.warn('pixelstore:' + p, `glPixelStore(0x${p.toString(16)}): byte swapping is not available in WebGL2 — ignored`); return; }
    if (WEBGL_PIXELSTORE.has(p)) gl.pixelStorei(p, v); else this.setErr(GL.INVALID_ENUM); }
  private getString(name: number): bigint {
    const gl = this.gl!;
    switch (name) { case GL.VENDOR: return this.gstr('Nebula OS'); case GL.RENDERER: return this.gstr(`WebGL2 bridge (${this.describeRenderer()})`); case GL.VERSION: return this.gstr('3.3.0 NebulaGL (WebGL2 bridge)'); case GL.SHADING_LANGUAGE_VERSION: return this.gstr('3.30 NebulaGL (GLSL ES 3.00)'); case GL.EXTENSIONS: return this.gstr(this.extList.join(' '));
      default: this.setErr(GL.INVALID_ENUM); void gl; return 0n; } }
  private texLevelParam(target: number, level: number, p: number): number { const ti = this.texInfo.get(this.curTex(target)); const l = ti?.levels.get(level); if (!l) return 0; switch (p) { case GL.TEXTURE_WIDTH: return l.w; case GL.TEXTURE_HEIGHT: return l.h; case GL.TEXTURE_DEPTH: return l.d; case GL.TEXTURE_INTERNAL_FORMAT: return l.ifmt; case GL.TEXTURE_RED_SIZE: case GL.TEXTURE_GREEN_SIZE: case GL.TEXTURE_BLUE_SIZE: case GL.TEXTURE_ALPHA_SIZE: return 8; case GL.TEXTURE_DEPTH_SIZE: return l.ifmt === GL.DEPTH_COMPONENT16 ? 16 : l.ifmt === GL.DEPTH_COMPONENT24 || l.ifmt === GL.DEPTH24_STENCIL8 ? 24 : l.ifmt === GL.DEPTH_COMPONENT32F ? 32 : 0; case GL.TEXTURE_COMPRESSED: return l.ifmt >= 0x83F0 && l.ifmt <= 0x83F3 ? 1 : 0; default: return 0; } }
  private compressedOk(fmt: number): boolean { if (fmt >= GL.COMPRESSED_RGB_S3TC_DXT1_EXT && fmt <= GL.COMPRESSED_RGBA_S3TC_DXT5_EXT) { if (this.s3tc) return true; this.setErr(GL.INVALID_ENUM); this.warn('s3tc', 'S3TC/DXT compressed textures are not supported by this device\'s WebGL2 (WEBGL_compressed_texture_s3tc missing)'); return false; } return true; }
  private vertexAttribParam(i: number, p: number): number { if (i >= this.maxAttribs) { this.setErr(GL.INVALID_VALUE); return 0; } const s = this.curAttribs()[i]; switch (p) { case GL.VERTEX_ATTRIB_ARRAY_ENABLED: return s.enabled ? 1 : 0; case GL.VERTEX_ATTRIB_ARRAY_SIZE: return s.size; case GL.VERTEX_ATTRIB_ARRAY_STRIDE: return s.stride; case GL.VERTEX_ATTRIB_ARRAY_TYPE: return s.type; case GL.VERTEX_ATTRIB_ARRAY_NORMALIZED: return s.normalized ? 1 : 0; case GL.VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: return s.buffer; case GL.VERTEX_ATTRIB_ARRAY_DIVISOR: return s.divisor; case GL.VERTEX_ATTRIB_ARRAY_INTEGER: return s.integer ? 1 : 0; default: { const v = this.gl!.getVertexAttrib(i, p); return typeof v === 'number' ? v : typeof v === 'boolean' ? (v ? 1 : 0) : 0; } } }
  /** bytes of the bound element array buffer from a CPU mirror (filled once per buffer version) — avoids a synchronous getBufferSubData per draw call */
  private elementData(offset: number, bytes: number): Uint8Array | null {
    const gl = this.gl!; const info = this.bufInfo.get(this.bound.element); if (!info || offset < 0 || offset + bytes > info.size) return null;
    if (!info.shadow || info.shadow.length !== info.size) { info.shadow = new Uint8Array(info.size); gl.getBufferSubData(gl.ELEMENT_ARRAY_BUFFER, 0, info.shadow); }
    return info.shadow.slice(offset, offset + bytes);   // a fresh, 0-based copy so typed-array views over it are aligned
  }
  private bufObj(id: number): WebGLBuffer { let b = this.buf.get(id); if (!b) { b = this.gl!.createBuffer()!; this.buf.set(id, b); this.bufIds.set(b, id); this.bufInfo.set(id, { size: 0, usage: GL.STATIC_DRAW, version: 0, shadow: null, kind: 0, twin: null, stale: 0 }); } return b; }
  private kindOf(target: number): number { return target === GL.ELEMENT_ARRAY_BUFFER ? 2 : 1; }
  /** the WebGL object of buffer `id` that may legally be bound to `target` (no synchronisation — use after objFor() has run for that pairing) */
  private bufFor(id: number, target: number): WebGLBuffer | null { if (!id) return null; const b = this.buf.get(id); if (!b) return null; const info = this.bufInfo.get(id); if (!info || !info.twin || !info.kind || info.kind === this.kindOf(target)) return b; return info.twin; }
  /** the object for binding `id` to `target`, creating / refreshing the twin of the other kind when the program uses one buffer for both index and vertex data */
  private objFor(id: number, target: number): WebGLBuffer {
    const gl = this.gl!; const b = this.bufObj(id); const info = this.bufInfo.get(id)!; const k = this.kindOf(target);
    if (!info.kind) { info.kind = k; return b; }
    if (info.kind === k) { if (info.stale === 1 && info.twin) this.syncBuf(info, info.twin, b, k); return b; }
    if (!info.twin) { info.twin = gl.createBuffer()!; this.warn('bufdual:' + id, `buffer ${id} is used both as ${info.kind === 2 ? 'an element array and a vertex/data' : 'a vertex/data and an element array'} buffer — WebGL2 allows one kind per object, so the bridge keeps two synchronised copies`); this.syncBuf(info, b, info.twin, k); }
    else if (info.stale === 2) this.syncBuf(info, b, info.twin, k);
    return info.twin; }
  private syncBuf(info: BufInfo, from: WebGLBuffer, to: WebGLBuffer, toKind: number): void {
    const gl = this.gl!; const fromT = toKind === 2 ? gl.COPY_READ_BUFFER : gl.ELEMENT_ARRAY_BUFFER; const toT = toKind === 2 ? gl.ELEMENT_ARRAY_BUFFER : gl.COPY_WRITE_BUFFER;
    const data = new Uint8Array(info.size); gl.bindBuffer(fromT, from); if (info.size) gl.getBufferSubData(fromT, 0, data); gl.bindBuffer(toT, to); gl.bufferData(toT, data, info.usage); info.stale = 0;
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.bufFor(this.bound.element, GL.ELEMENT_ARRAY_BUFFER)); gl.bindBuffer(gl.COPY_READ_BUFFER, this.bufFor(this.bound.copyRead, GL.COPY_READ_BUFFER)); gl.bindBuffer(gl.COPY_WRITE_BUFFER, this.bufFor(this.bound.copyWrite, GL.COPY_WRITE_BUFFER)); }
  /** after data went into the buffer bound at `target`: the object of the other kind (if any) is now outdated; refresh at once when it is bound too */
  private touched(target: number): void {
    const id = this.bufTargetId(target); const info = this.bufInfo.get(id); if (!id || !info || !info.twin) return; const k = this.kindOf(target); info.stale = k === info.kind ? 2 : 1;
    const b = this.bound; const otherBound = k === 2 ? (b.array === id || b.pack === id || b.unpack === id || b.uniform === id || b.copyRead === id || b.copyWrite === id || b.tfb === id) : b.element === id;
    if (otherBound) { const main = this.buf.get(id)!; if (info.stale === 1) this.syncBuf(info, info.twin, main, info.kind); else this.syncBuf(info, main, info.twin, info.kind === 2 ? 1 : 2); } }
  private setBound(target: number, id: number) { switch (target) { case GL.ARRAY_BUFFER: this.bound.array = id; break; case GL.ELEMENT_ARRAY_BUFFER: this.bound.element = id; break; case GL.PIXEL_PACK_BUFFER: this.bound.pack = id; break; case GL.PIXEL_UNPACK_BUFFER: this.bound.unpack = id; break; case GL.UNIFORM_BUFFER: this.bound.uniform = id; break; case GL.COPY_READ_BUFFER: this.bound.copyRead = id; break; case GL.COPY_WRITE_BUFFER: this.bound.copyWrite = id; break; case GL.TRANSFORM_FEEDBACK_BUFFER: this.bound.tfb = id; break; } }
  private bindBuffer(target: number, id: number) { const gl = this.gl!; if (target === GL.ARRAY_BUFFER || target === GL.ELEMENT_ARRAY_BUFFER || target === GL.PIXEL_PACK_BUFFER || target === GL.PIXEL_UNPACK_BUFFER || target === GL.UNIFORM_BUFFER || target === GL.COPY_READ_BUFFER || target === GL.COPY_WRITE_BUFFER || target === GL.TRANSFORM_FEEDBACK_BUFFER) { gl.bindBuffer(target, id ? this.objFor(id, target) : null); this.setBound(target, id); } else { this.setErr(GL.INVALID_ENUM); this.warn('buftarget:' + target, `glBindBuffer(0x${target.toString(16)}): this buffer target does not exist in WebGL2 → GL_INVALID_ENUM`); } }
  private mapBuffer(target: number, offset: number, length: number, access: number): bigint {
    const gl = this.gl!; const id = this.bufTargetId(target); const info = this.bufInfo.get(id); if (!id || !info) { this.setErr(GL.INVALID_OPERATION); return 0n; }
    if (this.maps.some((m) => m.buffer === id)) { this.setErr(GL.INVALID_OPERATION); return 0n; }
    if (length < 0) length = info.size - offset; if (length < 0 || offset + length > info.size) { this.setErr(GL.INVALID_VALUE); return 0n; }
    const ptr = this.mem!.alloc(Math.max(length, 16)); if (!ptr) { this.setErr(GL.OUT_OF_MEMORY); return 0n; }
    if (access & GL.MAP_READ_BIT) { const d = this.u8(ptr, length, true); if (d) gl.getBufferSubData(target, offset, d); }
    this.maps.push({ buffer: id, target, offset, length, ptr, access }); this.warn('mapbuffer', 'glMapBuffer(Range): emulated with a CPU copy that is uploaded at glUnmapBuffer (WebGL2 has no persistent mapping)'); return ptr; }
  private unmapBuffer(target: number): number {
    const gl = this.gl!; const id = this.bufTargetId(target); const k = this.maps.findIndex((m) => m.buffer === id); if (k < 0) { this.setErr(GL.INVALID_OPERATION); return 0; }
    const m = this.maps[k]; this.maps.splice(k, 1);
    if ((m.access & GL.MAP_WRITE_BIT) && !(m.access & GL.MAP_FLUSH_EXPLICIT_BIT)) { const d = this.u8(m.ptr, m.length); if (d) { gl.bufferSubData(target, m.offset, d); const info = this.bufInfo.get(id); if (info) { info.version++; info.shadow = null; } this.touched(target); } }
    this.mem!.free(m.ptr); return 1; }
  private drawArrays(mode: number, first: number, count: number, instances: number) {
    if (this.digestLeft > 0 && count > 0) { const d = this.digestBegin('glDrawArrays', mode, count, 0, 0n, first); try { this.drawArraysImpl(mode, first, count, instances); } finally { this.digestEnd(d); } return; }
    this.drawArraysImpl(mode, first, count, instances); }
  private drawArraysImpl(mode: number, first: number, count: number, instances: number) {
    this.draws++; const gl = this.gl!; if (count <= 0) return;
    if (this.ff.ownsDraw()) { if (instances > 1) this.warn('ffinst', 'instanced draw with fixed-function vertex arrays: drawn once'); this.ff.drawArrays(mode, first, count); return; }
    if (mode === GL.QUADS) { const nq = count >> 2; const prep = this.prepareDraw(first, nq * 4, null); if (!prep.ok) return; const ibo = this.quadIndices(nq); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ibo);
      if (first === 0) { if (instances > 1) gl.drawElementsInstanced(gl.TRIANGLES, nq * 6, gl.UNSIGNED_INT, 0, instances); else gl.drawElements(gl.TRIANGLES, nq * 6, gl.UNSIGNED_INT, 0); }
      else { const idx = new Uint32Array(nq * 6); for (let i = 0; i < nq; i++) { const b = first + i * 4; idx[i * 6] = b; idx[i * 6 + 1] = b + 1; idx[i * 6 + 2] = b + 2; idx[i * 6 + 3] = b; idx[i * 6 + 4] = b + 2; idx[i * 6 + 5] = b + 3; } if (!this.scratchIbo) this.scratchIbo = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.scratchIbo); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STREAM_DRAW); gl.drawElements(gl.TRIANGLES, nq * 6, gl.UNSIGNED_INT, 0); }
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.bufFor(this.bound.element, GL.ELEMENT_ARRAY_BUFFER)); return; }
    if (mode === GL.QUAD_STRIP) mode = 5 /* TRIANGLE_STRIP: identical vertex order */; else if (mode === GL.POLYGON) mode = GL.TRIANGLE_FAN;
    const prep = this.prepareDraw(first, count, null); if (!prep.ok) return;
    if (instances > 1) gl.drawArraysInstanced(mode, first, count, instances); else gl.drawArrays(mode, first, count); }
  private drawElements(mode: number, count: number, type: number, indices: bigint, instances: number, baseVertex: number) {
    if (this.digestLeft > 0 && count > 0) { const d = this.digestBegin('glDrawElements', mode, count, type, indices, 0); try { this.drawElementsImpl(mode, count, type, indices, instances, baseVertex); } finally { this.digestEnd(d); } return; }
    this.drawElementsImpl(mode, count, type, indices, instances, baseVertex); }
  private drawElementsImpl(mode: number, count: number, type: number, indices: bigint, instances: number, baseVertex: number) {
    this.draws++;
    const gl = this.gl!; if (count <= 0) return;
    if (this.ff.ownsDraw() && !baseVertex) { this.ff.drawElements(mode, count, type, indices); return; }
    if (baseVertex) { this.setErr(GL.INVALID_OPERATION); this.warn('basevertex', 'glDrawElementsBaseVertex with a non-zero base vertex: not available in WebGL2 → GL_INVALID_OPERATION'); return; }
    if (mode === GL.QUADS) {   // expand the quad index list into triangles (index data read back from the element buffer when needed)
      const bytes = count * typeBytes(type); let src: Uint8Array | null;
      if (this.bound.element) src = this.elementData(Number(indices), bytes); else src = this.u8(indices, bytes);
      if (!src) return; const raw = type === GL.UNSIGNED_INT ? new Uint32Array(src.buffer, src.byteOffset, count) : type === GL.UNSIGNED_SHORT ? new Uint16Array(src.buffer, src.byteOffset, count) : src;
      const nq = count >> 2; const idx = new Uint32Array(nq * 6); for (let i = 0; i < nq; i++) { idx[i * 6] = raw[i * 4]; idx[i * 6 + 1] = raw[i * 4 + 1]; idx[i * 6 + 2] = raw[i * 4 + 2]; idx[i * 6 + 3] = raw[i * 4]; idx[i * 6 + 4] = raw[i * 4 + 2]; idx[i * 6 + 5] = raw[i * 4 + 3]; }
      const prep = this.prepareDraw(0, 0, { type: GL.UNSIGNED_INT, ptr: 0n, count: 0 }); void prep;
      if (!this.scratchIbo) this.scratchIbo = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.scratchIbo); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STREAM_DRAW);
      const attrs = this.curAttribs(); if (attrs.some((a) => a.enabled && !a.buffer)) { let maxIndex = 0; for (let i = 0; i < idx.length; i++) if (idx[i] > maxIndex) maxIndex = idx[i]; this.prepareDraw(0, maxIndex + 1, null); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.scratchIbo); }
      gl.drawElements(gl.TRIANGLES, nq * 6, gl.UNSIGNED_INT, 0); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.bufFor(this.bound.element, GL.ELEMENT_ARRAY_BUFFER)); return; }
    if (mode === GL.QUAD_STRIP) mode = 5; else if (mode === GL.POLYGON) mode = GL.TRIANGLE_FAN;
    const prep = this.prepareDraw(0, 0, { type, ptr: indices, count }); if (!prep.ok) { this.finishDraw(true); return; }
    if (instances > 1) gl.drawElementsInstanced(mode, count, type, prep.indexOffset, instances); else gl.drawElements(mode, count, type, prep.indexOffset);
    this.finishDraw(true); }
  private readPixels(x: number, y: number, w: number, h: number, format: number, type: number, dst: bigint) {
    const gl = this.gl!;
    if (this.bound.pack) { gl.readPixels(x, y, w, h, format === GL.BGRA ? GL.RGBA : format, type, Number(dst)); return; }
    const bpp = pixelBytes(format, type); if (!bpp) { this.setErr(GL.INVALID_ENUM); return; }
    const rowPixels = this.packRowLength || w; const rowBytes = Math.ceil(rowPixels * bpp / this.packAlign) * this.packAlign; const total = rowBytes * h;
    if (format === GL.BGRA || format === GL.BGR) {
      const tmp = new Uint8Array(w * h * 4); gl.pixelStorei(gl.PACK_ALIGNMENT, 1); gl.pixelStorei(gl.PACK_ROW_LENGTH, 0); gl.readPixels(x, y, w, h, gl.RGBA, gl.UNSIGNED_BYTE, tmp); gl.pixelStorei(gl.PACK_ALIGNMENT, this.packAlign); gl.pixelStorei(gl.PACK_ROW_LENGTH, this.packRowLength);
      const out = this.u8(dst, total, true); if (!out) return; const comps = format === GL.BGRA ? 4 : 3;
      for (let yy = 0; yy < h; yy++) for (let xx = 0; xx < w; xx++) { const s = (yy * w + xx) * 4, o = yy * rowBytes + xx * comps; out[o] = tmp[s + 2]; out[o + 1] = tmp[s + 1]; out[o + 2] = tmp[s]; if (comps === 4) out[o + 3] = tmp[s + 3]; } return; }
    const v = this.view(dst, total, type, true); if (!v) return; gl.readPixels(x, y, w, h, format, type, v);
    if (v.buffer !== this.mem!.buffer()) { const out = this.u8(dst, total, true); if (out) out.set(new Uint8Array(v.buffer, 0, total)); } }
  /** glGetTexImage: read a 2D texture level back through a temporary framebuffer (RGBA/UNSIGNED_BYTE or FLOAT) */
  private getTexImage(target: number, level: number, format: number, type: number, dst: bigint) {
    const gl = this.gl!; const id = this.curTex(target); const t = this.tex.get(id); const l = this.texInfo.get(id)?.levels.get(level);
    if (!t || !l || (target !== GL.TEXTURE_2D && !(target >= GL.TEXTURE_CUBE_MAP_POSITIVE_X && target < GL.TEXTURE_CUBE_MAP_POSITIVE_X + 6))) { this.setErr(GL.INVALID_OPERATION); this.warn('gettex', 'glGetTexImage: only 2D / cube-face colour textures can be read back (via a temporary framebuffer) in WebGL2'); return; }
    if (this.bound.pack) { this.setErr(GL.INVALID_OPERATION); return; }
    const fb = gl.createFramebuffer(); const prevRead = this.bound.fbRead; gl.bindFramebuffer(gl.READ_FRAMEBUFFER, fb); gl.framebufferTexture2D(gl.READ_FRAMEBUFFER, gl.COLOR_ATTACHMENT0, target, t, level);
    if (gl.checkFramebufferStatus(gl.READ_FRAMEBUFFER) === GL.FRAMEBUFFER_COMPLETE) { const bpp = pixelBytes(format, type); const rowBytes = Math.ceil(l.w * bpp / this.packAlign) * this.packAlign; const saveRow = this.packRowLength; if (saveRow) gl.pixelStorei(gl.PACK_ROW_LENGTH, 0); this.readPixels(0, 0, l.w, l.h, format, type, dst); if (saveRow) gl.pixelStorei(gl.PACK_ROW_LENGTH, saveRow); void rowBytes; }
    else { this.setErr(GL.INVALID_OPERATION); this.warn('gettexfmt', 'glGetTexImage: this texture format cannot be attached to a framebuffer for readback'); }
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, prevRead ? this.fbo.get(prevRead) ?? null : null); gl.deleteFramebuffer(fb); }
}
