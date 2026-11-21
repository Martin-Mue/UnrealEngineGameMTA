#include "MTAInteriorTypes.h"

namespace MTAInterior
{
	FName EntranceKindToName(EMTAEntranceKind Kind)
	{
		switch (Kind)
		{
		case EMTAEntranceKind::Back:      return TEXT("back");
		case EMTAEntranceKind::Roof:      return TEXT("roof");
		case EMTAEntranceKind::Emergency: return TEXT("emergency");
		case EMTAEntranceKind::Garage:    return TEXT("garage");
		default:                          return TEXT("front");
		}
	}

	bool EntranceKindFromName(FName Name, EMTAEntranceKind& OutKind)
	{
		static const TPair<FName, EMTAEntranceKind> Map[] = {
			{ TEXT("front"), EMTAEntranceKind::Front }, { TEXT("back"), EMTAEntranceKind::Back },
			{ TEXT("roof"), EMTAEntranceKind::Roof }, { TEXT("emergency"), EMTAEntranceKind::Emergency },
			{ TEXT("garage"), EMTAEntranceKind::Garage } };
		for (const auto& P : Map)
		{
			if (P.Key == Name) { OutKind = P.Value; return true; }
		}
		return false;
	}

	FName DistrictToName(EMTADistrict District)
	{
		// Schreibweise wie in interiors.json ("districts", "districtStyles")
		switch (District)
		{
		case EMTADistrict::Core:        return TEXT("Core");
		case EMTADistrict::Saltmarket:  return TEXT("Saltmarket");
		case EMTADistrict::LanternHill: return TEXT("LanternHill");
		case EMTADistrict::Greenmoor:   return TEXT("Greenmoor");
		case EMTADistrict::Rustline:    return TEXT("Rustline");
		case EMTADistrict::Sunstrip:    return TEXT("Sunstrip");
		default:                        return NAME_None;
		}
	}
}
