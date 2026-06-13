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

    public event Action<WorldTile, EditorTool>? TileClicked;
    public event Action<Placement>? PlacementPicked;
    public event Action<WorldTile>? HoverChanged;

    public EditorTool Tool { get; set; } = EditorTool.Select;

    public IsoMapView()
    {
        Focusable = true;
        ClipToBounds = true;
        Background = new SolidColorBrush(Color.FromRgb(20, 20, 25));
        // Center the origin once we have a size.
        Loaded += (_, _) => { _pan = new Vector(ActualWidth / 2, ActualHeight / 3); InvalidateVisual(); };
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
        DrawGrid(dc);
        DrawPlacements(dc);
        DrawHover(dc);
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
