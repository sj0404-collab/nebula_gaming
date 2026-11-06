// WebGL presenter for the emulated window's BGRA framebuffer (zero conversion: the swizzle happens in the fragment shader).
export class FramePresenter {
  readonly gl: WebGL2RenderingContext | WebGLRenderingContext;
  readonly api: 'webgl2' | 'webgl';
  readonly renderer: string;
  private tex: WebGLTexture; private prog: WebGLProgram; private quad: WebGLBuffer; private uScale: WebGLUniformLocation | null;
  private glTex: WebGLTexture; private uSize: WebGLUniformLocation | null; private uGlRect: WebGLUniformLocation | null; private uHasGl: WebGLUniformLocation | null; private uGl: WebGLUniformLocation | null;
  private glRect: [number, number, number, number] | null = null;
  private texW = 0; private texH = 0;
  frames = 0;

  constructor(readonly canvas: HTMLCanvasElement) {
    const opts: WebGLContextAttributes = { alpha: false, antialias: false, preserveDrawingBuffer: true, powerPreference: 'high-performance' };
    let api: 'webgl2' | 'webgl' = 'webgl2';
    let gl = canvas.getContext('webgl2', opts) as WebGL2RenderingContext | null;
    if (!gl) { api = 'webgl'; gl = canvas.getContext('webgl', opts) as WebGL2RenderingContext | null; }
    if (!gl) throw new Error('WebGL is not available');
    this.gl = gl; this.api = api;
    const dbg = gl.getExtension('WEBGL_debug_renderer_info');
    this.renderer = String(dbg ? gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER));
    const vs = `attribute vec2 aPos; uniform vec2 uScale; varying vec2 vUv; void main(){ vUv = vec2(aPos.x * 0.5 + 0.5, 0.5 - aPos.y * 0.5); gl_Position = vec4(aPos * uScale, 0.0, 1.0); }`;
    // screen pixels equal to GL_KEY_COLOR (BGRA 01 FE 01 FD → sampled r=FD g=01 b=FE a=01) show the live WebGL surface (uGl) placed at uGlRect (screen pixels)
    const fs = `#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D uTex; uniform sampler2D uGl; uniform vec2 uSize; uniform vec4 uGlRect; uniform float uHasGl; varying vec2 vUv;
void main(){
  vec4 c = texture2D(uTex, vUv);
  if (uHasGl > 0.5) {
    vec2 px = vUv * uSize; vec4 k = texture2D(uTex, (floor(px) + 0.5) / uSize);
    if (all(lessThan(abs(k - vec4(253.0, 1.0, 254.0, 1.0) / 255.0), vec4(0.003)))) {
      vec2 g = (px - uGlRect.xy) / uGlRect.zw;
      if (g.x >= 0.0 && g.y >= 0.0 && g.x < 1.0 && g.y < 1.0) { gl_FragColor = vec4(texture2D(uGl, g).rgb, 1.0); return; }
    }
  }
  gl_FragColor = vec4(c.b, c.g, c.r, 1.0);
}`;
    const sh = (t: number, s: string) => { const o = gl!.createShader(t)!; gl!.shaderSource(o, s); gl!.compileShader(o); if (!gl!.getShaderParameter(o, gl!.COMPILE_STATUS)) throw new Error(gl!.getShaderInfoLog(o) || 'shader'); return o; };
    this.prog = gl.createProgram()!; gl.attachShader(this.prog, sh(gl.VERTEX_SHADER, vs)); gl.attachShader(this.prog, sh(gl.FRAGMENT_SHADER, fs)); gl.linkProgram(this.prog);
    this.quad = gl.createBuffer()!; gl.bindBuffer(gl.ARRAY_BUFFER, this.quad);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1]), gl.STATIC_DRAW);
    this.tex = gl.createTexture()!; gl.bindTexture(gl.TEXTURE_2D, this.tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    this.uScale = gl.getUniformLocation(this.prog, 'uScale'); this.uSize = gl.getUniformLocation(this.prog, 'uSize'); this.uGlRect = gl.getUniformLocation(this.prog, 'uGlRect'); this.uHasGl = gl.getUniformLocation(this.prog, 'uHasGl'); this.uGl = gl.getUniformLocation(this.prog, 'uGl');
    this.glTex = gl.createTexture()!; gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, this.glTex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.activeTexture(gl.TEXTURE0);
    gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT);
  }

  /** upload a BGRA framebuffer (a view straight into wasm64 memory) and draw it aspect-fitted */
  present(px: Uint8Array, w: number, h: number) {
    const gl = this.gl; const c = this.canvas;
    const cw = c.clientWidth || w, ch = c.clientHeight || h; const dpr = Math.min(2, window.devicePixelRatio || 1);
    const pw = Math.max(1, Math.round(cw * dpr)), ph = Math.max(1, Math.round(ch * dpr));
    if (c.width !== pw || c.height !== ph) { c.width = pw; c.height = ph; }
    gl.viewport(0, 0, pw, ph);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, this.tex);
    if (w !== this.texW || h !== this.texH) { gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, px); this.texW = w; this.texH = h; }
    else gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
    const s = Math.min(pw / w, ph / h); const sx = (w * s) / pw, sy = (h * s) / ph;
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.useProgram(this.prog); gl.uniform2f(this.uScale, sx, sy); gl.uniform2f(this.uSize, w, h); gl.uniform1i(this.uGl, 1);
    const r = this.glRect; gl.uniform1f(this.uHasGl, r ? 1 : 0); gl.uniform4f(this.uGlRect, r ? r[0] : 0, r ? r[1] : 0, r ? r[2] : 1, r ? r[3] : 1);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, this.glTex); gl.activeTexture(gl.TEXTURE0);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.quad); const loc = gl.getAttribLocation(this.prog, 'aPos'); gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
    gl.drawArrays(gl.TRIANGLES, 0, 6);
    this.frames++;
  }

  /** SwapBuffers: copy the program's WebGL surface into a texture on the GPU (no readPixels) for composition at screen rect (x,y,w,h); false = fall back to the CPU path */
  setGl(src: OffscreenCanvas | HTMLCanvasElement, x: number, y: number, w: number, h: number): boolean {
    const gl = this.gl; if (gl.isContextLost()) return false;
    try {
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, this.glTex);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false); gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, src as TexImageSource);
      gl.activeTexture(gl.TEXTURE0);
      if (gl.getError()) { this.glRect = null; return false; }
      this.glRect = [x, y, w, h]; return true;
    } catch { gl.activeTexture(gl.TEXTURE0); this.glRect = null; return false; }
  }

  /** canvas css pixel → guest client coordinates (null when outside the image) */
  toGuest(cx: number, cy: number, w: number, h: number): { x: number; y: number } | null {
    const c = this.canvas; const cw = c.clientWidth, ch = c.clientHeight; if (!w || !h || !cw || !ch) return null;
    const s = Math.min(cw / w, ch / h); const ox = (cw - w * s) / 2, oy = (ch - h * s) / 2;
    const x = (cx - ox) / s, y = (cy - oy) / s; if (x < 0 || y < 0 || x >= w || y >= h) return null; return { x, y };
  }
}
