using System.Text.Json.Serialization;

namespace SacredStudio.Model;

/// <summary>Discriminator for the placement kinds the editor can author.</summary>
public enum PlacementKind
{
    Npc,
    Point,
    Zone,
    Item,
}

/// <summary>
/// Dev NPC archetypes extracted from the 4182 hand-placed campaign CreateNPC
/// records (npc_templates.lua). spawn_template resolves these to a type-correct
/// engine init, then we apply at most one surgical delta. See docs/24-storyline.md.
/// </summary>
public enum NpcArchetype
{
    FriendlyTownGuard,
    PatrolSoldier,
    BellevueEnemy,
    DormantEnemy,
    Townsperson,
    QuestNpc,
    AmbientAnimal,
    AllyCompanion,
}

public static class NpcArchetypeNames
{
    // Lua identifier used by N.spawn_template(<archetype>, ...).
    public static string ToLua(NpcArchetype a) => a switch
    {
        NpcArchetype.FriendlyTownGuard => "friendly_town_guard",
        NpcArchetype.PatrolSoldier => "patrol_soldier",
        NpcArchetype.BellevueEnemy => "bellevue_enemy",
        NpcArchetype.DormantEnemy => "dormant_enemy",
        NpcArchetype.Townsperson => "townsperson",
        NpcArchetype.QuestNpc => "quest_npc",
        NpcArchetype.AmbientAnimal => "ambient_animal",
        NpcArchetype.AllyCompanion => "ally_companion",
        _ => "townsperson",
    };
}

/// <summary>
/// Base for everything placed on the map. Id is a stable editor handle (used for
/// selection + generated Lua local names). Pos is the anchor world tile.
/// </summary>
[JsonPolymorphic(TypeDiscriminatorPropertyName = "kind")]
[JsonDerivedType(typeof(NpcPlacement), "npc")]
[JsonDerivedType(typeof(PointPlacement), "point")]
[JsonDerivedType(typeof(ZonePlacement), "zone")]
[JsonDerivedType(typeof(ItemPlacement), "item")]
public abstract class Placement : ObservableObject
{
    private string _id = Guid.NewGuid().ToString("N")[..8];
    private string _name = "";
    private WorldTile _pos;

    public string Id { get => _id; set => Set(ref _id, value); }

    public string Name
    {
        get => _name;
        set { if (Set(ref _name, value)) { Raise(nameof(LuaLocal)); Raise(nameof(Display)); } }
    }

    public WorldTile Pos
    {
        get => _pos;
        set { if (Set(ref _pos, value)) { Raise(nameof(Wx)); Raise(nameof(Wy)); Raise(nameof(Display)); } }
    }

    /// <summary>Editable X without binding into the struct directly.</summary>
    [JsonIgnore]
    public int Wx { get => _pos.Wx; set => Pos = _pos with { Wx = value }; }

    [JsonIgnore]
    public int Wy { get => _pos.Wy; set => Pos = _pos with { Wy = value }; }

    [JsonIgnore]
    public abstract PlacementKind Kind { get; }

    /// <summary>List-row label.</summary>
    [JsonIgnore]
    public string Display => $"[{Kind}] {(string.IsNullOrWhiteSpace(Name) ? Id : Name)}  {Pos}";

    /// <summary>Sanitised lua local identifier, e.g. "npc_leandra".</summary>
    [JsonIgnore]
    public string LuaLocal
    {
        get
        {
            string slug = new string((Name.Length > 0 ? Name : Id)
                .Select(c => char.IsLetterOrDigit(c) ? char.ToLowerInvariant(c) : '_').ToArray());
            if (slug.Length == 0 || !char.IsLetter(slug[0])) slug = "_" + slug;
            return $"{Kind.ToString().ToLowerInvariant()}_{slug}";
        }
    }
}

/// <summary>An NPC spawned via the engine CreateNPC path from a dev template.</summary>
public sealed class NpcPlacement : Placement
{
    private NpcArchetype _archetype = NpcArchetype.Townsperson;
    private int _npcType;
    private string? _npcTypeName;
    private int _subId = 1;
    private int? _stanceClass;
    private int? _stanceMatrix;
    private bool _questGiver;
    private bool _immortal;
    private bool _stationary;
    private int? _level;

    public override PlacementKind Kind => PlacementKind.Npc;

    public NpcArchetype Archetype { get => _archetype; set => Set(ref _archetype, value); }

    /// <summary>NPC type id (from npc.lua / TYPE_* table). 0 = leave template default.</summary>
    public int NpcType { get => _npcType; set => Set(ref _npcType, value); }
    public string? NpcTypeName { get => _npcTypeName; set => Set(ref _npcTypeName, value); }

    public int SubId { get => _subId; set => Set(ref _subId, value); }

    /// <summary>One surgical stance delta o:stance(class, matrix). Null = engine default.</summary>
    public int? StanceClass { get => _stanceClass; set => Set(ref _stanceClass, value); }
    public int? StanceMatrix { get => _stanceMatrix; set => Set(ref _stanceMatrix, value); }

    public bool QuestGiver { get => _questGiver; set => Set(ref _questGiver, value); }
    public bool Immortal { get => _immortal; set => Set(ref _immortal, value); }
    public bool Stationary { get => _stationary; set => Set(ref _stationary, value); }
    public int? Level { get => _level; set => Set(ref _level, value); }

    public List<EquipEntry> Equipment { get; set; } = new();
}

/// <summary>One equipped item delta: o:equip(item_type, slot). Slot 0x0D = main weapon.</summary>
public sealed class EquipEntry
{
    public int ItemType { get; set; }
    public string? ItemTypeName { get; set; }
    public int Slot { get; set; } = 0x0D;
}

/// <summary>A named position the script can reference, e.g. patrol waypoints.</summary>
public sealed class PointPlacement : Placement
{
    public override PlacementKind Kind => PlacementKind.Point;
}

/// <summary>
/// An axis-aligned world-tile rectangle (MVP). Inclusive bounds in world tiles.
/// Pos is the top-left anchor; (W, H) extend it. Emits an in_zone() helper table.
/// </summary>
public sealed class ZonePlacement : Placement
{
    private int _w = 1;
    private int _h = 1;

    public override PlacementKind Kind => PlacementKind.Zone;

    public int W { get => _w; set { if (Set(ref _w, value)) Raise(nameof(MaxWx)); } }
    public int H { get => _h; set { if (Set(ref _h, value)) Raise(nameof(MaxWy)); } }

    [JsonIgnore] public int MaxWx => Pos.Wx + Math.Max(0, W - 1);
    [JsonIgnore] public int MaxWy => Pos.Wy + Math.Max(0, H - 1);
}

/// <summary>A world item drop via sacred.spawn_item(type, kx, ky).</summary>
public sealed class ItemPlacement : Placement
{
    private int _itemType;
    private string? _itemTypeName;

    public override PlacementKind Kind => PlacementKind.Item;
    public int ItemType { get => _itemType; set => Set(ref _itemType, value); }
    public string? ItemTypeName { get => _itemTypeName; set => Set(ref _itemTypeName, value); }
}
