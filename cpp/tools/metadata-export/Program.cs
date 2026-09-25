using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using MphRead;
using MphRead.Entities;

static string P(string? path) => path?.Replace('\\', '/') ?? "";

var models = new SortedDictionary<string, object>();
foreach ((string name, ModelMetadata m) in Metadata.ModelMetadata)
{
    models[name] = new
    {
        modelPath = P(m.ModelPath),
        animationPath = P(m.AnimationPath),
        animationShare = P(m.AnimationShare),
        recolors = m.Recolors.Select(r => new
        {
            name = r.Name,
            modelPath = P(r.ModelPath),
            texturePath = P(r.TexturePath),
            palettePath = P(r.PalettePath),
            replacePath = P(r.ReplacePath),
            replaceIds = r.ReplaceIds.OrderBy(kv => kv.Key).Select(kv => new { from = kv.Key, to = kv.Value }).ToList(),
        }).ToList(),
    };
}

var objects = new List<object>();
for (int i = 0; ; i++)
{
    ObjectMetadata o;
    try { o = Metadata.GetObjectById(i); } catch { break; }
    Vector3Like v = i < Metadata.ObjectVisPosOffsets.Count ? new(Metadata.ObjectVisPosOffsets[i]) : new(default);
    objects.Add(new { name = o.Name, lighting = o.Lighting, recolorId = o.RecolorId, animationIds = o.AnimationIds, visPosOffset = v.Values });
}

var platforms = new List<object?>();
for (int i = 0; ; i++)
{
    PlatformMetadata? p;
    try { p = Metadata.GetPlatformById(i); } catch { break; }
    platforms.Add(p == null ? null : new { name = p.Name, lighting = p.Lighting, animationIds = p.AnimationIds });
}

var playerValues = new List<Dictionary<string, long>>();
foreach (PlayerValues v in Metadata.PlayerValues)
{
    var fields = new Dictionary<string, long>();
    foreach (System.Reflection.FieldInfo f in typeof(PlayerValues).GetFields())
    {
        object? value = f.GetValue(v);
        if (value is Enum e)
        {
            fields[f.Name] = Convert.ToInt64(e);
        }
        else if (value is IConvertible c)
        {
            fields[f.Name] = c.ToInt64(null);
        }
    }
    playerValues.Add(fields);
}

static object? Plain(object? value)
{
    return value switch
    {
        null => null,
        string s => s,
        bool b => b,
        Enum e => Convert.ToInt64(e),
        float f => f,
        double d => d,
        IConvertible c => c.ToInt64(null),
        System.Collections.IEnumerable list => list.Cast<object?>().Select(Plain).ToList(),
        _ => value.ToString(),
    };
}

// A ricochet weapon is exported as its index in Weapons.Ricochets, -1 for none.
var ricochets = Weapons.Ricochets.ToList();
Dictionary<string, object?> WeaponFields(WeaponInfo w) => typeof(WeaponInfo).GetProperties()
    .ToDictionary(p => p.Name, p => p.PropertyType == typeof(WeaponInfo)
        ? (p.GetValue(w) is WeaponInfo r ? ricochets.IndexOf(r) : -1)
        : Plain(p.GetValue(w)));
var weaponsMP = Weapons.WeaponsMP.Select(WeaponFields).ToList();
var ricochetWeapons = Weapons.Ricochets.Select(WeaponFields).ToList();

static Dictionary<string, object?> Fields(object o) => o.GetType().GetFields()
    .Where(f => !f.FieldType.Name.StartsWith("HudObjectInstance"))
    .ToDictionary(f => f.Name, f => f.GetValue(o) is string str ? P(str) : Plain(f.GetValue(o)));

var hud = new
{
    hunterObjects = MphRead.Hud.HudElements.HunterObjects.Select(Fields).ToList(),
    mainHealthbars = MphRead.Hud.HudElements.MainHealthbars.Select(Fields).ToList(),
    subHealthbars = MphRead.Hud.HudElements.SubHealthbars.Select(Fields).ToList(),
    ammoBars = MphRead.Hud.HudElements.AmmoBars.Select(Fields).ToList(),
    boost = P(MphRead.Hud.HudElements.Boost),
    bombs = P(MphRead.Hud.HudElements.Bombs),
};

var result = new
{
    hud,
    models,
    doors = Metadata.Doors.Select(d => new { name = d.Name, lockName = d.LockName, lockOffset = d.LockOffset, radius = d.Radius }),
    doorPalettes = Metadata.DoorPalettes,
    jumpPads = Metadata.JumpPads,
    items = Metadata.Items,
    objects,
    platforms,
    playerValues,
    weaponsMP,
    ricochetWeapons,
    beamDrawEffects = Metadata.BeamDrawEffects,
    effects = Metadata.Effects.Select(e => new { name = e.Name, archive = e.Archive }),
    syluxBombEffects = Metadata.SyluxBombEffects,
    gunAnimationIds = Metadata.GunAnimationIds.Cast<int>().ToList(), // [8 hunters, 13 GunAnimation, 10: node + per beam]
    affinityWeapons = Weapons.AffinityWeapons.Select(b => (int)b).ToList(),
    slipSpeedFactors = Metadata.SlipSpeedFactors,
    tractionFactors = Metadata.TractionFactors,
};
string outPath = args.Length > 0 ? args[0] : "metadata.json";
File.WriteAllText(outPath, JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
Console.WriteLine($"{models.Count} models, {objects.Count} objects, {platforms.Count} platforms -> {outPath}");

readonly record struct Vector3Like(OpenTK.Mathematics.Vector3 V)
{
    public float[] Values => [V.X, V.Y, V.Z];
}
