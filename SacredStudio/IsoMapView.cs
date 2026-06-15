using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using SacredStudio.Model;

namespace SacredStudio;

/// <summary>
/// Lightweight isometric map view rendered via DrawingContext (immediate mode),
/// so hundreds of markers + a live grid stay cheap. Screen = iso * zoom + pan,
/// where iso = IsoProjection.WorldToIso(tile). A baked LOD backdrop can be
/// layered behind the grid later; for now the grid gives spatial reference.
/// </summary>
public sealed class IsoMapView : FrameworkElement
{
    private ObservableCollection<Placement>? _placements;
    private Placement? _selected;

    private double _zoom = 0.5;
    private Vector _pan = new(0, 0);
    private bool _panning;
    private Point _lastMouse;
    private WorldTile _hover;

    private readonly MapBackdrop _backdrop = new();
    public bool ShowBackdrop { get; set; } = true;
    public bool ShowGrid { get; set; } = true;
    public bool HasBackdrop => _backdrop.Available;

    // Exposed so the GL terrain renderer behind this overlay can mirror the camera
    // (screen = iso*zoom + pan), keeping terrain aligned 1:1 with markers.
    public double ZoomValue => _zoom;
    public double PanXValue => _pan.X;
    public double PanYValue => _pan.Y;

    public event Action<WorldTile, EditorTool>? TileClicked;
    public event Action<Placement>? PlacementPicked;
    public event Action<WorldTile>? HoverChanged;

    public EditorTool Tool { get; set; } = EditorTool.Select;

    public IsoMapView()
    {
        Focusable = true;
        ClipToBounds = true;
        // Smooth (Fant) scaling for the terrain bitmaps — crisp when down-sampled,
        // softly filtered rather than blocky when a tile is up-sampled.
        RenderOptions.SetBitmapScalingMode(this, BitmapScalingMode.HighQuality);
        Background = new SolidColorBrush(Color.FromRgb(18, 20, 28));
        // First layout: frame the whole baked world if we have terrain, else
        // just center the iso origin.
        Loaded += (_, _) =>
        {
            if (_backdrop.Available) FitToMap();
            else { _zoom = 0.5; FocusTile(new WorldTile(2080, 3500)); }   // land on a populated area
        };
    }

    /// <summary>Frame the whole baked terrain (used on load + the "Fit map" button).</summary>
    public void FitToMap()
    {
        if (ActualWidth <= 0 || ActualHeight <= 0) return;
        if (!_backdrop.TryGetIsoBounds(out double x0, out double y0, out double x1, out double y1))
        {
            _zoom = 0.5; FocusTile(new WorldTile(2080, 3500)); return;   // no backdrop → populated area
        }
        double w = x1 - x0, h = y1 - y0;
        _zoom = Math.Clamp(Math.Min(ActualWidth / w, ActualHeight / h) * 0.94, 0.0008, 8.0);
        double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
        _pan = new Vector(ActualWidth / 2 - cx * _zoom, ActualHeight / 2 - cy * _zoom);
        InvalidateVisual();
    }

    public Brush Background { get; set; }

    public void Bind(ObservableCollection<Placement> placements)
    {
        if (_placements is not null)
            _placements.CollectionChanged -= OnCollectionChanged;
        _placements = placements;
        _placements.CollectionChanged += OnCollectionChanged;
        InvalidateVisual();
    }

    public void SetSelected(Placement? p)
    {
        _selected = p;
        InvalidateVisual();
    }

    /// <summary>Debug: jump to a zoom level centered on a world tile (env-gated harness).</summary>
    public void DebugView(double zoom, int wx, int wy)
    {
        if (ActualWidth <= 0 || ActualHeight <= 0) return;
        _zoom = Math.Clamp(zoom, 0.02, 8.0);
        var iso = IsoProjection.WorldToIso(new WorldTile(wx, wy));
        _pan = new Vector(ActualWidth / 2 - iso.X * _zoom, ActualHeight / 2 - iso.Y * _zoom);
        InvalidateVisual();
    }

    public void FocusTile(WorldTile t)
    {
        var iso = IsoProjection.WorldToIso(t);
        _pan = new Vector(ActualWidth / 2 - iso.X * _zoom, ActualHeight / 2 - iso.Y * _zoom);
        InvalidateVisual();
    }

    /// <summary>Re-center only when the tile is outside the current viewport.</summary>
    public void FocusTileIfOffscreen(WorldTile t)
    {
        if (ActualWidth <= 0 || ActualHeight <= 0) return;
        Point s = WorldToScreen(t);
        const double margin = 40;
        if (s.X < margin || s.Y < margin || s.X > ActualWidth - margin || s.Y > ActualHeight - margin)
            FocusTile(t);
    }

    private void OnCollectionChanged(object? s, NotifyCollectionChangedEventArgs e) => InvalidateVisual();

    // ---- coordinate transforms ----------------------------------------------

    private Point IsoToScreen(IsoPoint p) => new(p.X * _zoom + _pan.X, p.Y * _zoom + _pan.Y);

    private WorldTile ScreenToWorld(Point screen)
    {
        double ix = (screen.X - _pan.X) / _zoom;
        double iy = (screen.Y - _pan.Y) / _zoom;
        return IsoProjection.IsoToWorld(ix, iy);
    }

    private Point WorldToScreen(WorldTile t) => IsoToScreen(IsoProjection.WorldToIso(t));

    // ---- input ----------------------------------------------------------------

    protected override void OnMouseWheel(MouseWheelEventArgs e)
    {
        Point before = e.GetPosition(this);
        WorldTile anchorWorld = ScreenToWorld(before);
        double factor = e.Delta > 0 ? 1.15 : 1 / 1.15;
        _zoom = Math.Clamp(_zoom * factor, 0.02, 8.0);
        // Keep the tile under the cursor fixed during zoom.
        Point after = WorldToScreen(anchorWorld);
        _pan += before - after;
        InvalidateVisual();
    }

    protected override void OnMouseDown(MouseButtonEventArgs e)
    {
        Focus();
        _lastMouse = e.GetPosition(this);
        if (e.ChangedButton is MouseButton.Middle or MouseButton.Right)
        {
            _panning = true;
            CaptureMouse();
            return;
        }
        if (e.ChangedButton == MouseButton.Left)
        {
            var tile = ScreenToWorld(_lastMouse);
            if (Tool == EditorTool.Select)
            {
                var hit = PickNearest(_lastMouse);
                if (hit is not null) PlacementPicked?.Invoke(hit);
            }
            else
            {
                TileClicked?.Invoke(tile, Tool);
            }
        }
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        Point pos = e.GetPosition(this);
        if (_panning)
        {
            _pan += pos - _lastMouse;
            _lastMouse = pos;
            InvalidateVisual();
            return;
        }
        var t = ScreenToWorld(pos);
        if (!t.Equals(_hover))
        {
            _hover = t;
            HoverChanged?.Invoke(t);
            InvalidateVisual();
        }
    }

    protected override void OnMouseUp(MouseButtonEventArgs e)
    {
        if (_panning && e.ChangedButton is MouseButton.Middle or MouseButton.Right)
        {
            _panning = false;
            ReleaseMouseCapture();
        }
    }

    private Placement? PickNearest(Point screen)
    {
        if (_placements is null) return null;
        Placement? best = null;
        double bestDist = 14 * 14; // px^2 hit radius
        foreach (var p in _placements)
        {
            Point s = WorldToScreen(p.Pos);
            double dx = s.X - screen.X, dy = s.Y - screen.Y;
            double d = dx * dx + dy * dy;
            if (d < bestDist) { bestDist = d; best = p; }
        }
        return best;
    }

    // ---- render ----------------------------------------------------------------

    protected override void OnRender(DrawingContext dc)
    {
        dc.DrawRectangle(Background, null, new Rect(0, 0, ActualWidth, ActualHeight));
        DrawBackdrop(dc);
        if (ShowGrid) DrawGrid(dc);
        DrawPlacements(dc);
        DrawHover(dc);
    }

    // Below this zoom the LOD16 overview is still down-sampled (crisp); above it
    // LOD16 starts upscaling, so switch to the 2×-finer LOD8 per-sector images.
    private const double Lod8ZoomThreshold = 0.06;

    /// <summary>
    /// Draw the baked terrain under the grid, picking the level of detail from
    /// zoom: LOD16 overview chunks when far, LOD8 per-sector images when close.
    /// Only tiles intersecting the viewport are loaded/drawn (cull + bitmap cache).
    /// </summary>
    // At/above this zoom the native GL terrain layer (behind this overlay) takes
    // over with full game-fidelity tiles; the baked-LOD images only cover the far
    // overview (where GL would need too many sectors/VRAM).
    public const double GlTerrainZoom = 0.04;

    private void DrawBackdrop(DrawingContext dc)
    {
        if (!ShowBackdrop || !_backdrop.Available) return;
        if (_zoom >= GlTerrainZoom) return;           // GL terrain handles near zoom
        if (_zoom >= Lod8ZoomThreshold && _backdrop.HasLod8) DrawLod8(dc);
        else DrawLod16(dc);
    }

    private void DrawLod16(DrawingContext dc)
    {
        double s = MapBackdrop.ChunkIsoSize;
        double ix0 = (0 - _pan.X) / _zoom, iy0 = (0 - _pan.Y) / _zoom;
        double ix1 = (ActualWidth - _pan.X) / _zoom, iy1 = (ActualHeight - _pan.Y) / _zoom;
        int cx0 = (int)Math.Floor(ix0 / s), cy0 = (int)Math.Floor(iy0 / s);
        int cx1 = (int)Math.Floor(ix1 / s), cy1 = (int)Math.Floor(iy1 / s);
        for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++)
        {
            var bmp = _backdrop.GetChunk(cx, cy);
            if (bmp is null) continue;
            dc.DrawImage(bmp, new Rect(cx * s * _zoom + _pan.X, cy * s * _zoom + _pan.Y, s * _zoom, s * _zoom));
        }
    }

    // LOD8: per-sector images. A sector at grid (gx,gy) has its nominal iso origin
    // at ((gx-gy)*3072,(gx+gy)*1536); invert the viewport's iso corners to a grid
    // range, then draw the available sectors back-to-front (by gx+gy depth).
    private void DrawLod8(DrawingContext dc)
    {
        double ix0 = (0 - _pan.X) / _zoom, iy0 = (0 - _pan.Y) / _zoom;
        double ix1 = (ActualWidth - _pan.X) / _zoom, iy1 = (ActualHeight - _pan.Y) / _zoom;
        double gxMin = double.MaxValue, gxMax = double.MinValue, gyMin = double.MaxValue, gyMax = double.MinValue;
        foreach (var (ix, iy) in new[] { (ix0, iy0), (ix1, iy0), (ix0, iy1), (ix1, iy1) })
        {
            double a = ix / 3072.0, b = iy / 1536.0;     // a = gx-gy, b = gx+gy
            double gx = (a + b) / 2, gy = (b - a) / 2;
            gxMin = Math.Min(gxMin, gx); gxMax = Math.Max(gxMax, gx);
            gyMin = Math.Min(gyMin, gy); gyMax = Math.Max(gyMax, gy);
        }
        int gx0 = (int)Math.Floor(gxMin) - 2, gx1 = (int)Math.Ceiling(gxMax) + 2;
        int gy0 = (int)Math.Floor(gyMin) - 2, gy1 = (int)Math.Ceiling(gyMax) + 2;
        if ((long)(gx1 - gx0) * (gy1 - gy0) > 12000) { DrawLod16(dc); return; }

        var vis = new List<(int gx, int gy)>();
        for (int gy = gy0; gy <= gy1; gy++)
        for (int gx = gx0; gx <= gx1; gx++)
            if (_backdrop.HasSector(gx, gy)) vis.Add((gx, gy));
        vis.Sort((p, q) => (p.gx + p.gy).CompareTo(q.gx + q.gy));   // back-to-front

        foreach (var (gx, gy) in vis)
        {
            var bmp = _backdrop.GetSector(gx, gy);
            if (bmp is null) continue;
            var (ox, oy) = MapBackdrop.SectorIsoOrigin(gx, gy);
            double isoW = bmp.PixelWidth * MapBackdrop.Lod8Factor;
            double isoH = bmp.PixelHeight * MapBackdrop.Lod8Factor;
            dc.DrawImage(bmp, new Rect(ox * _zoom + _pan.X, oy * _zoom + _pan.Y, isoW * _zoom, isoH * _zoom));
        }
    }

    private static readonly Pen GridPen = MakePen(Color.FromArgb(40, 120, 120, 160), 1);
    private static readonly Pen SectorPen = MakePen(Color.FromArgb(90, 90, 160, 200), 1);

    private static Pen MakePen(Color c, double w)
    {
        var pen = new Pen(new SolidColorBrush(c), w);
        pen.Freeze();
        return pen;
    }

    private void DrawGrid(DrawingContext dc)
    {
        // Determine world-tile span covering the viewport, then draw a sparse
        // iso grid. Step adapts to zoom so we never draw too many lines.
        var tl = ScreenToWorld(new Point(0, 0));
        var br = ScreenToWorld(new Point(ActualWidth, ActualHeight));
        var tr = ScreenToWorld(new Point(ActualWidth, 0));
        var bl = ScreenToWorld(new Point(0, ActualHeight));
        int minWx = Math.Min(Math.Min(tl.Wx, br.Wx), Math.Min(tr.Wx, bl.Wx)) - 2;
        int maxWx = Math.Max(Math.Max(tl.Wx, br.Wx), Math.Max(tr.Wx, bl.Wx)) + 2;
        int minWy = Math.Min(Math.Min(tl.Wy, br.Wy), Math.Min(tr.Wy, bl.Wy)) - 2;
        int maxWy = Math.Max(Math.Max(tl.Wy, br.Wy), Math.Max(tr.Wy, bl.Wy)) + 2;

        int span = Math.Max(maxWx - minWx, maxWy - minWy);
        int step = span > 400 ? 64 : span > 120 ? 16 : 4;          // tile lines
        if (span > 2000) step = 64;

        // Snap to step.
        minWx -= ((minWx % step) + step) % step;
        minWy -= ((minWy % step) + step) % step;

        for (int wx = minWx; wx <= maxWx; wx += step)
        {
            Pen pen = wx % WorldTile.SectorSize == 0 ? SectorPen : GridPen;
            dc.DrawLine(pen, WorldToScreen(new WorldTile(wx, minWy)), WorldToScreen(new WorldTile(wx, maxWy)));
        }
        for (int wy = minWy; wy <= maxWy; wy += step)
        {
            Pen pen = wy % WorldTile.SectorSize == 0 ? SectorPen : GridPen;
            dc.DrawLine(pen, WorldToScreen(new WorldTile(minWx, wy)), WorldToScreen(new WorldTile(maxWx, wy)));
        }
    }

    private void DrawPlacements(DrawingContext dc)
    {
        if (_placements is null) return;
        foreach (var p in _placements)
        {
            bool sel = ReferenceEquals(p, _selected);
            if (p is ZonePlacement z) DrawZone(dc, z, sel);
            else DrawMarker(dc, p, sel);
        }
    }

    private void DrawZone(DrawingContext dc, ZonePlacement z, bool sel)
    {
        Point a = WorldToScreen(z.Pos);
        Point b = WorldToScreen(new WorldTile(z.MaxWx + 1, z.Pos.Wy));
        Point c = WorldToScreen(new WorldTile(z.MaxWx + 1, z.MaxWy + 1));
        Point d = WorldToScreen(new WorldTile(z.Pos.Wx, z.MaxWy + 1));
        var fig = new PathFigure { StartPoint = a, IsClosed = true };
        fig.Segments.Add(new PolyLineSegment(new[] { b, c, d }, true));
        var geo = new PathGeometry(new[] { fig });
        var fill = new SolidColorBrush(Color.FromArgb((byte)(sel ? 70 : 40), 80, 200, 120));
        var pen = MakePen(Color.FromArgb(220, 90, 220, 130), sel ? 2 : 1);
        dc.DrawGeometry(fill, pen, geo);
        DrawLabel(dc, a, z.Name, sel);
    }

    private void DrawMarker(DrawingContext dc, Placement p, bool sel)
    {
        Point s = WorldToScreen(p.Pos);
        Color col = p.Kind switch
        {
            PlacementKind.Npc => Color.FromRgb(80, 170, 255),
            PlacementKind.Point => Color.FromRgb(255, 210, 70),
            PlacementKind.Item => Color.FromRgb(220, 120, 255),
            _ => Colors.White,
        };
        double r = sel ? 8 : 6;
        var fill = new SolidColorBrush(col);
        var pen = MakePen(sel ? Colors.White : Color.FromArgb(180, 0, 0, 0), sel ? 2 : 1);
        if (p.Kind == PlacementKind.Point)
        {
            // diamond
            var fig = new PathFigure { StartPoint = new Point(s.X, s.Y - r), IsClosed = true };
            fig.Segments.Add(new PolyLineSegment(new[]
            {
                new Point(s.X + r, s.Y), new Point(s.X, s.Y + r), new Point(s.X - r, s.Y)
            }, true));
            dc.DrawGeometry(fill, pen, new PathGeometry(new[] { fig }));
        }
        else
        {
            dc.DrawEllipse(fill, pen, s, r, r);
        }
        DrawLabel(dc, new Point(s.X + r + 2, s.Y - r), p.Name, sel);
    }

    private void DrawLabel(DrawingContext dc, Point at, string text, bool sel)
    {
        if (string.IsNullOrWhiteSpace(text) || _zoom < 0.12) return;
        var ft = new FormattedText(text, System.Globalization.CultureInfo.InvariantCulture,
            FlowDirection.LeftToRight, new Typeface("Segoe UI"), 11,
            new SolidColorBrush(sel ? Colors.White : Color.FromArgb(200, 220, 220, 220)),
            VisualTreeHelper.GetDpi(this).PixelsPerDip);
        dc.DrawText(ft, at);
    }

    private void DrawHover(DrawingContext dc)
    {
        if (Tool == EditorTool.Select) return;
        Point s = WorldToScreen(_hover);
        var pen = MakePen(Color.FromArgb(160, 255, 255, 255), 1);
        dc.DrawEllipse(null, pen, s, 5, 5);
    }
}
