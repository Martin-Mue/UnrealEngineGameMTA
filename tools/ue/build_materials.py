# -*- coding: utf-8 -*-
"""MTA 6 - Materialien fuer Kestrel Bay erzeugen (Unreal Python, UE 5.8).

Erzeugt unter /Game/MTA6/Materials/:
  MPC_MTA6_World      Material Parameter Collection (Night, TimeOfDay, Wetness)
  M_MTA_Ground        Strasse/Boden world-aligned: Asphalt, Mittellinie, Randlinien, Zebrastreifen,
                      Gehweg mit Bordsteinkante, Gras ausserhalb der Stadt (Port des Godot-Ground-Shaders)
  M_MTA_Building      Fassade: Fensterraster je Stil (Glas/Stahl, Putz, Industrie, Villa),
                      Metall/Glas, nachts emissive Fenster ueber MPC "Night", Toenung per Custom-Data
  M_MTA_LampEmissive  Laternenkoepfe / Leuchtkrone (Farbe per Custom-Data, nachts an)
  M_MTA_Metal         Masten, Antennen, Schornsteine, Dachaufbauten
  M_MTA_TreeTrunk     Baumstaemme
  M_MTA_Foliage       Baumkronen (Farbe per Custom-Data, world-aligned Variation)
  M_MTA_Park          Parkrasen (Maehstreifen, Custom-Data-Farbe)

Die Logik steckt bewusst in HLSL-Custom-Nodes: ein Knoten je Material statt dutzender Einzelknoten
ist robuster gegen API-Unterschiede und spiegelt die Godot-Shader 1:1.
Custom-Data-Indizes muessen zu Source/MTA6/MTACityGenerator.h passen (MTABuildingData, MTAPropData,
MTAGroundData in MTACityGenerator.cpp).

Start (Commandlet, ohne Editor-Fenster):
  "F:\\UnrealEngine\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" "E:\\Projekte\\GTA 6\\MTA6.uproject"
      -run=pythonscript -script="E:/Projekte/GTA 6/tools/ue/build_materials.py" -unattended -nosplash -stdout
Rueckgabe: Prozess-Exitcode 0 bei Erfolg, 1 bei Fehlern (sys.exit).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import unreal  # noqa: E402
from mta_ue_common import (MATERIAL_DIR, MPC_PATH, enum_value, ensure_directory, error, info,  # noqa: E402
                           load_or_create_asset, save_asset, set_prop, warn)

MEL = unreal.MaterialEditingLibrary

# ----------------------------------------------------------------------------- HLSL

# Hash-Hilfen als lokale Struktur (Custom-Nodes erlauben keine freien Funktionen).
HLSL_HASH = """
struct FMTAHash
{
    float H(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
};
FMTAHash Hs;
"""

# Boden: Port von GROUND_SHADER (godot/main.gd), Einheiten intern in Metern.
# Eingaben: WP (Weltposition, cm), OX/OY (Stadtursprung), PER (Periode), RW (Strassenbreite),
# HX/HY (halbe Stadtausdehnung), Wet (MPC). Ausgaben: return BaseColor, Rough.
HLSL_GROUND = HLSL_HASH + """
float2 wp = WP.xy / 100.0;
float2 org = float2(OX, OY) / 100.0;
float P = max(PER / 100.0, 1.0);
float hw = RW / 200.0;
float2 hext = float2(HX, HY) / 100.0;  // nicht "half": reserviertes HLSL-Schluesselwort
float2 p = wp - org;
float2 q = p + hext;
float2 m = fmod(fmod(q, P) + P, P);
float2 d = min(m, P - m);
bool inside = abs(p.x) < hext.x + hw && abs(p.y) < hext.y + hw;
bool roadx = d.x < hw;
bool roady = d.y < hw;
float3 col = float3(0.16, 0.27, 0.12);
Rough = 1.0;
if (inside)
{
    col = float3(0.52, 0.52, 0.50);
    Rough = 0.9;
    if (roadx || roady)
    {
        col = float3(0.11, 0.11, 0.12);
        Rough = 0.8;
        if (!(roadx && roady))
        {
            float along = roadx ? q.y : q.x;
            float across = roadx ? d.x : d.y;
            float seg = fmod(fmod(along, P) + P, P);
            float s0 = hw + 4.0;
            float s1 = P - hw - 4.0;
            if (across < 0.35 && seg > s0 && seg < s1) col = float3(0.9, 0.7, 0.1);
            if (abs(across - (hw - 0.6)) < 0.15 && seg > hw + 1.0 && seg < P - hw - 1.0) col = float3(0.75, 0.75, 0.75);
            bool zebra = (seg > hw && seg < hw + 3.0) || (seg > P - hw - 3.0 && seg < P - hw);
            if (zebra && across < hw - 1.0 && fmod(across * 1.2, 2.0) < 1.0) col = float3(0.85, 0.85, 0.85);
        }
    }
}
float n = Hs.H(floor(wp * 2.5)) * 0.5 + Hs.H(floor(wp * 0.4)) * 0.5;
col *= 0.88 + 0.24 * n;
if (inside && !(roadx || roady) && min(d.x, d.y) < hw + 0.5) col *= 0.7;
Rough = Rough - 0.1 * n;
float w = saturate(Wet);
col *= lerp(1.0, 0.55, w);
Rough = lerp(Rough, 0.12, w);
return col;
"""

# Gebaeude: Port von BUILDING_SHADER plus Stil, Verschleiss und Leuchtfenster-Anteil.
# Eingaben: WP, N (Vertex-Normale Welt), Tint (RGB), HM (Hoehe m), Seed, Wear, Lit, Style, Night, Wet,
# WinLum (Leuchtdichte der Fenster), Ausgaben: return BaseColor, Rough, Metal, Emis (float3).
HLSL_BUILDING = HLSL_HASH + """
float3 wp = WP / 100.0;
float3 nn = normalize(N);
float3 col = Tint;
Rough = 0.85;
Metal = 0.0;
Emis = float3(0.0, 0.0, 0.0);
bool glass = Style < 0.5;
bool industrial = Style > 1.5 && Style < 2.5;
bool villa = Style > 2.5;
if (abs(nn.z) < 0.5)
{
    float u = abs(nn.x) > 0.5 ? wp.y : wp.x;
    float wdepth = abs(nn.x) > 0.5 ? wp.x : wp.y;
    float cw = glass ? 3.0 : (industrial ? 6.0 : 4.0);
    float ch = industrial ? 7.0 : 3.5;
    float2 cell = float2(floor(u / cw), floor(wp.z / ch));
    float2 f = float2(frac(u / cw), frac(wp.z / ch));
    float2 lo = glass ? float2(0.05, 0.10) : float2(0.2, 0.25);
    float2 hi = glass ? float2(0.95, 0.92) : float2(0.8, 0.8);
    if (industrial) { lo = float2(0.08, 0.62); hi = float2(0.92, 0.82); }
    if (villa) { lo = float2(0.3, 0.3); hi = float2(0.7, 0.78); }
    bool win = f.x > lo.x && f.x < hi.x && f.y > lo.y && f.y < hi.y && wp.z > 1.2;
    if (win)
    {
        col = float3(0.06, 0.09, 0.13);
        Rough = 0.08;
        Metal = 0.6;
        float h = Hs.H(cell + float2(floor(wdepth * 0.5), 3.0 + Seed));
        if (h < Lit)
        {
            float t = Hs.H(cell * 1.7 + Seed);
            float3 warm = lerp(float3(1.0, 0.82, 0.5), float3(0.75, 0.85, 1.0), step(0.8, t));
            Emis = warm * (0.6 + 0.4 * t);
        }
    }
    else if (glass)
    {
        col = Tint * 0.55;
        Rough = 0.3;
        Metal = 0.8;
    }
    float base = saturate(1.0 - (wp.z - 0.15) / 2.5);
    float streak = Hs.H(float2(floor(u * 2.0), 7.0 + Seed)) * saturate(1.0 - f.y * 1.3);
    col *= 1.0 - Wear * (0.35 * base + 0.18 * streak);
    Rough = lerp(Rough, 0.95, Wear * 0.3);
}
else
{
    col = float3(0.22, 0.22, 0.22) * (1.0 - 0.3 * Wear);
    Rough = 0.9;
}
float w = saturate(Wet);
col *= lerp(1.0, 0.7, w);
Rough = lerp(Rough, 0.1, w * 0.8);
Emis *= saturate(Night) * WinLum;
return col;
"""

# Laternenkoepfe/Leuchtkrone: Grundfarbe hell, nachts emissiv in Bezirksfarbe.
HLSL_LAMP = """
Emis = LampColor * saturate(Night) * LampLum;
return LampColor * 0.8;
"""

# Laub: Custom-Data-Farbe mit world-aligned Helligkeitsvariation.
HLSL_FOLIAGE = HLSL_HASH + """
float3 wp = WP / 100.0;
float v = Hs.H(floor(wp.xy * 1.5) + floor(wp.z * 2.0)) * 0.25 + 0.85;
return Leaf * v;
"""

# Rasen: Maehstreifen alle 2 m, Farbe per Custom-Data.
HLSL_PARK = HLSL_HASH + """
float2 wp = WP.xy / 100.0;
float stripe = step(0.5, frac(wp.x / 4.0));
float n = Hs.H(floor(wp * 3.0));
return Grass * (0.9 + 0.12 * stripe) * (0.92 + 0.16 * n);
"""


# ----------------------------------------------------------------------------- Bausteine

def float_type(n):
    names = {1: ("CMOT_FLOAT1", "CMOT_Float1"), 2: ("CMOT_FLOAT2", "CMOT_Float2"),
             3: ("CMOT_FLOAT3", "CMOT_Float3"), 4: ("CMOT_FLOAT4", "CMOT_Float4")}[n]
    return enum_value(unreal.CustomMaterialOutputType, *names)


def mat_prop(name):
    return enum_value(unreal.MaterialProperty, name)


class Graph(object):
    """Kleiner Helfer zum Anlegen von Knoten in einem UMaterial."""

    def __init__(self, material):
        self.mat = material
        self.x = -1400
        self.y = -400
        self.ok = True

    def node(self, cls, x=None, y=None):
        if x is None:
            x, self.y = self.x, self.y + 120
            y = self.y
        expr = MEL.create_material_expression(self.mat, cls, x, y)
        if expr is None:
            error("Knoten %s konnte nicht erzeugt werden (%s)" % (cls.__name__, self.mat.get_name()))
            self.ok = False
        return expr

    def world_pos(self):
        return self.node(unreal.MaterialExpressionWorldPosition)

    def normal_ws(self):
        return self.node(unreal.MaterialExpressionVertexNormalWS)

    def custom_data(self, index, default):
        expr = self.node(unreal.MaterialExpressionPerInstanceCustomData)
        if expr:
            set_prop(expr, "data_index", index)
            set_prop(expr, "const_default_value", float(default))
        return expr

    def custom_data3(self, index, default_rgb):
        expr = self.node(unreal.MaterialExpressionPerInstanceCustomData3Vector)
        if expr:
            set_prop(expr, "data_index", index)
            set_prop(expr, "const_default_value", unreal.LinearColor(default_rgb[0], default_rgb[1], default_rgb[2], 1.0))
        return expr

    def scalar_param(self, name, value, group="MTA"):
        expr = self.node(unreal.MaterialExpressionScalarParameter)
        if expr:
            set_prop(expr, "parameter_name", name)
            set_prop(expr, "default_value", float(value))
            set_prop(expr, "group", group, quiet=True)
        return expr

    def vector_param(self, name, rgb, group="MTA"):
        expr = self.node(unreal.MaterialExpressionVectorParameter)
        if expr:
            set_prop(expr, "parameter_name", name)
            set_prop(expr, "default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
            set_prop(expr, "group", group, quiet=True)
        return expr

    def mpc_param(self, mpc, name, fallback_value):
        """MPC-Parameter; fehlt die MPC, wird ein Konstant-Knoten mit Ersatzwert verwendet."""
        if mpc is None:
            expr = self.node(unreal.MaterialExpressionConstant)
            if expr:
                set_prop(expr, "r", float(fallback_value))
            return expr
        expr = self.node(unreal.MaterialExpressionCollectionParameter)
        if expr:
            # Reihenfolge wichtig: erst Collection, dann Name (PostEditChange setzt die ParameterId).
            set_prop(expr, "collection", mpc)
            set_prop(expr, "parameter_name", name)
        return expr

    def custom(self, description, code, out_type, inputs, extra_outputs):
        """inputs: Liste (Name, Ausdruck). extra_outputs: Liste (Name, Komponenten)."""
        expr = self.node(unreal.MaterialExpressionCustom, -500, 0)
        if not expr:
            return None
        set_prop(expr, "description", description)
        set_prop(expr, "code", code)
        set_prop(expr, "output_type", float_type(out_type))
        custom_inputs = []
        for name, _src in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", name)
            custom_inputs.append(ci)
        set_prop(expr, "inputs", custom_inputs)
        outs = []
        for name, comps in extra_outputs:
            co = unreal.CustomOutput()
            co.set_editor_property("output_name", name)
            co.set_editor_property("output_type", float_type(comps))
            outs.append(co)
        set_prop(expr, "additional_outputs", outs)
        for name, src in inputs:
            if src is None:
                self.ok = False
                continue
            if not MEL.connect_material_expressions(src, "", expr, name):
                error("Verbindung -> %s.%s fehlgeschlagen (%s)" % (description, name, self.mat.get_name()))
                self.ok = False
        return expr

    def out(self, expr, output_name, prop_name):
        if expr is None:
            self.ok = False
            return
        if not MEL.connect_material_property(expr, output_name, mat_prop(prop_name)):
            error("Ausgang %s -> %s fehlgeschlagen (%s)" % (output_name or "<main>", prop_name, self.mat.get_name()))
            self.ok = False


def begin_material(name, instanced=True):
    asset, _created = load_or_create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if asset is None:
        return None
    # Idempotent: vorhandene Knoten entfernen und neu aufbauen.
    MEL.delete_all_material_expressions(asset)
    if instanced:
        usage = enum_value(unreal.MaterialUsage, "MATUSAGE_INSTANCED_STATIC_MESHES", "MATUSAGE_InstancedStaticMeshes")
        try:
            MEL.set_material_usage(asset, usage)
        except Exception as exc:  # noqa: BLE001
            warn("Material-Usage ISM fuer %s nicht gesetzt: %s" % (name, exc))
    return asset


def finish_material(asset, graph):
    MEL.layout_material_expressions(asset)
    MEL.recompile_material(asset)
    saved = save_asset(asset)
    if not graph.ok:
        error("Material %s unvollstaendig aufgebaut" % asset.get_name())
    return graph.ok and saved


# ----------------------------------------------------------------------------- Assets

def build_mpc():
    mpc, _created = load_or_create_asset("MPC_MTA6_World", MATERIAL_DIR, unreal.MaterialParameterCollection,
                                         unreal.MaterialParameterCollectionFactoryNew())
    if mpc is None:
        return None
    wanted = [("Night", 0.0), ("TimeOfDay", 18.0), ("Wetness", 0.0)]
    existing = list(mpc.get_editor_property("scalar_parameters"))
    names = [str(p.get_editor_property("parameter_name")) for p in existing]
    for pname, default in wanted:
        if pname in names:
            continue
        p = unreal.CollectionScalarParameter()  # C++-Konstruktor erzeugt die Guid
        p.set_editor_property("parameter_name", pname)
        p.set_editor_property("default_value", default)
        existing.append(p)
        info("MPC-Parameter angelegt: " + pname)
    set_prop(mpc, "scalar_parameters", existing)
    save_asset(mpc)
    return mpc


def build_ground(mpc):
    mat = begin_material("M_MTA_Ground")
    if mat is None:
        return False
    g = Graph(mat)
    inputs = [("WP", g.world_pos()),
              ("OX", g.custom_data(0, 0.0)), ("OY", g.custom_data(1, 0.0)),
              ("PER", g.custom_data(2, 8000.0)), ("RW", g.custom_data(3, 1600.0)),
              ("HX", g.custom_data(4, 32000.0)), ("HY", g.custom_data(5, 32000.0)),
              ("Wet", g.mpc_param(mpc, "Wetness", 0.0))]
    c = g.custom("MTA Ground", HLSL_GROUND, 3, inputs, [("Rough", 1)])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(c, "Rough", "MP_ROUGHNESS")
    return finish_material(mat, g)


def build_building(mpc):
    mat = begin_material("M_MTA_Building")
    if mat is None:
        return False
    g = Graph(mat)
    inputs = [("WP", g.world_pos()), ("N", g.normal_ws()),
              ("Tint", g.custom_data3(0, (0.8, 0.8, 0.8))),
              ("HM", g.custom_data(3, 20.0)), ("Seed", g.custom_data(4, 0.0)),
              ("Wear", g.custom_data(5, 0.3)), ("Lit", g.custom_data(6, 0.45)),
              ("Style", g.custom_data(7, 1.0)),
              ("Night", g.mpc_param(mpc, "Night", 0.0)), ("Wet", g.mpc_param(mpc, "Wetness", 0.0)),
              ("WinLum", g.scalar_param("WindowLuminance", 40.0))]
    c = g.custom("MTA Building", HLSL_BUILDING, 3, inputs, [("Rough", 1), ("Metal", 1), ("Emis", 3)])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(c, "Rough", "MP_ROUGHNESS")
    g.out(c, "Metal", "MP_METALLIC")
    g.out(c, "Emis", "MP_EMISSIVE_COLOR")
    return finish_material(mat, g)


def build_lamp(mpc):
    mat = begin_material("M_MTA_LampEmissive")
    if mat is None:
        return False
    g = Graph(mat)
    inputs = [("LampColor", g.custom_data3(0, (1.0, 0.8, 0.55))),
              ("Night", g.mpc_param(mpc, "Night", 1.0)),
              ("LampLum", g.scalar_param("LampLuminance", 600.0))]
    c = g.custom("MTA Lamp", HLSL_LAMP, 3, inputs, [("Emis", 3)])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(c, "Emis", "MP_EMISSIVE_COLOR")
    rough = g.scalar_param("Roughness", 0.3)
    g.out(rough, "", "MP_ROUGHNESS")
    return finish_material(mat, g)


def build_simple(name, rgb, metallic, roughness):
    """Einfaches PBR-Material mit Parametern (Metall, Rinde)."""
    mat = begin_material(name)
    if mat is None:
        return False
    g = Graph(mat)
    g.out(g.vector_param("BaseColor", rgb), "", "MP_BASE_COLOR")
    g.out(g.scalar_param("Metallic", metallic), "", "MP_METALLIC")
    g.out(g.scalar_param("Roughness", roughness), "", "MP_ROUGHNESS")
    return finish_material(mat, g)


def build_foliage():
    mat = begin_material("M_MTA_Foliage")
    if mat is None:
        return False
    g = Graph(mat)
    inputs = [("WP", g.world_pos()), ("Leaf", g.custom_data3(0, (0.17, 0.42, 0.16)))]
    c = g.custom("MTA Foliage", HLSL_FOLIAGE, 3, inputs, [])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(g.scalar_param("Roughness", 0.85), "", "MP_ROUGHNESS")
    g.out(g.scalar_param("Specular", 0.3), "", "MP_SPECULAR")
    return finish_material(mat, g)


def build_park():
    mat = begin_material("M_MTA_Park")
    if mat is None:
        return False
    g = Graph(mat)
    inputs = [("WP", g.world_pos()), ("Grass", g.custom_data3(0, (0.24, 0.42, 0.18)))]
    c = g.custom("MTA Park", HLSL_PARK, 3, inputs, [])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(g.scalar_param("Roughness", 1.0), "", "MP_ROUGHNESS")
    return finish_material(mat, g)


def main():
    info("build_materials.py startet (Ziel %s)" % MATERIAL_DIR)
    if not ensure_directory(MATERIAL_DIR):
        return 1
    mpc = build_mpc()
    if mpc is None:
        warn("Ohne MPC: Night/Wetness werden als Konstanten eingebaut (keine Tag/Nacht-Reaktion).")
    results = {
        "M_MTA_Ground": build_ground(mpc),
        "M_MTA_Building": build_building(mpc),
        "M_MTA_LampEmissive": build_lamp(mpc),
        "M_MTA_Metal": build_simple("M_MTA_Metal", (0.18, 0.19, 0.2), 0.6, 0.45),
        "M_MTA_TreeTrunk": build_simple("M_MTA_TreeTrunk", (0.3, 0.2, 0.12), 0.0, 0.9),
        "M_MTA_Foliage": build_foliage(),
        "M_MTA_Park": build_park(),
    }
    failed = [k for k, v in results.items() if not v]
    if mpc is None:
        failed.append("MPC_MTA6_World")
    if failed:
        error("Fehlgeschlagen: " + ", ".join(failed))
        return 1
    info("Alle Materialien erzeugt.")
    return 0


if __name__ == "__main__":
    _rc = main()
    if _rc != 0:
        # Im Commandlet fuehrt eine Python-Ausnahme zu Exitcode != 0.
        raise SystemExit(_rc)
