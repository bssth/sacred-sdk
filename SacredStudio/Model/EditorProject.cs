namespace SacredStudio.Model;

/// <summary>
/// The full editable document: all placements for one mod, plus metadata that
/// drives code generation (mod folder name, which playable class's script tree
/// it deploys into).
/// </summary>
public sealed class EditorProject
{
    public int SchemaVersion { get; set; } = 1;

    /// <summary>Mod folder under custom/lua/, e.g. "lost_manuscript".</summary>
    public string ModName { get; set; } = "untitled_mod";

    /// <summary>Free-text author note carried into the generated Lua header.</summary>
    public string Description { get; set; } = "";

    /// <summary>
    /// Playable class whose FunkCode tree this deploys into, e.g.
    /// "TYPE_NPC_GLADIATOR". Empty = class-agnostic / runtime-only.
    /// </summary>
    public string TargetClass { get; set; } = "";

    public List<Placement> Placements { get; set; } = new();

    public IEnumerable<NpcPlacement> Npcs => Placements.OfType<NpcPlacement>();
    public IEnumerable<PointPlacement> Points => Placements.OfType<PointPlacement>();
    public IEnumerable<ZonePlacement> Zones => Placements.OfType<ZonePlacement>();
    public IEnumerable<ItemPlacement> Items => Placements.OfType<ItemPlacement>();
}
