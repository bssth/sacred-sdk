using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using Microsoft.Win32;
using OpenTK.Wpf;
using SacredStudio.CodeGen;
using SacredStudio.Io;
using SacredStudio.Model;

namespace SacredStudio;

public partial class MainWindow : Window
{
    private readonly AppState _state = new();
    private readonly GlTerrainRenderer _glr = new();
    private readonly System.Diagnostics.Stopwatch _fpsSw = System.Diagnostics.Stopwatch.StartNew();
    private int _fpsFrames;
    private bool _dbgLock; private double _dbgZoom; private int _dbgWx, _dbgWy;

    public MainWindow()
    {
        InitializeComponent();

        // GL terrain layer (under the transparent IsoMapView marker overlay).
        Map.Background = Brushes.Transparent;   // hit-testable but lets GL show through
        Map.ShowBackdrop = true;                // baked LOD covers FAR overview; GL takes near zoom
        Gl.Ready += _glr.Ready;
        Gl.Render += _ =>
        {
            _glr.ViewW = Gl.ActualWidth;
            _glr.ViewH = Gl.ActualHeight;
            if (_dbgLock)
            {
                // Debug harness: lock the GL view to a fixed zoom/center (immune to any
                // stray wheel drift), so screenshots are reproducible.
                double ix = (_dbgWx - _dbgWy) * 48.0, iy = (_dbgWx + _dbgWy) * 24.0;
                _glr.Zoom = _dbgZoom;
                _glr.PanX = Gl.ActualWidth / 2 - ix * _dbgZoom;
                _glr.PanY = Gl.ActualHeight / 2 - iy * _dbgZoom;
            }
            else
            {
                _glr.Zoom = Map.ZoomValue;
                _glr.PanX = Map.PanXValue;
                _glr.PanY = Map.PanYValue;
            }
            _glr.Render();

            // Lightweight FPS HUD (verifies the streaming fix at a glance).
            _fpsFrames++;
            if (_fpsSw.Elapsed.TotalSeconds >= 0.5)
            {
                double fps = _fpsFrames / _fpsSw.Elapsed.TotalSeconds;
                _fpsFrames = 0; _fpsSw.Restart();
                StatusMap.Text = $"GL {fps:0} fps · sheets {_glr.ResidentSheets} · z={Map.ZoomValue:0.000}";
            }
        };
        Gl.Start(new GLWpfControlSettings { MajorVersion = 3, MinorVersion = 3 });

        // Debug harness: SS_ZOOM=<z> jumps to that zoom centered on a known populated
        // tile right after layout, so a screenshot can inspect a fixed view.
        var dbgZoom = Environment.GetEnvironmentVariable("SS_ZOOM");
        if (dbgZoom is not null &&
            double.TryParse(dbgZoom, System.Globalization.NumberStyles.Float,
                            System.Globalization.CultureInfo.InvariantCulture, out double dz))
        {
            int twx = 2107, twy = 3548;
            var dbgTile = Environment.GetEnvironmentVariable("SS_TILE");
            if (dbgTile is not null)
            {
                var parts = dbgTile.Split(',');
                if (parts.Length == 2 && int.TryParse(parts[0], out int px) && int.TryParse(parts[1], out int py))
                { twx = px; twy = py; }
            }
            Map.Loaded += (_, _) => Map.DebugView(dz, twx, twy);
            _dbgLock = true; _dbgZoom = dz; _dbgWx = twx; _dbgWy = twy;
        }

        Map.Bind(_state.Placements);
        Map.TileClicked += OnMapTileClicked;
        Map.PlacementPicked += OnMapPlacementPicked;
        Map.HoverChanged += t => StatusHover.Text = $"hover: {t}  sector {t.SectorGrid}  local {t.Local}";

        _state.Placements.CollectionChanged += (_, _) => UpdateCounts();
        _state.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(AppState.Selected)) SyncSelection();
            if (e.PropertyName == nameof(AppState.WindowTitle)) Title = _state.WindowTitle;
            if (e.PropertyName == nameof(AppState.Tool)) StatusTool.Text = $"tool: {_state.Tool}";
        };

        BindMetadata();
        UpdateCounts();
        Title = _state.WindowTitle;
        StatusMap.Text = Map.HasBackdrop ? "map: terrain loaded" : "map: grid only (no baked terrain found)";
    }

    // ---- metadata two-way (manual; small + avoids INPC on project) -----------

    private void BindMetadata()
    {
        TbModName.Text = _state.Project.ModName;
        TbTargetClass.Text = _state.Project.TargetClass;
        TbDescription.Text = _state.Project.Description;
        TbModName.LostFocus += (_, _) => _state.Project.ModName = TbModName.Text;
        TbTargetClass.LostFocus += (_, _) => _state.Project.TargetClass = TbTargetClass.Text;
        TbDescription.LostFocus += (_, _) => _state.Project.Description = TbDescription.Text;
    }

    private void RefreshMetadata()
    {
        TbModName.Text = _state.Project.ModName;
        TbTargetClass.Text = _state.Project.TargetClass;
        TbDescription.Text = _state.Project.Description;
    }

    // ---- map / list interaction ----------------------------------------------

    private void OnMapTileClicked(WorldTile tile, EditorTool tool)
    {
        Placement p = tool switch
        {
            EditorTool.Npc => new NpcPlacement { Name = $"npc{_state.Placements.Count + 1}", Pos = tile },
            EditorTool.Point => new PointPlacement { Name = $"pt{_state.Placements.Count + 1}", Pos = tile },
            EditorTool.Zone => new ZonePlacement { Name = $"zone{_state.Placements.Count + 1}", Pos = tile, W = 4, H = 4 },
            EditorTool.Item => new ItemPlacement { Name = $"item{_state.Placements.Count + 1}", Pos = tile },
            _ => null!,
        };
        if (p is not null) _state.Add(p);
    }

    private void OnMapPlacementPicked(Placement p) => _state.Selected = p;

    private void OnListSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (PlacementList.SelectedItem is Placement p) _state.Selected = p;
    }

    private void SyncSelection()
    {
        PropertyHost.Content = _state.Selected;
        Map.SetSelected(_state.Selected);
        if (!ReferenceEquals(PlacementList.SelectedItem, _state.Selected))
            PlacementList.SelectedItem = _state.Selected;
        if (_state.Selected is not null)
            Map.FocusTileIfOffscreen(_state.Selected.Pos);
    }

    private void UpdateCounts()
    {
        StatusCount.Text = $"{_state.Placements.Count} placements";
        if (PlacementList.ItemsSource is null)
            PlacementList.ItemsSource = _state.Placements;
    }

    // ---- tools ----------------------------------------------------------------

    private void OnToolChanged(object sender, RoutedEventArgs e)
    {
        if (sender is RadioButton { Tag: string tag } && Enum.TryParse<EditorTool>(tag, out var tool))
        {
            _state.Tool = tool;
            Map.Tool = tool;
        }
    }

    // ---- map view controls -----------------------------------------------------

    private void OnFitMap(object sender, RoutedEventArgs e) => Map.FitToMap();

    private void OnToggleBackdrop(object sender, RoutedEventArgs e)
    {
        if (sender is System.Windows.Controls.Primitives.ToggleButton t)
        {
            Map.ShowBackdrop = t.IsChecked == true;
            Map.InvalidateVisual();
        }
    }

    private void OnToggleGrid(object sender, RoutedEventArgs e)
    {
        if (sender is System.Windows.Controls.Primitives.ToggleButton t)
        {
            Map.ShowGrid = t.IsChecked == true;
            Map.InvalidateVisual();
        }
    }

    // ---- menu ------------------------------------------------------------------

    private void OnNew(object sender, RoutedEventArgs e)
    {
        _state.NewProject();
        RefreshMetadata();
        PropertyHost.Content = null;
        LuaPreview.Text = "";
    }

    private void OnOpen(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Filter = "SacredStudio project (*.sstudio.json)|*.sstudio.json|JSON (*.json)|*.json" };
        if (dlg.ShowDialog() != true) return;
        try
        {
            var project = ProjectStore.Load(dlg.FileName);
            _state.LoadProject(project, dlg.FileName);
            RefreshMetadata();
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Open failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void OnSave(object sender, RoutedEventArgs e)
    {
        if (_state.ProjectPath is null) { OnSaveAs(sender, e); return; }
        DoSave(_state.ProjectPath);
    }

    private void OnSaveAs(object sender, RoutedEventArgs e)
    {
        var dlg = new SaveFileDialog
        {
            Filter = "SacredStudio project (*.sstudio.json)|*.sstudio.json",
            FileName = $"{_state.Project.ModName}.sstudio.json",
        };
        if (dlg.ShowDialog() != true) return;
        DoSave(dlg.FileName);
        _state.ProjectPath = dlg.FileName;
    }

    private void DoSave(string path)
    {
        CommitFocusedEdit();
        _state.SyncToProject();
        try { ProjectStore.Save(_state.Project, path); }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Save failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void OnExportLua(object sender, RoutedEventArgs e)
    {
        CommitFocusedEdit();
        var dlg = new SaveFileDialog { Filter = "Lua (*.lua)|*.lua", FileName = "init.lua" };
        if (dlg.ShowDialog() != true) return;
        try { File.WriteAllText(dlg.FileName, _state.GenerateLua()); }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Export failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void OnRegenerateLua(object sender, RoutedEventArgs e)
    {
        CommitFocusedEdit();
        LuaPreview.Text = _state.GenerateLua();
    }

    private void OnDelete(object sender, RoutedEventArgs e)
    {
        if (_state.Selected is { } p) _state.Remove(p);
    }

    protected override void OnPreviewKeyDown(KeyEventArgs e)
    {
        if (e.Key == Key.Delete && _state.Selected is { } p
            && PlacementList.IsKeyboardFocusWithin == false
            && PropertyHost.IsKeyboardFocusWithin == false)
        {
            _state.Remove(p);
            e.Handled = true;
        }
        base.OnPreviewKeyDown(e);
    }

    private void OnExit(object sender, RoutedEventArgs e) => Close();

    /// <summary>Push any pending LostFocus-bound textbox edit before serialising.</summary>
    private void CommitFocusedEdit()
    {
        if (Keyboard.FocusedElement is TextBox tb)
        {
            var be = tb.GetBindingExpression(TextBox.TextProperty);
            be?.UpdateSource();
        }
        _state.Project.ModName = TbModName.Text;
        _state.Project.TargetClass = TbTargetClass.Text;
        _state.Project.Description = TbDescription.Text;
    }
}
