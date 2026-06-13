using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using SacredStudio.Model;

namespace SacredStudio.Io;

/// <summary>Loads/saves an EditorProject as human-diffable JSON.</summary>
public static class ProjectStore
{
    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        Converters = { new JsonStringEnumConverter() },
    };

    public static void Save(EditorProject project, string path)
    {
        string json = JsonSerializer.Serialize(project, Options);
        File.WriteAllText(path, json);
    }

    public static EditorProject Load(string path)
    {
        string json = File.ReadAllText(path);
        return JsonSerializer.Deserialize<EditorProject>(json, Options)
               ?? throw new InvalidDataException($"Project file is empty or invalid: {path}");
    }

    /// <summary>Serialize to a string (for previews / tests) without touching disk.</summary>
    public static string ToJson(EditorProject project) => JsonSerializer.Serialize(project, Options);
}
