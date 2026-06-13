using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using Microsoft.Win32;
using SacredStudio.CodeGen;
using SacredStudio.Io;
using SacredStudio.Model;

namespace SacredStudio;

public partial class MainWindow : Window
{
    private readonly AppState _state = new();

    public MainWindow()
    {
        InitializeComponent();

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
