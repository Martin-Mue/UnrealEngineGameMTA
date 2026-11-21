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
			break;

		default:
			P.DisplayName = LOCTEXT("Unknown", "Unbekannt");
			P.Palette = { FLinearColor(0.6f, 0.6f, 0.6f) };
			break;
		}
		return P;
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
