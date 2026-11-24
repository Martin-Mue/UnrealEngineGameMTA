#include "MTADistrict.h"

#define LOCTEXT_NAMESPACE "MTADistrict"

namespace
{
	/** sRGB-Hexfarbe (z. B. 0xC8742E) in lineare Farbe umrechnen; Paletten sind so lesbarer. */
	FLinearColor Hex(uint32 RGB)
	{
		return FLinearColor(FColor((RGB >> 16) & 0xFF, (RGB >> 8) & 0xFF, RGB & 0xFF, 0xFF));
	}
}

namespace MTADistrict
{
	FMTADistrictProfile MakeDefaultProfile(EMTADistrict District)
	{
		FMTADistrictProfile P;
		switch (District)
		{
		case EMTADistrict::Core:
			// Konzernviertel: Glas/Stahl, 60-180 m, sauber, kaltweisses Licht, viel Beleuchtung.
			P.DisplayName = LOCTEXT("Core", "Kestrel Core");
			P.MinHeight = 6000.f; P.MaxHeight = 18000.f; P.HeightBias = 0.8f; P.DowntownInfluence = 1.f;
			P.LotSplitWeights = FVector4(3.0, 2.0, 1.0, 0.0);
			P.EmptyLotChance = 0.f; P.ParkChance = 0.08f; P.IndustrialChance = 0.f; P.StreetTreesPerBlock = 4;
			P.PrimaryStyle = EMTABuildingStyle::GlassSteel;
			P.LeadColor = Hex(0x4A6E96);
			P.Palette = { Hex(0x5B7A9C), Hex(0xC9D3DE), Hex(0x8A9BAE), Hex(0x3E4F63), Hex(0xE6EBF0) };
			P.FoliageColor = Hex(0x3C6B2E);
			P.Wear = 0.05f; P.LitWindowRatio = 0.6f;
			P.LampColor = FLinearColor(0.8f, 0.9f, 1.f); P.LampDensity = 1.f;
			// Look 2: gestufte Glastuerme mit Lichtkrone, kaltes Blau-Weiss, maximale Aegis-Dichte.
			P.HeightBias = 0.7f;
			P.RoofStyle = EMTARoofStyle::Setback; P.PitchedRoofChance = 0.f; P.RoofColor = Hex(0x2E3640);
			P.AccentColor = FLinearColor(0.35f, 0.75f, 1.f); P.NeonShare = 0.3f; P.FacadeDetail = 0.7f; P.RooftopClutter = 0.25f;
			P.AegisCameraDensity = 1.f; P.StreetFurnitureDensity = 0.9f; P.TrafficLightShare = 1.f; P.GraffitiShare = 0.04f; P.BillboardShare = 0.08f;
			break;

		case EMTADistrict::Saltmarket:
			// Alter Hafen: Lagerhaeuser, Rostorange/Gruen, Natriumlicht, Brachen.
			P.DisplayName = LOCTEXT("Saltmarket", "Saltmarket");
			P.MinHeight = 800.f; P.MaxHeight = 2400.f; P.HeightBias = 1.2f; P.DowntownInfluence = 0.2f;
			P.LotSplitWeights = FVector4(1.0, 3.0, 3.0, 1.0);
			P.EmptyLotChance = 0.1f; P.ParkChance = 0.05f; P.IndustrialChance = 0.35f; P.StreetTreesPerBlock = 0;
			P.PrimaryStyle = EMTABuildingStyle::Plaster;
			P.LeadColor = Hex(0xB5552A);
			P.Palette = { Hex(0xB5552A), Hex(0x5E7A5A), Hex(0x8C4A32), Hex(0x9A8F7E), Hex(0x6B6255) };
			P.FoliageColor = Hex(0x4A5E2C);
			P.Wear = 0.7f; P.LitWindowRatio = 0.3f;
			P.LampColor = FLinearColor(1.f, 0.6f, 0.25f); P.LampDensity = 0.7f;
			// Look 2: Ziegel-Lagerhaeuser mit flachen Satteldaechern, Kameras nur an Toren.
			P.RoofStyle = EMTARoofStyle::Gable; P.PitchedRoofChance = 0.35f; P.RoofColor = Hex(0x5A3A2C);
			P.AccentColor = FLinearColor(1.f, 0.45f, 0.1f); P.NeonShare = 0.15f; P.FacadeDetail = 0.8f; P.RooftopClutter = 0.4f;
			P.AegisCameraDensity = 0.2f; P.StreetFurnitureDensity = 0.45f; P.TrafficLightShare = 0.7f; P.GraffitiShare = 0.35f; P.BillboardShare = 0.12f;
			break;

		case EMTADistrict::LanternHill:
			// Altstadt: 3-6 Geschosse, Terrakotta/Ocker, dicht, warmes Licht, viel Nachtleben.
			P.DisplayName = LOCTEXT("LanternHill", "Lantern Hill");
			P.MinHeight = 1000.f; P.MaxHeight = 2000.f; P.HeightBias = 1.f; P.DowntownInfluence = 0.1f;
			P.LotSplitWeights = FVector4(0.0, 1.0, 3.0, 4.0);
			P.EmptyLotChance = 0.05f; P.ParkChance = 0.1f; P.IndustrialChance = 0.f; P.StreetTreesPerBlock = 2;
			P.PrimaryStyle = EMTABuildingStyle::Plaster;
			P.LeadColor = Hex(0xD19A3C);
			P.Palette = { Hex(0xC4673E), Hex(0xD19A3C), Hex(0xE3C98F), Hex(0xB0553A), Hex(0xEADBC0) };
			P.FoliageColor = Hex(0x3F6A2A);
			P.Wear = 0.45f; P.LitWindowRatio = 0.65f;
			P.LampColor = FLinearColor(1.f, 0.75f, 0.45f); P.LampDensity = 1.f;
			// Look 2: Terrakotta-Satteldaecher, Balkone, Antennenwald, viel warmes Neon, Kameras gehackt.
			P.LotSplitWeights = FVector4(0.0, 0.5, 2.0, 5.0);
			P.RoofStyle = EMTARoofStyle::Gable; P.PitchedRoofChance = 0.55f; P.RoofColor = Hex(0x9C4A2E);
			P.AccentColor = FLinearColor(1.f, 0.55f, 0.2f); P.NeonShare = 0.55f; P.FacadeDetail = 0.9f; P.RooftopClutter = 0.9f;
			P.AegisCameraDensity = 0.35f; P.StreetFurnitureDensity = 0.6f; P.TrafficLightShare = 0.5f; P.GraffitiShare = 0.5f; P.BillboardShare = 0.05f;
			break;

		case EMTADistrict::Greenmoor:
			// Gated Vorstadt: Villen, Pastell, viele Gaerten und Baeume, wenig Dichte.
			P.DisplayName = LOCTEXT("Greenmoor", "Greenmoor");
			P.MinHeight = 500.f; P.MaxHeight = 900.f; P.HeightBias = 1.5f; P.DowntownInfluence = 0.f;
			P.LotSplitWeights = FVector4(0.0, 1.0, 2.0, 4.0);
			P.EmptyLotChance = 0.35f; P.ParkChance = 0.25f; P.IndustrialChance = 0.f; P.StreetTreesPerBlock = 10;
			P.PrimaryStyle = EMTABuildingStyle::Villa;
			P.LeadColor = Hex(0x78B48A);
			P.Palette = { Hex(0xF2E6D8), Hex(0xCFE3D2), Hex(0xE8D5E0), Hex(0xF5F0DC), Hex(0xD5E0EA) };
			P.FoliageColor = Hex(0x2F7A2A);
			P.Wear = 0.05f; P.LitWindowRatio = 0.35f;
			P.LampColor = FLinearColor(1.f, 0.9f, 0.75f); P.LampDensity = 0.8f;
			// Look 2: Villen mit Schieferdach, Fensterlaeden, kein Neon, private Kameras an Toren.
			P.RoofStyle = EMTARoofStyle::Gable; P.PitchedRoofChance = 1.f; P.RoofColor = Hex(0x3C4650);
			P.AccentColor = FLinearColor(0.5f, 1.f, 0.6f); P.NeonShare = 0.f; P.FacadeDetail = 0.8f; P.RooftopClutter = 0.05f;
			P.AegisCameraDensity = 0.45f; P.StreetFurnitureDensity = 0.35f; P.TrafficLightShare = 0.2f; P.GraffitiShare = 0.f; P.BillboardShare = 0.f;
			break;

		case EMTADistrict::Rustline:
			// Industrie/Schrott: grosse Hallen, Schornsteine, Natriumorange, dunkel, verschlissen.
			P.DisplayName = LOCTEXT("Rustline", "Rustline");
			P.MinHeight = 800.f; P.MaxHeight = 1800.f; P.HeightBias = 1.5f; P.DowntownInfluence = 0.f;
			P.LotSplitWeights = FVector4(3.0, 2.0, 1.0, 0.0);
			P.EmptyLotChance = 0.2f; P.ParkChance = 0.02f; P.IndustrialChance = 0.65f; P.StreetTreesPerBlock = 0;
			P.PrimaryStyle = EMTABuildingStyle::Industrial;
			P.LeadColor = Hex(0x8C7356);
			P.Palette = { Hex(0x7A7268), Hex(0x8C5A3A), Hex(0x5F5F5C), Hex(0x9C8B74), Hex(0x4E4A44) };
			P.FoliageColor = Hex(0x4B5A2A);
			P.Wear = 0.9f; P.LitWindowRatio = 0.15f;
			P.LampColor = FLinearColor(1.f, 0.55f, 0.2f); P.LampDensity = 0.5f;
			// Look 2: Sheddaecher, Wellblech-Rippen, Rost, flackerndes Natrium-Neon, Aegis fast blind.
			P.RoofStyle = EMTARoofStyle::Sawtooth; P.PitchedRoofChance = 0.f; P.RoofColor = Hex(0x5C4A3C);
			P.AccentColor = FLinearColor(1.f, 0.4f, 0.05f); P.NeonShare = 0.1f; P.FacadeDetail = 0.9f; P.RooftopClutter = 0.5f;
			P.AegisCameraDensity = 0.06f; P.StreetFurnitureDensity = 0.2f; P.TrafficLightShare = 0.35f; P.GraffitiShare = 0.7f; P.BillboardShare = 0.15f;
			break;

		case EMTADistrict::Sunstrip:
			// Promenade: Pastell Tuerkis/Pink, Hotels mittlerer Hoehe, Palmen, viel Licht.
			P.DisplayName = LOCTEXT("Sunstrip", "Sunstrip");
			P.MinHeight = 1200.f; P.MaxHeight = 4500.f; P.HeightBias = 1.2f; P.DowntownInfluence = 0.3f;
			P.LotSplitWeights = FVector4(1.0, 2.0, 2.0, 0.0);
			P.EmptyLotChance = 0.05f; P.ParkChance = 0.2f; P.IndustrialChance = 0.f; P.StreetTreesPerBlock = 6;
			P.PrimaryStyle = EMTABuildingStyle::Plaster;
			P.LeadColor = Hex(0x3BC0C0);
			P.Palette = { Hex(0x6FD3CF), Hex(0xF2A7C3), Hex(0xF7F2E8), Hex(0xFFD6A5), Hex(0x9AD9E8) };
			P.FoliageColor = Hex(0x5E7F22);
			P.Wear = 0.2f; P.LitWindowRatio = 0.55f;
			P.LampColor = FLinearColor(1.f, 0.8f, 0.7f); P.LampDensity = 1.f;
			// Look 2: Art-Deco-Baender, Pastell, sehr viel Pink/Tuerkis-Neon, Werbung allgegenwaertig.
			P.RoofStyle = EMTARoofStyle::Flat; P.PitchedRoofChance = 0.f; P.RoofColor = Hex(0xE8E0D0);
			P.AccentColor = FLinearColor(1.f, 0.2f, 0.65f); P.NeonShare = 0.8f; P.FacadeDetail = 0.8f; P.RooftopClutter = 0.2f;
			P.AegisCameraDensity = 0.75f; P.StreetFurnitureDensity = 0.8f; P.TrafficLightShare = 0.9f; P.GraffitiShare = 0.12f; P.BillboardShare = 0.45f;
			break;

		default:
			P.DisplayName = LOCTEXT("Unknown", "Unbekannt");
			P.Palette = { FLinearColor(0.6f, 0.6f, 0.6f) };
			break;
		}
		P.LookVersion = CurrentLookVersion;
		return P;
	}

	void CopyLookFields(const FMTADistrictProfile& Source, FMTADistrictProfile& Target)
	{
		Target.RoofStyle = Source.RoofStyle;
		Target.PitchedRoofChance = Source.PitchedRoofChance;
		Target.RoofColor = Source.RoofColor;
		Target.AccentColor = Source.AccentColor;
		Target.NeonShare = Source.NeonShare;
		Target.FacadeDetail = Source.FacadeDetail;
		Target.RooftopClutter = Source.RooftopClutter;
		Target.AegisCameraDensity = Source.AegisCameraDensity;
		Target.StreetFurnitureDensity = Source.StreetFurnitureDensity;
		Target.TrafficLightShare = Source.TrafficLightShare;
		Target.GraffitiShare = Source.GraffitiShare;
		Target.BillboardShare = Source.BillboardShare;
		Target.LookVersion = Source.LookVersion;
	}

	const TCHAR* ShortName(EMTADistrict District)
	{
		switch (District)
		{
		case EMTADistrict::Core:        return TEXT("Core");
		case EMTADistrict::Saltmarket:  return TEXT("Saltmarket");
		case EMTADistrict::LanternHill: return TEXT("LanternHill");
		case EMTADistrict::Greenmoor:   return TEXT("Greenmoor");
		case EMTADistrict::Rustline:    return TEXT("Rustline");
		case EMTADistrict::Sunstrip:    return TEXT("Sunstrip");
		default:                        return TEXT("Unknown");
		}
	}

	EMTADistrict ClassifyNormalized(float U, float V)
	{
		// Grobe Umsetzung der Schemakarte aus Kapitel 8.1 (Meer West, Rustline Sued, Core Mitte).
		if (V < 0.25f)
		{
			return EMTADistrict::Rustline;
		}
		if (U < 0.25f)
		{
			return V >= 0.65f ? EMTADistrict::Sunstrip : EMTADistrict::Saltmarket;
		}
		if (V >= 0.7f)
		{
			return U < 0.6f ? EMTADistrict::LanternHill : EMTADistrict::Greenmoor;
		}
		if (U >= 0.75f)
		{
			return V >= 0.45f ? EMTADistrict::Greenmoor : EMTADistrict::Rustline;
		}
		return EMTADistrict::Core;
	}
}

#undef LOCTEXT_NAMESPACE
