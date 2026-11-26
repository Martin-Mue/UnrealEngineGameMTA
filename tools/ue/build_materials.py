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
  M_MTA_Roof          Sattel-/Sheddaecher des Generators (Ziegelreihen, Farbe per Custom-Data)
  M_MTA_Water         Meer: opak, Wellen per World Position Offset, Weltraum-Normale, Fresnel, Kuestenschaum
  M_MTA_Concrete      Kaimauern, Molen, Promenade, Strand, Planken, Salzebene, Landmarken-Beton
  M_MTA_PaintedMetal  Kraene, Container (Wellblech), Masten, Warnstreifen, Rost ueber Verschleiss
  M_MTA_Neon          Leuchtakzente (Farbe/Intensitaet/Flackern/Blinken per Custom-Data, nachts staerker)
  M_MTA_Signage       Plakate, Graffiti, Werbetafeln, Aegis-Logo, Laden-/Strassenschilder, Tueren
  M_MTA_Mountain      Berge: Gras/Fels/Schnee nach Neigung und Hoehe, Dunst nach Entfernung
  M_MTA_Road          Kuestenstrasse/Route 9 (Markierungen ueber die lokale Position)
  M_MTA_Terrain       Wildnis-Gelaende (Vertex-Farbe: Waldboden/Schotter/Erde, Fels nach Neigung)
  M_MTA_Waterfall     Wasserfall/Gischt (transluzent, unlit, Panner, Fresnel, Emissive)

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

# Gebaeude: Port von BUILDING_SHADER plus Stil, Verschleiss, Leuchtfenster-Anteil und Bezirks-Look (Version 2).
# Eingaben: WP, N (Vertex-Normale Welt), Tint (RGB), HM (Hoehe m), Seed, Wear, Lit, Style, Night, Wet,
# WinLum (Leuchtdichte der Fenster), District (CD 8), Neon (CD 9, Ladenband), Detail (CD 10), Accent (CD 11-13),
# NeonLum. Ausgaben: return BaseColor, Rough, Metal, Emis (float3).
HLSL_BUILDING = HLSL_HASH + """
float3 wp = WP / 100.0;
float3 nn = normalize(N);
float3 col = Tint;
Rough = 0.85;
Metal = 0.0;
float3 emWin = float3(0.0, 0.0, 0.0);
float3 emNeon = float3(0.0, 0.0, 0.0);
bool glass = Style < 0.5;
bool industrial = Style > 1.5 && Style < 2.5;
bool villa = Style > 2.5;
int dist = (int)(District + 0.5);
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
    float streak = Hs.H(float2(floor(u * 2.0), 7.0 + Seed)) * saturate(1.0 - f.y * 1.3);
    bool shop = Neon > 0.01 && !industrial && !villa && wp.z < 3.6;
    bool win = !shop && f.x > lo.x && f.x < hi.x && f.y > lo.y && f.y < hi.y && wp.z > 1.2;
    if (shop)
    {
        // Erdgeschoss-Ladenfront mit Neonband (Bezirks-Akzentfarbe, auch am Tag schwach sichtbar).
        float sc = floor(u / 5.0);
        float sf = frac(u / 5.0);
        col = Tint * 0.55;
        if (sf > 0.06 && sf < 0.94 && wp.z > 0.45 && wp.z < 2.85)
        {
            col = float3(0.05, 0.07, 0.09);
            Rough = 0.06;
            Metal = 0.5;
            emWin = float3(1.0, 0.85, 0.6) * (0.3 + 0.4 * Hs.H(float2(sc, Seed)));
        }
        if (wp.z > 2.95 && wp.z < 3.25 && sf > 0.03 && sf < 0.97)
        {
            float hh = Hs.H(float2(sc, 11.0 + Seed));
            float3 a = hh > 0.7 ? Accent.zxy : Accent;
            col = a * 0.5;
            emNeon = a * Neon;
        }
    }
    else if (win)
    {
        col = float3(0.06, 0.09, 0.13);
        Rough = 0.08;
        Metal = 0.6;
        float h = Hs.H(cell + float2(floor(wdepth * 0.5), 3.0 + Seed));
        if (h < Lit)
        {
            float t = Hs.H(cell * 1.7 + Seed);
            float3 warm = lerp(float3(1.0, 0.82, 0.5), float3(0.75, 0.85, 1.0), step(0.8, t));
            emWin = warm * (0.6 + 0.4 * t);
        }
    }
    else
    {
        if (glass) { col = Tint * 0.55; Rough = 0.3; Metal = 0.8; }
        // Bezirkstypisches Fassadendetail: 0 Core Lisenen, 1 Saltmarket Ziegel, 2 Lantern Hill Balkone,
        // 3 Greenmoor Fensterlaeden, 4 Rustline Wellblech + Rost, 5 Sunstrip Art-Deco-Baender.
        if (dist == 0 && glass && f.x < 0.05) { col = float3(0.7, 0.72, 0.75); Metal = 0.9; Rough = 0.25; }
        else if (dist == 1) { float bz = frac(wp.z * 4.0); float bx = frac(u * 2.0 + 0.5 * step(0.5, frac(wp.z * 2.0))); if (bz < 0.08 || bx < 0.04) col *= 1.0 - 0.3 * Detail; }
        else if (dist == 2)
        {
            bool bal = Hs.H(cell + float2(5.0, Seed)) < Detail * 0.7;
            if (bal && f.y < 0.1) col *= 0.4;
            if (bal && f.y > 0.1 && f.y < 0.3 && f.x > lo.x - 0.05 && f.x < hi.x + 0.05 && frac(u * 6.0) < 0.15) col = float3(0.12, 0.12, 0.12);
        }
        else if (dist == 3) { if (f.y > lo.y && f.y < hi.y && ((f.x > lo.x - 0.1 && f.x < lo.x) || (f.x > hi.x && f.x < hi.x + 0.1))) col = Accent * 0.3 + 0.08; }
        else if (dist == 4) { col *= 1.0 - 0.2 * Detail * step(0.5, frac(u * 3.3)); col = lerp(col, float3(0.35, 0.16, 0.07), saturate(Wear * streak * 1.4) * Detail); }
        else if (dist == 5)
        {
            if (f.y < 0.1) col = lerp(col, float3(0.95, 0.95, 0.92), 0.7 * Detail);
            if (frac(u / (cw * 3.0)) < 0.04) col = lerp(col, Accent * 0.6 + 0.3, 0.5 * Detail);
        }
    }
    float base = saturate(1.0 - (wp.z - 0.15) / 2.5);
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
float night = saturate(Night);
Emis = emWin * night * WinLum + emNeon * NeonLum * (0.1 + 0.9 * night);
return col;
"""

# Sattel-/Sheddaecher: Ziegelreihen world-aligned, Moos/Schmutz ueber Verschleiss (CD 0-2 Farbe, CD 4 Wear).
HLSL_ROOF = HLSL_HASH + """
float3 wp = WP / 100.0;
float3 nn = normalize(N);
float rowi = floor(wp.z * 3.3);
float row = frac(wp.z * 3.3);
float along = abs(nn.x) > abs(nn.y) ? wp.y : wp.x;
float n = Hs.H(float2(floor(along * 4.0 + 0.5 * rowi), rowi));
float3 col = Tint * (0.82 + 0.18 * n) * (row < 0.12 ? 0.7 : 1.0);
col = lerp(col, float3(0.2, 0.22, 0.12), Wear * 0.35 * Hs.H(floor(wp.xy * 0.7)));
Rough = 0.75;
float w = saturate(Wet);
col *= lerp(1.0, 0.7, w);
Rough = lerp(Rough, 0.15, w * 0.8);
return col;
"""

# Wasser (Pixel): Summe gerichteter Sinuswellen mit analytischer Normale (Weltraum), Fresnel-Faerbung,
# Flachwasser + Schaum an der Kuestenlinie (gleiche Formel wie AMTACityGenerator::GetCoastLocalY).
# Eingaben: WP, CamPos, T (Zeit s), Wet, Base (CD 0-2), CoastY (CD 3, Welt cm), OX (CD 4), HS (CD 5), SL (CD 6).
# Ausgaben: return BaseColor, Rough, Nrm (Weltnormale).
HLSL_WATER = """
struct FMTAWave
{
    float h; float2 g;
    void Add(float2 p, float2 d, float len, float amp, float spd, float t)
    {
        float k = 6.2831853 / len;
        float ph = dot(d, p) * k - t * spd * k;
        h += amp * sin(ph);
        g += amp * k * cos(ph) * d;
    }
};
float3 wp = WP / 100.0;
float dist = length(WP - CamPos) / 100.0;
FMTAWave Wv;
Wv.h = 0.0;
Wv.g = float2(0.0, 0.0);
float fine = saturate(1.0 - dist / 400.0);
Wv.Add(wp.xy, normalize(float2(0.35, 1.0)), 67.0, 0.18, 2.6, T);
Wv.Add(wp.xy, normalize(float2(-0.6, 1.0)), 41.0, 0.22, 2.0, T);
Wv.Add(wp.xy, normalize(float2(0.9, 0.4)), 23.0, 0.08, 1.5, T);
Wv.Add(wp.xy, normalize(float2(-0.3, -1.0)), 13.0, 0.05, 1.1, T);
Wv.Add(wp.xy, normalize(float2(0.2, -1.0)), 4.1, 0.025 * fine, 0.9, T);
Wv.Add(wp.xy, normalize(float2(-1.0, 0.3)), 2.3, 0.015 * fine, 0.7, T);
Wv.Add(wp.xy, normalize(float2(0.7, -0.7)), 1.1, 0.006 * fine, 0.5, T);
float fade = saturate(1.0 - dist / 3000.0) * 0.8 + 0.2;
float3 n = normalize(float3(-Wv.g.x * fade, -Wv.g.y * fade, 1.0));
float dx = max(0.0, abs(WP.x - OX) - HS);
float coast = CoastY - SL * dx * dx / (dx + 10000.0);
float d = (coast - WP.y) / 100.0;
float3 v = normalize(CamPos - WP);
float fres = pow(1.0 - saturate(dot(n, v)), 5.0);
float3 shallow = Base * 2.2 + float3(0.0, 0.05, 0.04);
float3 col = lerp(shallow, Base, saturate(d / 60.0));
col = lerp(col, float3(0.25, 0.32, 0.38), fres * 0.35);
float foamN = frac(sin(dot(floor(wp.xy * 2.0 + T * 0.3), float2(12.9898, 78.233))) * 43758.5453);
float foam = saturate(1.0 - d / 4.0) * step(0.0, d + 1.0) * (0.55 + 0.45 * foamN);
foam += saturate(Wv.h * 2.5 - 0.35) * 0.3;
foam = saturate(foam);
col = lerp(col, float3(0.85, 0.88, 0.9), foam);
Rough = lerp(0.035, 0.55, foam) + saturate(Wet) * 0.08;
Nrm = n;
return col;
"""

# Wasser (Vertex, WPO): nur lange Wellen (Kachelabstand 20 m), Amplitude faellt mit der Entfernung auf 0
# (die groben Fernkacheln haben dadurch keine Risse). Eingaben: WP (ohne Shader-Offsets), CamPos, T, Amp.
HLSL_WATER_WPO = """
struct FMTAWaveV
{
    float h;
    void Add(float2 p, float2 d, float len, float amp, float spd, float t)
    {
        float k = 6.2831853 / len;
        h += amp * sin(dot(d, p) * k - t * spd * k);
    }
};
float3 wp = WP / 100.0;
float dist = length(WP - CamPos) / 100.0;
FMTAWaveV Wv;
Wv.h = 0.0;
Wv.Add(wp.xy, normalize(float2(0.35, 1.0)), 67.0, 0.18, 2.6, T);
Wv.Add(wp.xy, normalize(float2(-0.6, 1.0)), 41.0, 0.22, 2.0, T);
float fade = saturate(1.0 - dist / 1200.0);
return float3(0.0, 0.0, Wv.h * 100.0 * fade * Amp);
"""

# Beton/Putz/Sand/Planken/Salz/Erde (Kaimauern, Molen, Promenade, Strand, Landmarken).
# Eingaben: WP, N, Tint (CD 0-2), Wear (CD 3), Pat (CD 4: 0 glatt, 1 Sand, 2 Pflaster, 3 Planken, 4 Salz, 6 Erde), Wet.
HLSL_CONCRETE = HLSL_HASH + """
float3 wp = WP / 100.0;
float3 nn = normalize(N);
int pat = (int)(Pat + 0.5);
float n1 = Hs.H(floor(wp.xy * 3.0) + floor(wp.z * 3.0));
float n2 = Hs.H(floor(wp.xy * 0.35) + 17.0);
float3 col = Tint * (0.9 + 0.12 * n1) * (0.92 + 0.12 * n2);
Rough = 0.85;
bool top = nn.z > 0.5;
if (pat == 1) { col = Tint * (0.88 + 0.2 * Hs.H(floor(wp.xy * 9.0))); Rough = 0.95; }
else if (pat == 2) { float2 g = frac(wp.xy / 0.6); if (top && min(g.x, g.y) < 0.05) col *= 0.72; }
else if (pat == 3)
{
    if (top && frac(wp.x / 0.25) < 0.08) col *= 0.45;
    col *= 0.9 + 0.15 * Hs.H(float2(floor(wp.x / 0.25), floor(wp.y / 3.0)));
    Rough = 0.8;
}
else if (pat == 4)
{
    col = Tint * (0.93 + 0.07 * Hs.H(floor(wp.xy * 0.8)));
    if (frac(wp.x * 0.37 + wp.y * 0.21 + Hs.H(floor(wp.xy * 0.3))) < 0.02) col *= 0.8;
    Rough = 0.6;
}
else if (pat == 6) { col = lerp(Tint, Tint * float3(1.3, 1.1, 0.8), Hs.H(floor(wp.xy * 0.02))); Rough = 1.0; }
float stain = Hs.H(floor(wp.xy * 0.5) + 3.0);
col *= 1.0 - Wear * (top ? 0.35 * stain : 0.15 + 0.15 * saturate(1.0 - frac(wp.z / 3.0)));
float w = saturate(Wet);
col *= lerp(1.0, 0.6, w);
Rough = lerp(Rough, 0.12, w * 0.85);
return col;
"""

# Lackiertes Metall (Kraene, Container, Masten, Moeblierung, Riesenrad). Rost ueber Verschleiss.
# Eingaben: WP, N, Tint (CD 0-2), Wear (CD 3), Met (CD 4), Stripes (CD 5: 1 Wellblech, 2 Warnstreifen,
# 3 rot-weisse Ringe, 4 Schranke), Wet. Ausgaben: return BaseColor, Rough, Metal.
HLSL_METAL = HLSL_HASH + """
float3 wp = WP / 100.0;
float3 nn = normalize(N);
int st = (int)(Stripes + 0.5);
float u = abs(nn.x) > 0.5 ? wp.y : wp.x;
float3 col = Tint;
Metal = Met;
Rough = 0.45;
if (st == 1 && abs(nn.z) < 0.5) col *= 0.82 + 0.18 * step(0.5, frac(u * 3.3));
else if (st == 2) { if (frac((u + wp.z) * 1.5) < 0.5) col = float3(0.03, 0.03, 0.03); }
else if (st == 3) { if (frac(wp.z / 4.0) < 0.5) col = float3(0.85, 0.85, 0.83); }
else if (st == 4) { if (frac((wp.x + wp.y + wp.z) * 1.25) < 0.5) col = float3(0.9, 0.9, 0.9); }
float rn = Hs.H(floor(wp.xy * 1.7) + floor(wp.z * 1.7)) * 0.6 + Hs.H(floor(wp.xy * 0.4) + floor(wp.z * 0.4)) * 0.4;
float rust = saturate((rn - (1.0 - Wear)) * 2.5);
col = lerp(col, float3(0.32, 0.14, 0.06) * (0.8 + 0.4 * rn), rust);
Metal = lerp(Metal, 0.1, rust);
Rough = lerp(Rough, 0.9, rust);
float w = saturate(Wet);
col *= lerp(1.0, 0.75, w);
Rough = lerp(Rough, 0.12, w * 0.7);
return col;
"""

# Neon/Leuchtakzente: Farbe (CD 0-2), Intensitaet (CD 3), Flackern (CD 4, 0..1; 2 = Blinken), Tagesanteil (CD 5).
HLSL_NEON = """
float ph = frac(sin(dot(floor(WP.xy / 50.0), float2(12.9898, 78.233))) * 43758.5453);
float f = 1.0;
if (Flick > 1.5) { f = step(0.5, frac(T * 0.8 + ph)); }
else if (Flick > 0.01)
{
    float s = sin(T * (7.0 + ph * 13.0) + ph * 40.0) * sin(T * (2.3 + ph * 5.0));
    f = lerp(1.0, step(-0.6, s) * (0.75 + 0.25 * s), Flick);
}
float on = lerp(Day, 1.0, saturate(Night));
Emis = Col * Inten * f * on * NeonLum;
return Col * 0.25 + 0.04;
"""

# Schilder/Plakate/Graffiti/Werbung/Aegis-Logo/Tuer: Muster ueber die lokale Position (duenne Quader, X = Normale).
# Eingaben: LP, Col (CD 0-2), Kind (CD 3), SSeed (CD 4), Glow (CD 5), Night, Wet, SignLum.
HLSL_SIGN = HLSL_HASH + """
float2 uv = float2(LP.y / 100.0 + 0.5, LP.z / 100.0 + 0.5);
int k = (int)(Kind + 0.5);
float night = saturate(Night);
float3 col = Col;
Emis = float3(0.0, 0.0, 0.0);
Rough = 0.6;
if (k == 0)
{
    col = lerp(float3(0.92, 0.9, 0.85), Col, step(0.45, uv.y));
    float2 c = uv - float2(0.3 + 0.4 * Hs.H(float2(SSeed, 1.0)), 0.65);
    if (length(c * float2(1.0, 1.4)) < 0.18) col = Col.zxy;
    if (uv.y < 0.35 && frac(uv.y * 14.0) < 0.45 && uv.x > 0.1 && uv.x < 0.9) col = float3(0.1, 0.1, 0.1);
    if (Hs.H(floor(uv * float2(20.0, 12.0)) + SSeed) < 0.06) col *= 0.6;
}
else if (k == 1)
{
    col = float3(0.55, 0.55, 0.52);
    for (int i = 0; i < 5; ++i)
    {
        float fi = (float)i;
        float2 c = float2(Hs.H(float2(SSeed, fi)), 0.35 + 0.3 * Hs.H(float2(fi, SSeed)));
        float r = 0.08 + 0.1 * Hs.H(float2(SSeed + fi, 7.0));
        float dd = length((uv - c) * float2(2.2, 1.0));
        if (dd < r) col = lerp(Col, Col.yzx, Hs.H(float2(fi, 3.0)));
        if (abs(dd - r) < 0.015) col = float3(0.02, 0.02, 0.02);
    }
}
else if (k == 2)
{
    col = lerp(Col, Col * 0.25, uv.y);
    if (length((uv - float2(0.25, 0.55)) * float2(2.4, 1.0)) < 0.28) col = float3(1.0, 1.0, 1.0);
    if (uv.x > 0.5 && uv.x < 0.92 && uv.y > 0.3 && uv.y < 0.75 && frac(uv.y * 8.0) < 0.5) col = float3(0.95, 0.95, 0.9);
    Emis = col * Glow * (0.08 + night);
}
else if (k == 3)
{
    col = float3(0.03, 0.04, 0.06);
    float2 p = (uv - 0.5) * float2(1.0, 1.7);
    if (length(float2(p.x, p.y * 1.8)) < 0.42 && abs(p.y) < 0.24 - p.x * p.x * 0.9) col = Col;
    if (abs(length(p) - 0.32) < 0.025) col = Col;
    if (length(p) < 0.11) col = float3(1.0, 0.1, 0.08);
    Emis = col * Glow * (0.15 + night);
}
else if (k == 4)
{
    col = Col * 0.6;
    if (uv.x > 0.06 && uv.x < 0.94 && uv.y > 0.15 && uv.y < 0.85) col = Col;
    Emis = Col * Glow * (0.05 + night);
}
else if (k == 5)
{
    col = float3(0.05, 0.06, 0.07);
    Rough = 0.1;
    if (uv.x < 0.05 || uv.x > 0.95 || uv.y > 0.96) col = float3(0.25, 0.25, 0.26);
    if (abs(uv.x - 0.82) < 0.02 && abs(uv.y - 0.45) < 0.06) col = float3(0.8, 0.8, 0.8);
    if (uv.x > 0.1 && uv.x < 0.9 && uv.y > 0.15 && uv.y < 0.9) Emis = float3(1.0, 0.8, 0.55) * 0.25 * Glow * night;
}
else if (k == 6)
{
    col = Col;
    if (uv.x < 0.04 || uv.x > 0.96 || uv.y < 0.1 || uv.y > 0.9) col = float3(0.9, 0.9, 0.9);
    if (abs(uv.y - 0.5) < 0.15 && uv.x > 0.1 && uv.x < 0.9 && frac(uv.x * 12.0) < 0.6) col = float3(0.95, 0.95, 0.95);
}
else if (k == 7)
{
    col = Col;
    float2 p = uv - float2(0.5, 0.42);
    if (p.y > -0.3 && abs(p.x) < (0.35 - (p.y + 0.3) * 0.55)) col = float3(0.95, 0.95, 0.95);
    if (length(uv - float2(0.5, 0.55)) < 0.06) col = float3(0.05, 0.05, 0.05);
    Emis = col * Glow * 0.2 * night;
}
else if (k == 8)
{
    col = Col;
    if (uv.x < 0.08 || uv.x > 0.92 || uv.y < 0.08 || uv.y > 0.92) col = float3(0.9, 0.9, 0.9);
    if (abs(uv.y - 0.5) < 0.1 && uv.x > 0.15 && uv.x < 0.85) col = float3(0.95, 0.95, 0.95);
}
else if (k == 9)
{
    col = Col;
    if (frac((uv.x + uv.y) * 6.0) < 0.5 && (uv.y < 0.15 || uv.y > 0.85)) col = float3(0.03, 0.03, 0.03);
    if (abs(uv.y - 0.5) < 0.18 && uv.x > 0.08 && uv.x < 0.92 && frac(uv.x * 14.0) < 0.6) col = float3(0.05, 0.05, 0.05);
    Emis = Col * Glow * 0.3 * night;
}
else
{
    col = Col * (0.85 + 0.2 * step(0.5, frac(uv.y * 6.0)));
    Rough = 0.9;
}
Rough = lerp(Rough, 0.15, saturate(Wet) * 0.7);
Emis *= SignLum;
return col;
"""

# Berge: Gras/Fels nach Hangneigung, Schnee oberhalb der Schneegrenze, Dunst nach Entfernung und Hoehe
# (Talnebel), nachts dunkler Dunst. Eingaben: WP, N, CamPos, Tint (CD 0-2), Snow (CD 3, m), Haze (CD 4), Night, Wet.
HLSL_MOUNTAIN = HLSL_HASH + """
float3 wp = WP / 100.0;
float3 nn = normalize(N);
float slope = 1.0 - saturate(nn.z);
float n = Hs.H(floor(wp.xy / 25.0)) * 0.5 + Hs.H(floor(wp.xy / 7.0) + 3.0) * 0.3 + Hs.H(floor(wp.zz / 4.0) + 9.0) * 0.2;
float3 grass = float3(0.16, 0.22, 0.1);
float3 rock = float3(0.33, 0.31, 0.28);
float rockMask = saturate((slope - 0.3) * 3.0 + (n - 0.5) * 0.8 + saturate((wp.z - 250.0) / 200.0));
float3 col = lerp(grass, rock, rockMask) * Tint * (0.85 + 0.3 * n);
float snowMask = saturate((wp.z - Snow) / 40.0 + (n - 0.5)) * saturate(1.2 - slope * 1.2);
col = lerp(col, float3(0.85, 0.87, 0.9), snowMask);
float dist = length(WP - CamPos) / 100.0;
float haze = saturate(dist / 3000.0) * Haze * (0.55 + 0.45 * saturate(1.0 - wp.z / 600.0));
float3 hazeCol = lerp(float3(0.58, 0.66, 0.76), float3(0.03, 0.04, 0.06), saturate(Night));
col = lerp(col, hazeCol, saturate(haze));
Rough = lerp(0.95, 0.6, snowMask);
col *= lerp(1.0, 0.75, saturate(Wet));
return col;
"""

# Kuestenstrasse/Route 9: Markierungen ueber die lokale Position des Segments (Laenge/Breite per Custom-Data).
# Eingaben: WP, LP, Len (CD 3, cm), Wid (CD 4, cm), Kind (CD 5: 0 Kuestenstrasse, 1 Schnellstrasse), Wet.
HLSL_ROAD = HLSL_HASH + """
float along = (LP.x / 100.0) * Len / 100.0;
float across = (LP.y / 100.0) * Wid / 100.0;
float hw = Wid / 200.0;
float n = Hs.H(floor(WP.xy / 40.0));
float3 col = float3(0.12, 0.12, 0.13) * (0.88 + 0.22 * n);
Rough = 0.8;
if (Kind < 0.5)
{
    if (abs(across) < 0.08 && frac(along / 6.0) < 0.5) col = float3(0.85, 0.65, 0.1);
}
else
{
    if (abs(abs(across) - hw * 0.5) < 0.08 && frac(along / 9.0) < 0.4) col = float3(0.8, 0.8, 0.8);
    if (abs(across) < 0.12) col = float3(0.85, 0.65, 0.1);
}
if (abs(abs(across) - (hw - 0.3)) < 0.08) col = float3(0.78, 0.78, 0.78);
float w = saturate(Wet);
col *= lerp(1.0, 0.55, w);
Rough = lerp(Rough, 0.1, w);
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


# Wildnis-Gelaende (AMTAWildernessBuilder, ProceduralMesh): Grundfarbe aus Vertex-Farbe (Waldboden, Moos,
# Schotter, Erde, Flusskies), Fels nach Hangneigung, Variation world-aligned, Naesse ueber MPC.
# Eingaben: WP, N, VC (Vertex-Farbe RGB), VA (Vertex-Alpha: 1 = Weg, dort kein Fels), Wet.
HLSL_TERRAIN = HLSL_HASH + """
float3 wp = WP / 100.0;
float3 nn = normalize(N);
float slope = 1.0 - saturate(nn.z);
float n = Hs.H(floor(wp.xy * 1.3)) * 0.35 + Hs.H(floor(wp.xy * 0.22) + 5.0) * 0.4 + Hs.H(floor(wp.xy * 4.0) + 9.0) * 0.25;
float3 col = VC * (0.82 + 0.36 * n);
float3 rock = float3(0.30, 0.29, 0.27) * (0.8 + 0.4 * Hs.H(floor(wp.xy * 0.7) + floor(wp.z * 0.5)));
float rockMask = saturate((slope - 0.45) * 4.0 + (n - 0.5) * 0.6) * (1.0 - saturate(VA));
col = lerp(col, rock, rockMask);
Rough = lerp(0.95, 0.75, rockMask);
float w = saturate(Wet);
col *= lerp(1.0, 0.65, w);
Rough = lerp(Rough, 0.2, w * 0.8);
return col;
"""

# Wasserfall (unlit, transluzent, zweiseitig): Wasserbaender (Kind 0), Gischt-Kugeln (Kind 1), Flaechen (Kind 2).
# Fliessen ueber Zeit (Panner + lokale Position), Fresnel an den Raendern, Emissive statt Beleuchtung.
# Custom-Data wie im Builder: CD 0-2 Farbe, CD 3 Deckkraft, CD 4 Geschwindigkeit, CD 5 Art, CD 6 Phase.
# Eingaben: LP (lokal, -50..50), PUV (Panner), WP, N, CamPos, T, Tint, Opac, Speed, Kind, Phase, Night, Lum.
HLSL_WATERFALL = """
float3 nn = normalize(N);
float3 v = normalize(CamPos - WP);
float fres = pow(1.0 - saturate(abs(dot(nn, v))), 3.0);
int kind = (int)(Kind + 0.5);
float across = LP.y / 100.0 + 0.5;
float along = LP.z / 100.0 + 0.5 + T * Speed * 0.8 + Phase;
float fine = frac(sin(dot(floor(PUV * float2(23.0, 9.0)), float2(12.9898, 78.233))) * 43758.5453);
float3 col = Tint;
float a = 0.0;
if (kind == 1)
{
    a = Opac * (1.0 - fres) * (1.0 - fres);
    col = Tint * 1.1;
}
else
{
    float s1 = frac(sin(floor(across * 9.0) * 12.9898 + Phase * 7.0) * 43758.5453);
    float st = frac(along * (1.3 + s1) + s1 * 3.0);
    float streak = smoothstep(0.0, 0.25, st) * smoothstep(1.0, 0.55, st);
    float edge = smoothstep(0.0, 0.12, across) * smoothstep(1.0, 0.88, across);
    a = Opac * edge * saturate(0.35 + 0.55 * streak + 0.25 * fine);
    col = lerp(Tint * 0.8, float3(0.95, 0.98, 1.0), streak * 0.6 + fres * 0.4);
    if (kind == 2) { a = Opac * (0.6 + 0.3 * fine); }
}
Opacity = saturate(a);
return col * (0.6 + 0.4 * saturate(1.0 - Night * 0.7)) * Lum;
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

    def world_pos_no_offsets(self):
        """Weltposition ohne Shader-Offsets (Pflicht fuer Eingaben der World Position Offset)."""
        expr = self.world_pos()
        if expr:
            try:
                value = enum_value(unreal.WorldPositionIncludedOffsets, "WPT_EXCLUDE_ALL_SHADER_OFFSETS",
                                   "WPT_ExcludeAllShaderOffsets")
                set_prop(expr, "world_position_shader_offset", value)
            except AttributeError as exc:
                warn("WorldPositionIncludedOffsets nicht aufloesbar: %s" % exc)
        return expr

    def local_pos(self):
        """Lokale (Instanz-)Position, Engine-Grundkoerper: -50..50 cm je Achse."""
        return self.node(unreal.MaterialExpressionLocalPosition)

    def camera_pos(self):
        return self.node(unreal.MaterialExpressionCameraPositionWS)

    def time(self):
        return self.node(unreal.MaterialExpressionTime)

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
        # Eingaenge: (Name, Ausdruck) oder (Name, Ausdruck, Ausgangsname des Ausdrucks, z. B. "A").
        inputs = [item if len(item) == 3 else (item[0], item[1], "") for item in inputs]
        custom_inputs = []
        for name, _src, _out in inputs:
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
        for name, src, src_out in inputs:
            if src is None:
                self.ok = False
                continue
            if not MEL.connect_material_expressions(src, src_out, expr, name):
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
              ("WinLum", g.scalar_param("WindowLuminance", 40.0)),
              # Bezirks-Look (MTABuildingData 8..13 in MTACityGenerator.h)
              ("District", g.custom_data(8, 0.0)), ("Neon", g.custom_data(9, 0.0)),
              ("Detail", g.custom_data(10, 0.5)), ("Accent", g.custom_data3(11, (1.0, 0.3, 0.6))),
              ("NeonLum", g.scalar_param("NeonLuminance", 15.0))]
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


def build_roof(mpc):
    mat = begin_material("M_MTA_Roof")
    if mat is None:
        return False
    g = Graph(mat)
    inputs = [("WP", g.world_pos()), ("N", g.normal_ws()),
              ("Tint", g.custom_data3(0, (0.5, 0.25, 0.15))), ("Wear", g.custom_data(4, 0.3)),
              ("Wet", g.mpc_param(mpc, "Wetness", 0.0))]
    c = g.custom("MTA Roof", HLSL_ROOF, 3, inputs, [("Rough", 1)])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(c, "Rough", "MP_ROUGHNESS")
    return finish_material(mat, g)


# Custom-Data-Indizes der Welt-Details: MTADetailData in Source/MTA6/MTAWorldDetail.h (7 Werte je Instanz).
def build_water(mpc):
    """Opakes Wasser: Lumen-Reflexionen/SSR statt teurer Transluzenz (RX 5700 XT), Wellen per WPO."""
    mat = begin_material("M_MTA_Water")
    if mat is None:
        return False
    # Pixel-Normale wird im Weltraum berechnet.
    set_prop(mat, "tangent_space_normal", False)
    g = Graph(mat)
    inputs = [("WP", g.world_pos()), ("CamPos", g.camera_pos()), ("T", g.time()),
              ("Wet", g.mpc_param(mpc, "Wetness", 0.0)),
              ("Base", g.custom_data3(0, (0.02, 0.07, 0.08))),
              ("CoastY", g.custom_data(3, -40000.0)), ("OX", g.custom_data(4, 0.0)),
              ("HS", g.custom_data(5, 52000.0)), ("SL", g.custom_data(6, 0.55))]
    c = g.custom("MTA Water", HLSL_WATER, 3, inputs, [("Rough", 1), ("Nrm", 3)])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(c, "Rough", "MP_ROUGHNESS")
    g.out(c, "Nrm", "MP_NORMAL")
    g.out(g.scalar_param("Specular", 0.35), "", "MP_SPECULAR")
    wpo_inputs = [("WP", g.world_pos_no_offsets()), ("CamPos", g.camera_pos()), ("T", g.time()),
                  ("Amp", g.scalar_param("WaveAmplitude", 1.0))]
    w = g.custom("MTA Water WPO", HLSL_WATER_WPO, 3, wpo_inputs, [])
    g.out(w, "", "MP_WORLD_POSITION_OFFSET")
    return finish_material(mat, g)


def build_concrete(mpc):
    mat = begin_material("M_MTA_Concrete")
    if mat is None:
        return False
    g = Graph(mat)
    inputs = [("WP", g.world_pos()), ("N", g.normal_ws()),
              ("Tint", g.custom_data3(0, (0.5, 0.49, 0.46))), ("Wear", g.custom_data(3, 0.4)),
              ("Pat", g.custom_data(4, 0.0)), ("Wet", g.mpc_param(mpc, "Wetness", 0.0))]
    c = g.custom("MTA Concrete", HLSL_CONCRETE, 3, inputs, [("Rough", 1)])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(c, "Rough", "MP_ROUGHNESS")
    return finish_material(mat, g)


def build_painted_metal(mpc):
    mat = begin_material("M_MTA_PaintedMetal")
    if mat is None:
        return False
    g = Graph(mat)
    inputs = [("WP", g.world_pos()), ("N", g.normal_ws()),
              ("Tint", g.custom_data3(0, (0.5, 0.5, 0.5))), ("Wear", g.custom_data(3, 0.3)),
              ("Met", g.custom_data(4, 0.5)), ("Stripes", g.custom_data(5, 0.0)),
              ("Wet", g.mpc_param(mpc, "Wetness", 0.0))]
    c = g.custom("MTA Painted Metal", HLSL_METAL, 3, inputs, [("Rough", 1), ("Metal", 1)])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(c, "Rough", "MP_ROUGHNESS")
    g.out(c, "Metal", "MP_METALLIC")
    return finish_material(mat, g)


def build_neon(mpc):
    mat = begin_material("M_MTA_Neon")
    if mat is None:
        return False
    g = Graph(mat)
    inputs = [("WP", g.world_pos()), ("T", g.time()),
              ("Col", g.custom_data3(0, (1.0, 0.2, 0.6))), ("Inten", g.custom_data(3, 5.0)),
              ("Flick", g.custom_data(4, 0.0)), ("Day", g.custom_data(5, 0.1)),
              ("Night", g.mpc_param(mpc, "Night", 1.0)), ("NeonLum", g.scalar_param("NeonLuminance", 15.0))]
    c = g.custom("MTA Neon", HLSL_NEON, 3, inputs, [("Emis", 3)])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(c, "Emis", "MP_EMISSIVE_COLOR")
    g.out(g.scalar_param("Roughness", 0.35), "", "MP_ROUGHNESS")
    return finish_material(mat, g)


def build_signage(mpc):
    mat = begin_material("M_MTA_Signage")
    if mat is None:
        return False
    g = Graph(mat)
    inputs = [("LP", g.local_pos()),
              ("Col", g.custom_data3(0, (0.8, 0.2, 0.2))), ("Kind", g.custom_data(3, 0.0)),
              ("SSeed", g.custom_data(4, 0.0)), ("Glow", g.custom_data(5, 0.0)),
              ("Night", g.mpc_param(mpc, "Night", 0.0)), ("Wet", g.mpc_param(mpc, "Wetness", 0.0)),
              ("SignLum", g.scalar_param("SignLuminance", 12.0))]
    c = g.custom("MTA Signage", HLSL_SIGN, 3, inputs, [("Rough", 1), ("Emis", 3)])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(c, "Rough", "MP_ROUGHNESS")
    g.out(c, "Emis", "MP_EMISSIVE_COLOR")
    return finish_material(mat, g)


def build_mountain(mpc):
    mat = begin_material("M_MTA_Mountain")
    if mat is None:
        return False
    g = Graph(mat)
    inputs = [("WP", g.world_pos()), ("N", g.normal_ws()), ("CamPos", g.camera_pos()),
              ("Tint", g.custom_data3(0, (0.9, 0.95, 0.85))), ("Snow", g.custom_data(3, 380.0)),
              ("Haze", g.custom_data(4, 0.6)),
              ("Night", g.mpc_param(mpc, "Night", 0.0)), ("Wet", g.mpc_param(mpc, "Wetness", 0.0))]
    c = g.custom("MTA Mountain", HLSL_MOUNTAIN, 3, inputs, [("Rough", 1)])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(c, "Rough", "MP_ROUGHNESS")
    return finish_material(mat, g)


def build_road(mpc):
    mat = begin_material("M_MTA_Road")
    if mat is None:
        return False
    g = Graph(mat)
    inputs = [("WP", g.world_pos()), ("LP", g.local_pos()),
              ("Len", g.custom_data(3, 4000.0)), ("Wid", g.custom_data(4, 1400.0)),
              ("Kind", g.custom_data(5, 0.0)), ("Wet", g.mpc_param(mpc, "Wetness", 0.0))]
    c = g.custom("MTA Road", HLSL_ROAD, 3, inputs, [("Rough", 1)])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(c, "Rough", "MP_ROUGHNESS")
    return finish_material(mat, g)


def build_terrain(mpc):
    """Gelaende der Wildnis (ProceduralMesh, kein Instancing): Vertex-Farbe + Hangneigung."""
    mat = begin_material("M_MTA_Terrain", instanced=False)
    if mat is None:
        return False
    g = Graph(mat)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    inputs = [("WP", g.world_pos()), ("N", g.normal_ws()), ("VC", vc), ("VA", vc, "A"),
              ("Wet", g.mpc_param(mpc, "Wetness", 0.0))]
    c = g.custom("MTA Terrain", HLSL_TERRAIN, 3, inputs, [("Rough", 1)])
    g.out(c, "", "MP_BASE_COLOR")
    g.out(c, "Rough", "MP_ROUGHNESS")
    return finish_material(mat, g)


def build_waterfall(mpc):
    """Wasserfall: transluzent, unlit, zweiseitig, Panner + Fresnel + Emissive (Custom-Data wie AMTAWildernessBuilder)."""
    mat = begin_material("M_MTA_Waterfall")
    if mat is None:
        return False
    set_prop(mat, "blend_mode", enum_value(unreal.BlendMode, "BLEND_TRANSLUCENT", "BLEND_Translucent"))
    set_prop(mat, "shading_model", enum_value(unreal.MaterialShadingModel, "MSM_UNLIT", "MSM_Unlit"))
    set_prop(mat, "two_sided", True)
    g = Graph(mat)
    panner = g.node(unreal.MaterialExpressionPanner)
    if panner:
        set_prop(panner, "speed_x", 0.0)
        set_prop(panner, "speed_y", 1.2)
    inputs = [("LP", g.local_pos()), ("PUV", panner), ("WP", g.world_pos()), ("N", g.normal_ws()),
              ("CamPos", g.camera_pos()), ("T", g.time()),
              ("Tint", g.custom_data3(0, (0.75, 0.88, 0.95))), ("Opac", g.custom_data(3, 0.7)),
              ("Speed", g.custom_data(4, 1.0)), ("Kind", g.custom_data(5, 0.0)), ("Phase", g.custom_data(6, 0.0)),
              ("Night", g.mpc_param(mpc, "Night", 0.0)), ("Lum", g.scalar_param("WaterfallLuminance", 1.2))]
    c = g.custom("MTA Waterfall", HLSL_WATERFALL, 3, inputs, [("Opacity", 1)])
    g.out(c, "", "MP_EMISSIVE_COLOR")
    g.out(c, "Opacity", "MP_OPACITY")
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
        # Welt-Details (AMTAWorldDetail) und Daecher des Generators
        "M_MTA_Roof": build_roof(mpc),
        "M_MTA_Water": build_water(mpc),
        "M_MTA_Concrete": build_concrete(mpc),
        "M_MTA_PaintedMetal": build_painted_metal(mpc),
        "M_MTA_Neon": build_neon(mpc),
        "M_MTA_Signage": build_signage(mpc),
        "M_MTA_Mountain": build_mountain(mpc),
        "M_MTA_Road": build_road(mpc),
        # Wildnis Verdanta (AMTAWildernessBuilder)
        "M_MTA_Terrain": build_terrain(mpc),
        "M_MTA_Waterfall": build_waterfall(mpc),
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
