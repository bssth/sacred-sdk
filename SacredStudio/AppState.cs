using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using SacredStudio.CodeGen;
using SacredStudio.Model;

namespace SacredStudio;

public enum EditorTool { Select, Npc, Point, Zone, Item }

/// <summary>
/// Observable editor state bound by MainWindow. Keeps the EditorProject and the
/// live ObservableCollection in sync so the map, list and Lua preview all react.
/// </summary>
public sealed class AppState : INotifyPropertyChanged
{
    public ObservableCollection<Placement> Placements { get; } = new();

    private EditorProject _project = new();
    public EditorProject Project
    {
        get => _project;
        private set { _project = value; OnChanged(); }
    }

    private EditorTool _tool = EditorTool.Select;
    public EditorTool Tool
    {
        get => _tool;
        set { if (_tool != value) { _tool = value; OnChanged(); } }
    }

    private Placement? _selected;
    public Placement? Selected
    {
        get => _selected;
        set { if (!ReferenceEquals(_selected, value)) { _selected = value; OnChanged(); } }
    }

    private string? _projectPath;
    public string? ProjectPath
    {
        get => _projectPath;
        set { _projectPath = value; OnChanged(); OnChanged(nameof(WindowTitle)); }
    }

    public string WindowTitle =>
        $"SacredStudio — {(_projectPath is null ? "untitled" : System.IO.Path.GetFileName(_projectPath))}";

    public void NewProject()
    {
        Project = new EditorProject();
        Placements.Clear();
        Selected = null;
        ProjectPath = null;
    }

    public void LoadProject(EditorProject project, string? path)
    {
        Project = project;
        Placements.Clear();
        foreach (var p in project.Placements) Placements.Add(p);
        Selected = null;
        ProjectPath = path;
    }

    /// <summary>Mirror the live collection back into the project before save/codegen.</summary>
    public void SyncToProject()
    {
        Project.Placements.Clear();
        Project.Placements.AddRange(Placements);
    }

    public void Add(Placement p)
    {
        Placements.Add(p);
        Selected = p;
    }

    public void Remove(Placement p)
    {
        int idx = Placements.IndexOf(p);
        Placements.Remove(p);
        if (ReferenceEquals(Selected, p))
            Selected = Placements.Count == 0 ? null : Placements[Math.Min(idx, Placements.Count - 1)];
    }

    public string GenerateLua()
    {
        SyncToProject();
        return LuaEmitter.Emit(Project);
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
