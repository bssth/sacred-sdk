using OpenTK.Graphics.OpenGL4;
using SacredStudio.Game;

namespace SacredStudio;

/// <summary>
/// Native OpenGL map renderer. Terrain + FLOOR overlays come straight from the game
/// files via <see cref="GameAssets"/> (tiles.pak / texture.pak / sectors.* / Floor.pak
/// decoded in C#, no Python, no pre-baked assets). Sprites are per-group billboards
/// (still per-group PNGs for now). Camera mirrors IsoMapView pan/zoom so it aligns 1:1
/// with the WPF marker overlay.
///
/// Passes: (1) terrain diamonds — source sheets streamed by <see cref="GlTextureStreamer"/>
/// (async decode, LRU); (1b) floor overlays — primary RGB + secondary alpha mask;
/// (2) sprite billboards — depth-tested. A sector's tiles are grouped by sheet → one
/// draw per (sector, sheet); floors per (primarySheet, secondarySheet).
/// </summary>
public sealed class GlTerrainRenderer
{
    private readonly GameAssets _assets = new();
    public bool Available => _assets.Available;

    /// <summary>Sprite pass (trees/buildings).</summary>
    public bool DrawSprites = true;
    /// <summary>Floor-overlay pass (soft grass→dirt transitions).</summary>
    public bool DrawFloor = true;
    /// <summary>Liquid pass (water/lava surfaces).</summary>
    public bool DrawLiquid = true;
    private const int FloorBuildsPerFrame = 2;

    /// <summary>Resident terrain sheet textures (for diagnostics/HUD).</summary>
    public int ResidentSheets => _sheetStream.Resident;

    public double Zoom = 0.5, PanX, PanY, ViewW = 1, ViewH = 1;

    private int _program, _uPan, _uZoom, _uView, _uTex;
    private bool _ready;

    // Terrain source sheets (texture.pak entries), streamed + LRU.
    private readonly GlTextureStreamer _sheetStream;
    // Sprites: per-group PNGs, streamed by group_id.
    private readonly GlTextureStreamer _spriteStream;

    private readonly Dictionary<uint, GameAssets.SectorDecode?> _sectorDecode = new();
    private readonly Dictionary<uint, Mesh> _terrainMesh = new();
    private readonly Dictionary<uint, Mesh> _spriteMesh = new();
    private readonly Dictionary<uint, FloorMesh> _floorMesh = new();
    private readonly Dictionary<uint, Mesh> _liquidMesh = new();

    // Per-sector bake: below BakeZoomMax we render a sector's terrain+floor+liquid into
    // one texture (FBO) once and draw it as a single quad (the engine's approach) — kills
    // the per-tile draw-call/geometry cost at wide zoom. At/above BakeZoomMax we draw
    // per-tile (crisp near). Sprites stay billboards in both modes.
    private const float BakeZoomMax = 0.40f;   // ≥ → per-tile; < → baked
    private const float BakeScale = 0.40f;     // bake render scale (≈1:1 at the switch)
    private const int BakesPerFrame = 1;
    private const int BakeCap = 56;
    private int _fbo, _bakeQuadVao, _bakeQuadVbo;
    private int _wpfFbo;   // GLWpfControl's own framebuffer (its render target), NOT 0
    private long _frame;
    private sealed class Baked { public int Tex; public float IsoX, IsoY, IsoW, IsoH; public long Used; }
    private readonly Dictionary<uint, Baked> _baked = new();

    // Diamond corner UVs (px in a 100×50 tile), ordered to match pos[]: [left,top,right,bottom].
    private static readonly (float x, float y)[] DiamondUv =
        { (2.512f, 24.012f), (50.512f, 1.012f), (98.012f, 23.5f), (50.0f, 48.512f) };
    private const float HW = 48.2f, HH = 24.2f;
    private const float DepthScale = 1f / 305000f;

    private sealed class Mesh { public readonly List<(int key, int vao, int vbo, int count)> B = new(); }
    private sealed class FloorMesh { public readonly List<(int pSheet, int sSheet, int vao, int vbo, int count)> B = new(); }

    public GlTerrainRenderer()
    {
        var filter = Environment.GetEnvironmentVariable("SS_TFILTER") == "nearest"
            ? TextureMinFilter.Nearest : TextureMinFilter.Linear;
        _sheetStream = new GlTextureStreamer(1024, filter, _assets.DecodeSheet);
        _spriteStream = new GlTextureStreamer(2048, TextureMinFilter.Nearest,
            g => _assets.Sprites is { } sp ? sp.Compose(g) : (null, 0, 0));
    }

    private const string VertSrc = @"#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUv;
layout(location=2) in vec4 aCol;
uniform vec2 uPan; uniform float uZoom; uniform vec2 uView;
out vec2 vUv; out vec4 vCol;
void main(){
  vUv = aUv; vCol = aCol;
  vec2 s = aPos.xy*uZoom + uPan;
  vec2 ndc = vec2(s.x/uView.x*2.0-1.0, -(s.y/uView.y*2.0-1.0));
  gl_Position = vec4(ndc, aPos.z, 1.0);
}";

    private const string FragSrc = @"#version 330 core
in vec2 vUv; in vec4 vCol; out vec4 o;
uniform sampler2D uTex;
void main(){ vec4 t = texture(uTex, vUv); o = t*vCol; if(o.a < 0.35) discard; }";

    private int _floorProgram, _fPan, _fZoom, _fView, _fPrim, _fSec;

    private const string FloorVertSrc = @"#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUvP;
layout(location=2) in vec2 aUvS;
uniform vec2 uPan; uniform float uZoom; uniform vec2 uView;
out vec2 vUvP; out vec2 vUvS;
void main(){
  vUvP = aUvP; vUvS = aUvS;
  vec2 s = aPos.xy*uZoom + uPan;
  vec2 ndc = vec2(s.x/uView.x*2.0-1.0, -(s.y/uView.y*2.0-1.0));
  gl_Position = vec4(ndc, aPos.z, 1.0);
}";

    private const string FloorFragSrc = @"#version 330 core
in vec2 vUvP; in vec2 vUvS; out vec4 o;
uniform sampler2D uPrimary; uniform sampler2D uSecondary;
void main(){
  float a = texture(uSecondary, vUvS).a;
  if(a < 0.01) discard;
  o = vec4(texture(uPrimary, vUvP).rgb, a);
}";

    // Liquid: tiling water/lava texture × per-corner alpha (vCol.a), smooth blend.
    private int _liquidProgram, _lPan, _lZoom, _lView, _lTex;
    private const string LiquidFragSrc = @"#version 330 core
in vec2 vUv; in vec4 vCol; out vec4 o;
uniform sampler2D uTex;
void main(){ vec4 t = texture(uTex, vUv); o = vec4(t.rgb, t.a*vCol.a); if(o.a < 0.01) discard; }";

    // Baked-sector quad: a complete sector image → output opaque, discard only the
    // truly transparent diamond-border texels (don't apply the terrain's 0.35 cutoff).
    private int _quadProgram, _qPan, _qZoom, _qView, _qTex;
    private const string QuadFragSrc = @"#version 330 core
in vec2 vUv; in vec4 vCol; out vec4 o;
uniform sampler2D uTex;
void main(){ vec4 t = texture(uTex, vUv); if(t.a < 0.02) discard; o = vec4(t.rgb, 1.0); }";

    public void Ready()
    {
        GL.ClearColor(0.07f, 0.08f, 0.11f, 1f);
        GL.Enable(EnableCap.Blend);
        GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);
        _program = BuildProgram(VertSrc, FragSrc);
        _uPan = GL.GetUniformLocation(_program, "uPan");
        _uZoom = GL.GetUniformLocation(_program, "uZoom");
        _uView = GL.GetUniformLocation(_program, "uView");
        _uTex = GL.GetUniformLocation(_program, "uTex");

        _floorProgram = BuildProgram(FloorVertSrc, FloorFragSrc);
        _fPan = GL.GetUniformLocation(_floorProgram, "uPan");
        _fZoom = GL.GetUniformLocation(_floorProgram, "uZoom");
        _fView = GL.GetUniformLocation(_floorProgram, "uView");
        _fPrim = GL.GetUniformLocation(_floorProgram, "uPrimary");
        _fSec = GL.GetUniformLocation(_floorProgram, "uSecondary");

        _liquidProgram = BuildProgram(VertSrc, LiquidFragSrc);
        _lPan = GL.GetUniformLocation(_liquidProgram, "uPan");
        _lZoom = GL.GetUniformLocation(_liquidProgram, "uZoom");
        _lView = GL.GetUniformLocation(_liquidProgram, "uView");
        _lTex = GL.GetUniformLocation(_liquidProgram, "uTex");

        _quadProgram = BuildProgram(VertSrc, QuadFragSrc);
        _qPan = GL.GetUniformLocation(_quadProgram, "uPan");
        _qZoom = GL.GetUniformLocation(_quadProgram, "uZoom");
        _qView = GL.GetUniformLocation(_quadProgram, "uView");
        _qTex = GL.GetUniformLocation(_quadProgram, "uTex");

        // FBO + a reusable dynamic quad (9-float pos/uv/col) for drawing baked sectors.
        _fbo = GL.GenFramebuffer();
        _bakeQuadVao = GL.GenVertexArray(); GL.BindVertexArray(_bakeQuadVao);
        _bakeQuadVbo = GL.GenBuffer(); GL.BindBuffer(BufferTarget.ArrayBuffer, _bakeQuadVbo);
        GL.BufferData(BufferTarget.ArrayBuffer, 6 * 9 * 4, IntPtr.Zero, BufferUsageHint.DynamicDraw);
        GL.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, 9 * 4, 0); GL.EnableVertexAttribArray(0);
        GL.VertexAttribPointer(1, 2, VertexAttribPointerType.Float, false, 9 * 4, 3 * 4); GL.EnableVertexAttribArray(1);
        GL.VertexAttribPointer(2, 4, VertexAttribPointerType.Float, false, 9 * 4, 5 * 4); GL.EnableVertexAttribArray(2);
        GL.BindVertexArray(0);
        _ready = true;
    }

    public void Render()
    {
        // GLWpfControl renders into its OWN framebuffer (it presents that to WPF) — it
        // is bound on entry. Remember it so the per-sector bake can restore it after
        // switching to the bake FBO; binding 0 would draw into the invisible default FB.
        GL.GetInteger(GetPName.FramebufferBinding, out _wpfFbo);
        GL.Viewport(0, 0, (int)ViewW, (int)ViewH);
        GL.Clear(ClearBufferMask.ColorBufferBit | ClearBufferMask.DepthBufferBit);
        if (!_ready || !_assets.Available) return;

        GL.UseProgram(_program);
        GL.Uniform2(_uPan, (float)PanX, (float)PanY);
        GL.Uniform1(_uZoom, (float)Zoom);
        GL.Uniform2(_uView, (float)ViewW, (float)ViewH);
        GL.Uniform1(_uTex, 0);
        GL.ActiveTexture(TextureUnit.Texture0);

        var vis = VisibleSectors();

        // pass 1: terrain per-tile (near) OR baked sector quads (wide zoom)
        GL.Disable(EnableCap.DepthTest);
        bool baked = Zoom < BakeZoomMax;
        _frame++;
        _sheetStream.BeginFrame();
        if (baked) DrawBakedSectors(vis);
        if (!baked)
        foreach (var s in vis)
        {
            var m = GetTerrainMesh(s);
            foreach (var b in m.B)
            {
                int tex = _sheetStream.Get(b.key);
                if (tex == 0) continue;
                GL.BindTexture(TextureTarget.Texture2D, tex);
                GL.BindVertexArray(b.vao);
                GL.DrawArrays(PrimitiveType.Triangles, 0, b.count);
            }
        }

        // pass 1b: floor overlays (primary RGB + secondary alpha mask)
        if (!baked && DrawFloor)
        {
            GL.UseProgram(_floorProgram);
            GL.Uniform2(_fPan, (float)PanX, (float)PanY);
            GL.Uniform1(_fZoom, (float)Zoom);
            GL.Uniform2(_fView, (float)ViewW, (float)ViewH);
            GL.Uniform1(_fPrim, 0);
            GL.Uniform1(_fSec, 1);
            int floorBuilt = 0;
            foreach (var s in vis)
            {
                if (!_floorMesh.TryGetValue(s.Sid, out var fm))
                {
                    if (floorBuilt >= FloorBuildsPerFrame) continue;
                    fm = GetFloorMesh(s);
                    floorBuilt++;
                }
                foreach (var b in fm.B)
                {
                    int tp = _sheetStream.Get(b.pSheet);
                    int ts = _sheetStream.Get(b.sSheet);
                    if (tp == 0 || ts == 0) continue;
                    GL.ActiveTexture(TextureUnit.Texture1); GL.BindTexture(TextureTarget.Texture2D, ts);
                    GL.ActiveTexture(TextureUnit.Texture0); GL.BindTexture(TextureTarget.Texture2D, tp);
                    GL.BindVertexArray(b.vao);
                    GL.DrawArrays(PrimitiveType.Triangles, 0, b.count);
                }
            }
        }

        // pass 1c: liquids (water/lava) — tiling texture × per-corner alpha, over ground+floor
        if (!baked && DrawLiquid)
        {
            GL.UseProgram(_liquidProgram);
            GL.Uniform2(_lPan, (float)PanX, (float)PanY);
            GL.Uniform1(_lZoom, (float)Zoom);
            GL.Uniform2(_lView, (float)ViewW, (float)ViewH);
            GL.Uniform1(_lTex, 0);
            GL.ActiveTexture(TextureUnit.Texture0);
            foreach (var s in vis)
            {
                var m = GetLiquidMesh(s);
                foreach (var b in m.B)
                {
                    int tex = _sheetStream.Get(b.key);
                    if (tex == 0) continue;
                    GL.BindTexture(TextureTarget.Texture2D, tex);
                    GL.BindVertexArray(b.vao);
                    GL.DrawArrays(PrimitiveType.Triangles, 0, b.count);
                }
            }
        }
        _sheetStream.EndFrame();

        // pass 2: sprites (depth-tested billboards)
        if (DrawSprites && _assets.Sprites is { Ready: true })
        {
            GL.UseProgram(_program);
            GL.Uniform2(_uPan, (float)PanX, (float)PanY);
            GL.Uniform1(_uZoom, (float)Zoom);
            GL.Uniform2(_uView, (float)ViewW, (float)ViewH);
            GL.Uniform1(_uTex, 0);
            GL.ActiveTexture(TextureUnit.Texture0);
            GL.Enable(EnableCap.DepthTest);
            GL.DepthFunc(DepthFunction.Less);
            GL.DepthMask(true);
            _spriteStream.BeginFrame();
            foreach (var s in vis)
            {
                var m = GetSpriteMesh(s);
                foreach (var b in m.B)
                {
                    int tex = _spriteStream.Get(b.key);
                    if (tex == 0) continue;
                    GL.BindTexture(TextureTarget.Texture2D, tex);
                    GL.BindVertexArray(b.vao);
                    GL.DrawArrays(PrimitiveType.Triangles, 0, b.count);
                }
            }
            _spriteStream.EndFrame();
            GL.Disable(EnableCap.DepthTest);
        }
        GL.BindVertexArray(0);
    }

    private List<GameAssets.SectorInfo> VisibleSectors()
    {
        var list = new List<GameAssets.SectorInfo>();
        double ix0 = (0 - PanX) / Zoom, iy0 = (0 - PanY) / Zoom;
        double ix1 = (ViewW - PanX) / Zoom, iy1 = (ViewH - PanY) / Zoom;
        double gxMin = double.MaxValue, gxMax = double.MinValue, gyMin = double.MaxValue, gyMax = double.MinValue;
        foreach (var (ix, iy) in new[] { (ix0, iy0), (ix1, iy0), (ix0, iy1), (ix1, iy1) })
        {
            double a = ix / 3072.0, b = iy / 1536.0;
            double gx = (a + b) / 2, gy = (b - a) / 2;
            gxMin = Math.Min(gxMin, gx); gxMax = Math.Max(gxMax, gx);
            gyMin = Math.Min(gyMin, gy); gyMax = Math.Max(gyMax, gy);
        }
        int gx0 = (int)Math.Floor(gxMin) - 1, gx1 = (int)Math.Ceiling(gxMax) + 1;
        int gy0 = (int)Math.Floor(gyMin) - 1, gy1 = (int)Math.Ceiling(gyMax) + 1;
        if ((long)(gx1 - gx0) * (gy1 - gy0) > 4000) return list;
        for (int gy = gy0; gy <= gy1; gy++)
        for (int gx = gx0; gx <= gx1; gx++)
            if (_assets.TryGetSectorByGrid(gx, gy, out var s)) list.Add(s);
        list.Sort((p, q) => (p.Ox + p.Oy).CompareTo(q.Ox + q.Oy));
        return list;
    }

    // ---- per-sector bake (wide-zoom path) ----
    private void DrawBakedSectors(List<GameAssets.SectorInfo> vis)
    {
        int built = 0;
        foreach (var s in vis)
        {
            if (!_baked.TryGetValue(s.Sid, out var bk))
            {
                if (built >= BakesPerFrame) continue;
                bk = TryBakeSector(s);
                if (bk is null) continue;
                _baked[s.Sid] = bk; built++;
            }
            bk.Used = _frame;
            DrawBakedQuad(bk);
        }
        EvictBakes();
    }

    private Baked? TryBakeSector(GameAssets.SectorInfo s)
    {
        var tm = GetTerrainMesh(s);
        var fm = GetFloorMesh(s);
        var lm = GetLiquidMesh(s);

        // every referenced sheet must be resident first (else bake would be partial)
        bool ready = true;
        foreach (var b in tm.B) if (_sheetStream.Get(b.key) == 0) ready = false;
        foreach (var b in fm.B) { if (_sheetStream.Get(b.pSheet) == 0) ready = false; if (_sheetStream.Get(b.sSheet) == 0) ready = false; }
        foreach (var b in lm.B) if (_sheetStream.Get(b.key) == 0) ready = false;
        if (!ready) return null;

        SectorIsoBounds(s, out float minX, out float minY, out float maxX, out float maxY);
        int bw = Math.Clamp((int)MathF.Ceiling((maxX - minX) * BakeScale), 1, 4096);
        int bh = Math.Clamp((int)MathF.Ceiling((maxY - minY) * BakeScale), 1, 4096);

        int tex = GL.GenTexture();
        GL.BindTexture(TextureTarget.Texture2D, tex);
        GL.TexImage2D(TextureTarget.Texture2D, 0, PixelInternalFormat.Rgba, bw, bh, 0,
            OpenTK.Graphics.OpenGL4.PixelFormat.Rgba, PixelType.UnsignedByte, IntPtr.Zero);
        GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureMinFilter, (int)TextureMinFilter.Linear);
        GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureMagFilter, (int)TextureMagFilter.Linear);
        GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureWrapS, (int)TextureWrapMode.ClampToEdge);
        GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureWrapT, (int)TextureWrapMode.ClampToEdge);

        GL.BindFramebuffer(FramebufferTarget.Framebuffer, _fbo);
        GL.FramebufferTexture2D(FramebufferTarget.Framebuffer, FramebufferAttachment.ColorAttachment0, TextureTarget.Texture2D, tex, 0);
        if (GL.CheckFramebufferStatus(FramebufferTarget.Framebuffer) != FramebufferErrorCode.FramebufferComplete)
        {
            GL.BindFramebuffer(FramebufferTarget.Framebuffer, 0);
            GL.DeleteTexture(tex);
            return null;
        }
        GL.Viewport(0, 0, bw, bh);
        GL.ClearColor(0f, 0f, 0f, 0f);
        GL.Clear(ClearBufferMask.ColorBufferBit);

        float px = -minX * BakeScale, py = -minY * BakeScale;

        // terrain: blend OFF so the ground fills the FBO opaque (correct rgb, a=1) —
        // avoids the premultiply-darkening + low-alpha-discard that would blank the quad.
        GL.Disable(EnableCap.Blend);
        GL.UseProgram(_program);
        GL.Uniform2(_uPan, px, py); GL.Uniform1(_uZoom, BakeScale); GL.Uniform2(_uView, (float)bw, (float)bh); GL.Uniform1(_uTex, 0);
        GL.ActiveTexture(TextureUnit.Texture0);
        foreach (var b in tm.B)
        {
            GL.BindTexture(TextureTarget.Texture2D, _sheetStream.Get(b.key));
            GL.BindVertexArray(b.vao); GL.DrawArrays(PrimitiveType.Triangles, 0, b.count);
        }
        GL.Enable(EnableCap.Blend);

        GL.UseProgram(_floorProgram);
        GL.Uniform2(_fPan, px, py); GL.Uniform1(_fZoom, BakeScale); GL.Uniform2(_fView, (float)bw, (float)bh);
        GL.Uniform1(_fPrim, 0); GL.Uniform1(_fSec, 1);
        foreach (var b in fm.B)
        {
            GL.ActiveTexture(TextureUnit.Texture1); GL.BindTexture(TextureTarget.Texture2D, _sheetStream.Get(b.sSheet));
            GL.ActiveTexture(TextureUnit.Texture0); GL.BindTexture(TextureTarget.Texture2D, _sheetStream.Get(b.pSheet));
            GL.BindVertexArray(b.vao); GL.DrawArrays(PrimitiveType.Triangles, 0, b.count);
        }

        GL.UseProgram(_liquidProgram);
        GL.Uniform2(_lPan, px, py); GL.Uniform1(_lZoom, BakeScale); GL.Uniform2(_lView, (float)bw, (float)bh); GL.Uniform1(_lTex, 0);
        GL.ActiveTexture(TextureUnit.Texture0);
        foreach (var b in lm.B)
        {
            GL.BindTexture(TextureTarget.Texture2D, _sheetStream.Get(b.key));
            GL.BindVertexArray(b.vao); GL.DrawArrays(PrimitiveType.Triangles, 0, b.count);
        }

        GL.BindFramebuffer(FramebufferTarget.Framebuffer, _wpfFbo);
        return new Baked { Tex = tex, IsoX = minX, IsoY = minY, IsoW = maxX - minX, IsoH = maxY - minY, Used = _frame };
    }

    private static void SectorIsoBounds(GameAssets.SectorInfo s, out float minX, out float minY, out float maxX, out float maxY)
    {
        // tile (lx,ly): cx = ((Ox+lx)-(Oy+ly))*48 + 48 ; cy = ((Ox+lx)+(Oy+ly))*24 + 24
        int ox = s.Ox, oy = s.Oy;
        float cxMin = (ox - (oy + 63)) * 48f + 48f;   // lx=0, ly=63
        float cxMax = ((ox + 63) - oy) * 48f + 48f;   // lx=63, ly=0
        float cyMin = (ox + oy) * 24f + 24f;          // lx=ly=0
        float cyMax = ((ox + 63) + (oy + 63)) * 24f + 24f;
        minX = cxMin - 49f; maxX = cxMax + 51f;       // ±48 diamond half + liquid +2 + margin
        minY = cyMin - 25f; maxY = cyMax + 27f;
    }

    private void DrawBakedQuad(Baked bk)
    {
        GL.BindFramebuffer(FramebufferTarget.Framebuffer, _wpfFbo);
        GL.Viewport(0, 0, (int)ViewW, (int)ViewH);
        GL.UseProgram(_quadProgram);
        GL.Uniform2(_qPan, (float)PanX, (float)PanY); GL.Uniform1(_qZoom, (float)Zoom);
        GL.Uniform2(_qView, (float)ViewW, (float)ViewH); GL.Uniform1(_qTex, 0);
        GL.ActiveTexture(TextureUnit.Texture0); GL.BindTexture(TextureTarget.Texture2D, bk.Tex);
        float x0 = bk.IsoX, y0 = bk.IsoY, x1 = bk.IsoX + bk.IsoW, y1 = bk.IsoY + bk.IsoH;
        // FBO was rendered y-flipped → minY maps to v=1, maxY to v=0.
        float[] q =
        {
            x0,y0,0, 0,1, 1,1,1,1,
            x1,y0,0, 1,1, 1,1,1,1,
            x1,y1,0, 1,0, 1,1,1,1,
            x0,y0,0, 0,1, 1,1,1,1,
            x1,y1,0, 1,0, 1,1,1,1,
            x0,y1,0, 0,0, 1,1,1,1,
        };
        GL.BindVertexArray(_bakeQuadVao);
        GL.BindBuffer(BufferTarget.ArrayBuffer, _bakeQuadVbo);
        GL.BufferSubData(BufferTarget.ArrayBuffer, IntPtr.Zero, q.Length * sizeof(float), q);
        GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
    }

    private void EvictBakes()
    {
        if (_baked.Count <= BakeCap) return;
        int toRemove = _baked.Count - BakeCap;
        // remove least-recently-used not touched this frame
        while (toRemove > 0)
        {
            uint oldestKey = 0; long oldest = long.MaxValue; bool any = false;
            foreach (var kv in _baked)
                if (kv.Value.Used != _frame && kv.Value.Used < oldest) { oldest = kv.Value.Used; oldestKey = kv.Key; any = true; }
            if (!any) break;
            GL.DeleteTexture(_baked[oldestKey].Tex);
            _baked.Remove(oldestKey);
            toRemove--;
        }
    }

    private GameAssets.SectorDecode? GetSector(GameAssets.SectorInfo s)
    {
        if (_sectorDecode.TryGetValue(s.Sid, out var sd)) return sd;
        sd = _assets.DecodeSector(s);
        _sectorDecode[s.Sid] = sd;
        return sd;
    }

    // ---- terrain mesh: iso diamonds grouped by source sheet ----
    private Mesh GetTerrainMesh(GameAssets.SectorInfo s)
    {
        if (_terrainMesh.TryGetValue(s.Sid, out var m)) return m;
        m = new Mesh();
        var sd = GetSector(s);
        if (sd is not null)
        {
            var bySheet = new Dictionary<int, List<float>>();
            for (int idx = 0; idx < 4096; idx++)
            {
                long tid = sd.Ground[idx];
                if (tid == 0 || !_assets.TryTile(tid, out var tr)) continue;
                var (sx, sy) = GameAssets.SubPositions[tr.Sub];
                var (sw, sh) = _assets.SheetDim(tr.Sheet);
                float iw = 1f / sw, ih = 1f / sh;

                int lx = idx & 63, ly = idx >> 6;
                int wx = s.Ox + lx, wy = s.Oy + ly;
                float ix = (wx - wy) * 48f, iy = (wx + wy) * 24f, cx = ix + 48f, cy = iy + 24f;
                var pos = new (float x, float y)[] { (cx - HW, cy), (cx, cy - HH), (cx + HW, cy), (cx, cy + HH) };
                var uv = new (float u, float v)[4];
                for (int k = 0; k < 4; k++) uv[k] = ((sx + DiamondUv[k].x) * iw, (sy + DiamondUv[k].y) * ih);
                int to = idx * 4;
                var t = new[] { sd.Tints[to] / 255f, sd.Tints[to + 1] / 255f, sd.Tints[to + 2] / 255f, sd.Tints[to + 3] / 255f };
                float ucx = (uv[0].u + uv[1].u + uv[2].u + uv[3].u) * .25f, ucy = (uv[0].v + uv[1].v + uv[2].v + uv[3].v) * .25f;
                float tc = (t[0] + t[1] + t[2] + t[3]) * .25f;
                if (!bySheet.TryGetValue(tr.Sheet, out var buf)) { buf = new List<float>(288); bySheet[tr.Sheet] = buf; }
                int[][] tris = { new[] { 1, 2 }, new[] { 2, 3 }, new[] { 3, 0 }, new[] { 0, 1 } };
                foreach (var tri in tris)
                {
                    V(buf, cx, cy, 0, ucx, ucy, tc, tc, tc, 1);
                    V(buf, pos[tri[0]].x, pos[tri[0]].y, 0, uv[tri[0]].u, uv[tri[0]].v, t[tri[0]], t[tri[0]], t[tri[0]], 1);
                    V(buf, pos[tri[1]].x, pos[tri[1]].y, 0, uv[tri[1]].u, uv[tri[1]].v, t[tri[1]], t[tri[1]], t[tri[1]], 1);
                }
            }
            Upload(m, bySheet);
        }
        _terrainMesh[s.Sid] = m;
        return m;
    }

    // ---- floor mesh: blended diamonds (primary RGB + secondary alpha) ----
    private FloorMesh GetFloorMesh(GameAssets.SectorInfo s)
    {
        if (_floorMesh.TryGetValue(s.Sid, out var m)) return m;
        m = new FloorMesh();
        var sd = GetSector(s);
        if (sd is not null && sd.Floors.Length > 0)
        {
            var overlays = (GameAssets.FloorOverlay[])sd.Floors.Clone();
            Array.Sort(overlays, (a, b) =>
            {
                int c = ((a.LocalIdx & 63) + (a.LocalIdx >> 6)).CompareTo((b.LocalIdx & 63) + (b.LocalIdx >> 6));
                if (c != 0) return c;
                c = (a.LocalIdx >> 6).CompareTo(b.LocalIdx >> 6);
                return c != 0 ? c : a.Depth.CompareTo(b.Depth);
            });
            int[][] tris = { new[] { 1, 2 }, new[] { 2, 3 }, new[] { 3, 0 }, new[] { 0, 1 } };
            var byPair = new Dictionary<(int p, int s), List<float>>();
            foreach (var ov in overlays)
            {
                if (!_assets.TryTile(ov.Primary, out var pr)) continue;
                long secTile = ov.Secondary != 0 ? ov.Secondary : ov.Primary;
                if (!_assets.TryTile(secTile, out var sr)) continue;
                var (psx, psy) = GameAssets.SubPositions[pr.Sub];
                var (pw, ph) = _assets.SheetDim(pr.Sheet);
                var (ssx, ssy) = GameAssets.SubPositions[sr.Sub];
                var (sw, sh) = _assets.SheetDim(sr.Sheet);
                float piw = 1f / pw, pih = 1f / ph, siw = 1f / sw, sih = 1f / sh;

                int lx = ov.LocalIdx & 63, ly = ov.LocalIdx >> 6;
                int wx = s.Ox + lx, wy = s.Oy + ly;
                float ix = (wx - wy) * 48f, iy = (wx + wy) * 24f, cx = ix + 48f, cy = iy + 24f;
                var pos = new (float x, float y)[] { (cx - HW, cy), (cx, cy - HH), (cx + HW, cy), (cx, cy + HH) };
                var pUv = new (float u, float v)[4];
                var sUv = new (float u, float v)[4];
                for (int k = 0; k < 4; k++)
                {
                    pUv[k] = ((psx + DiamondUv[k].x) * piw, (psy + DiamondUv[k].y) * pih);
                    sUv[k] = ((ssx + DiamondUv[k].x) * siw, (ssy + DiamondUv[k].y) * sih);
                }
                float pcu = (pUv[0].u + pUv[1].u + pUv[2].u + pUv[3].u) * .25f, pcv = (pUv[0].v + pUv[1].v + pUv[2].v + pUv[3].v) * .25f;
                float scu = (sUv[0].u + sUv[1].u + sUv[2].u + sUv[3].u) * .25f, scv = (sUv[0].v + sUv[1].v + sUv[2].v + sUv[3].v) * .25f;
                var key = (pr.Sheet, sr.Sheet);
                if (!byPair.TryGetValue(key, out var buf)) { buf = new List<float>(84); byPair[key] = buf; }
                foreach (var tri in tris)
                {
                    Vf(buf, cx, cy, pcu, pcv, scu, scv);
                    Vf(buf, pos[tri[0]].x, pos[tri[0]].y, pUv[tri[0]].u, pUv[tri[0]].v, sUv[tri[0]].u, sUv[tri[0]].v);
                    Vf(buf, pos[tri[1]].x, pos[tri[1]].y, pUv[tri[1]].u, pUv[tri[1]].v, sUv[tri[1]].u, sUv[tri[1]].v);
                }
            }
            foreach (var kv in byPair)
            {
                float[] arr = kv.Value.ToArray();
                int vao = GL.GenVertexArray(); GL.BindVertexArray(vao);
                int vbo = GL.GenBuffer(); GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
                GL.BufferData(BufferTarget.ArrayBuffer, arr.Length * sizeof(float), arr, BufferUsageHint.StaticDraw);
                GL.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, 7 * 4, 0); GL.EnableVertexAttribArray(0);
                GL.VertexAttribPointer(1, 2, VertexAttribPointerType.Float, false, 7 * 4, 3 * 4); GL.EnableVertexAttribArray(1);
                GL.VertexAttribPointer(2, 2, VertexAttribPointerType.Float, false, 7 * 4, 5 * 4); GL.EnableVertexAttribArray(2);
                m.B.Add((kv.Key.p, kv.Key.s, vao, vbo, arr.Length / 7));
            }
            GL.BindVertexArray(0);
        }
        _floorMesh[s.Sid] = m;
        return m;
    }

    // ---- liquid mesh: diamonds, tiling texture + per-corner alpha, grouped by sheet ----
    private Mesh GetLiquidMesh(GameAssets.SectorInfo s)
    {
        if (_liquidMesh.TryGetValue(s.Sid, out var m)) return m;
        m = new Mesh();
        var sd = GetSector(s);
        if (sd is not null && sd.Liquids.Length > 0)
        {
            // diamond UVs map the whole texture across the 96×48 tile (engine: x/96, y/48)
            var uvCorner = new (float u, float v)[] { (0f, 0.5f), (0.5f, 0f), (1f, 0.5f), (0.5f, 1f) };
            int[][] tris = { new[] { 1, 2 }, new[] { 2, 3 }, new[] { 3, 0 }, new[] { 0, 1 } };
            var bySheet = new Dictionary<int, List<float>>();
            foreach (var lq in sd.Liquids)
            {
                int lx = lq.LocalIdx & 63, ly = lq.LocalIdx >> 6;
                int wx = s.Ox + lx, wy = s.Oy + ly;
                float ix = (wx - wy) * 48f, iy = (wx + wy) * 24f, cx = ix + 50f, cy = iy + 25f;  // +2,+1 engine shift
                var pos = new (float x, float y)[] { (cx - 48f, cy), (cx, cy - 24f), (cx + 48f, cy), (cx, cy + 24f) };
                var a = new[] { lq.AL / 255f, lq.AT / 255f, lq.AR / 255f, lq.AB / 255f };
                float ac = (a[0] + a[1] + a[2] + a[3]) * .25f;
                if (!bySheet.TryGetValue(lq.Sheet, out var buf)) { buf = new List<float>(108); bySheet[lq.Sheet] = buf; }
                foreach (var tri in tris)
                {
                    V(buf, cx, cy, 0, 0.5f, 0.5f, 1, 1, 1, ac);
                    V(buf, pos[tri[0]].x, pos[tri[0]].y, 0, uvCorner[tri[0]].u, uvCorner[tri[0]].v, 1, 1, 1, a[tri[0]]);
                    V(buf, pos[tri[1]].x, pos[tri[1]].y, 0, uvCorner[tri[1]].u, uvCorner[tri[1]].v, 1, 1, 1, a[tri[1]]);
                }
            }
            Upload(m, bySheet);
        }
        _liquidMesh[s.Sid] = m;
        return m;
    }

    // ---- sprite mesh (billboards), grouped by group_id (one tiny texture each) ----
    private Mesh GetSpriteMesh(GameAssets.SectorInfo s)
    {
        if (_spriteMesh.TryGetValue(s.Sid, out var m)) return m;
        m = new Mesh();
        var sprites = _assets.Sprites;
        var places = sprites?.SectorPlacements(s.Sid);
        if (sprites is not null && places is not null)
        {
            var byGroup = new Dictionary<int, List<float>>();
            foreach (var p in places)
            {
                if (!sprites.TryMeta(p.Group, out var sp)) continue;
                float footX = p.Px + SpriteAssets.ShiftX, footY = p.Py + SpriteAssets.ShiftY;
                float tlx = footX - sp.Ax, tly = footY - sp.Ay;
                float z = Math.Clamp(1f - p.Py * DepthScale, 0.001f, 0.999f);
                float x0 = tlx, y0 = tly, x1 = tlx + sp.W, y1 = tly + sp.H;
                if (!byGroup.TryGetValue(p.Group, out var buf)) { buf = new List<float>(64); byGroup[p.Group] = buf; }
                V(buf, x0, y0, z, 0, 0, 1, 1, 1, 1); V(buf, x1, y0, z, 1, 0, 1, 1, 1, 1); V(buf, x1, y1, z, 1, 1, 1, 1, 1, 1);
                V(buf, x0, y0, z, 0, 0, 1, 1, 1, 1); V(buf, x1, y1, z, 1, 1, 1, 1, 1, 1); V(buf, x0, y1, z, 0, 1, 1, 1, 1, 1);
            }
            Upload(m, byGroup);
        }
        _spriteMesh[s.Sid] = m;
        return m;
    }

    private static void Upload(Mesh m, Dictionary<int, List<float>> byKey)
    {
        foreach (var kv in byKey)
        {
            float[] arr = kv.Value.ToArray();
            int vao = GL.GenVertexArray(); GL.BindVertexArray(vao);
            int vbo = GL.GenBuffer(); GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, arr.Length * sizeof(float), arr, BufferUsageHint.StaticDraw);
            GL.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, 9 * 4, 0); GL.EnableVertexAttribArray(0);
            GL.VertexAttribPointer(1, 2, VertexAttribPointerType.Float, false, 9 * 4, 3 * 4); GL.EnableVertexAttribArray(1);
            GL.VertexAttribPointer(2, 4, VertexAttribPointerType.Float, false, 9 * 4, 5 * 4); GL.EnableVertexAttribArray(2);
            m.B.Add((kv.Key, vao, vbo, arr.Length / 9));
        }
        GL.BindVertexArray(0);
    }

    private static void V(List<float> b, float x, float y, float z, float u, float v, float r, float g, float bl, float a)
    { b.Add(x); b.Add(y); b.Add(z); b.Add(u); b.Add(v); b.Add(r); b.Add(g); b.Add(bl); b.Add(a); }

    private static void Vf(List<float> b, float x, float y, float up, float vp, float us, float vs)
    { b.Add(x); b.Add(y); b.Add(0); b.Add(up); b.Add(vp); b.Add(us); b.Add(vs); }

    private static int BuildProgram(string vs, string fs)
    {
        int v = Compile(ShaderType.VertexShader, vs), f = Compile(ShaderType.FragmentShader, fs);
        int p = GL.CreateProgram();
        GL.AttachShader(p, v); GL.AttachShader(p, f); GL.LinkProgram(p);
        GL.GetProgram(p, GetProgramParameterName.LinkStatus, out int ok);
        if (ok == 0) throw new Exception("GL link: " + GL.GetProgramInfoLog(p));
        GL.DeleteShader(v); GL.DeleteShader(f);
        return p;
    }
    private static int Compile(ShaderType type, string src)
    {
        int s = GL.CreateShader(type); GL.ShaderSource(s, src); GL.CompileShader(s);
        GL.GetShader(s, ShaderParameter.CompileStatus, out int ok);
        if (ok == 0) throw new Exception($"GL {type}: " + GL.GetShaderInfoLog(s));
        return s;
    }
}
