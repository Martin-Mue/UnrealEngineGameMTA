#include "MTAWardrobe.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTAWardrobe, Log, All);

namespace
{
	FString GStr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FString& Def = FString())
	{
		FString V;
		return (O.IsValid() && O->TryGetStringField(Key, V)) ? V : Def;
	}

	double GNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Def)
	{
		double V = 0.0;
		return (O.IsValid() && O->TryGetNumberField(Key, V)) ? V : Def;
	}

	TArray<FString> GStrings(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (O.IsValid() && O->TryGetArrayField(Key, Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S)) { Out.Add(S); }
			}
		}
		return Out;
	}

	TArray<EMTADistrict> ParseDistricts(const TArray<FString>& Ids)
	{
		TArray<EMTADistrict> Out;
		for (const FString& Id : Ids)
		{
			EMTADistrict D;
			if (UMTAAppearanceLibrary::DistrictFromId(Id, D)) { Out.Add(D); }
		}
		return Out;
	}

	bool IsFeminineOnly(const FMTAWardrobeItem& I) { return I.GenderFit == EMTAGenderFit::Feminine; }

	/** Passt das Item zum Koerperrahmen? Androgyn akzeptiert alles. */
	bool FitsFrame(const FMTAWardrobeItem& I, EMTABodyFrame Frame)
	{
		if (Frame == EMTABodyFrame::Androgynous || I.GenderFit == EMTAGenderFit::Any) { return true; }
		return IsFeminineOnly(I) ? Frame == EMTABodyFrame::Feminine : Frame == EMTABodyFrame::Masculine;
	}

	/** Feste Reihenfolge der Auswertung: Fullbody zuerst, damit seine Sperren wirken. */
	const EMTAWardrobeSlot GBuildOrder[] = {
		EMTAWardrobeSlot::Fullbody, EMTAWardrobeSlot::Hat, EMTAWardrobeSlot::Glasses, EMTAWardrobeSlot::Mask, EMTAWardrobeSlot::Neck,
		EMTAWardrobeSlot::Top, EMTAWardrobeSlot::Jacket, EMTAWardrobeSlot::Bottom, EMTAWardrobeSlot::Gloves, EMTAWardrobeSlot::Shoes, EMTAWardrobeSlot::Bag };
}

void UMTAWardrobeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadWardrobe();
}

bool UMTAWardrobeSubsystem::LoadWardrobe(const FString& Path)
{
	TSharedPtr<FJsonObject> Root;
	if (!MTAAppearanceJson::LoadJsonFile(Path.IsEmpty() ? TEXT("Data/wardrobe.json") : Path, Root))
	{
		return false;
	}
	Items.Reset(); ItemIndex.Reset(); Presets.Reset(); PresetIndex.Reset(); Palettes.Reset();
	Rules = FMTAWardrobeRules();

	// Paletten
	if (const TSharedPtr<FJsonObject>* Pal = nullptr; Root->TryGetObjectField(TEXT("palettes"), Pal) && Pal)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Pal)->Values)
		{
			TArray<FLinearColor>& Colors = Palettes.FindOrAdd(FName(*P.Key));
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (P.Value->TryGetArray(Arr) && Arr)
			{
				for (const TSharedPtr<FJsonValue>& C : *Arr)
				{
					FString Hex;
					if (C->TryGetString(Hex)) { Colors.Add(MTAAppearanceJson::ParseColor(Hex, FLinearColor::White)); }
				}
			}
		}
	}

	// Regeln
	if (const TSharedPtr<FJsonObject>* R = nullptr; Root->TryGetObjectField(TEXT("rules"), R) && R)
	{
		auto Slots = [&](const TCHAR* Key, TArray<EMTAWardrobeSlot>& Out)
		{
			for (const FString& S : GStrings(*R, Key))
			{
				EMTAWardrobeSlot Slot;
				if (UMTAAppearanceLibrary::SlotFromId(S, Slot)) { Out.Add(Slot); }
			}
		};
		Slots(TEXT("requiredSlots"), Rules.RequiredSlots);
		Slots(TEXT("requiredUnlessFullbody"), Rules.RequiredUnlessFullbody);
		Slots(TEXT("fullbodyDefaultBlocks"), Rules.FullbodyDefaultBlocks);
		Rules.ColdBelowC = static_cast<float>(GNum(*R, TEXT("coldBelowC"), 8));
		Rules.HotAboveC = static_cast<float>(GNum(*R, TEXT("hotAboveC"), 30));
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if ((*R)->TryGetArrayField(TEXT("tagConflicts"), Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
				FString A, B;
				if (V->TryGetArray(Pair) && Pair && Pair->Num() == 2 && (*Pair)[0]->TryGetString(A) && (*Pair)[1]->TryGetString(B))
				{
					Rules.TagConflicts.Emplace(FName(*A), FName(*B));
				}
			}
		}
		if ((*R)->TryGetArrayField(TEXT("styleClashes"), Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
				FString A, B;
				EMTAClothingStyle SA, SB;
				if (V->TryGetArray(Pair) && Pair && Pair->Num() == 2 && (*Pair)[0]->TryGetString(A) && (*Pair)[1]->TryGetString(B)
					&& UMTAAppearanceLibrary::StyleFromId(A, SA) && UMTAAppearanceLibrary::StyleFromId(B, SB))
				{
					Rules.StyleClashes.Emplace(SA, SB);
				}
			}
		}
	}

	// Kleidungsstuecke
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Root->TryGetArrayField(TEXT("items"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid()) { continue; }
			FMTAWardrobeItem I;
			I.Id = FName(*GStr(O, TEXT("id")));
			I.DisplayName = GStr(O, TEXT("name"));
			if (!UMTAAppearanceLibrary::SlotFromId(GStr(O, TEXT("slot")), I.Slot))
			{
				UE_LOG(LogMTAWardrobe, Warning, TEXT("MTA6: Item %s hat unbekannten Slot"), *I.Id.ToString());
				continue;
			}
			UMTAAppearanceLibrary::StyleFromId(GStr(O, TEXT("style")), I.Style);
			I.Districts = ParseDistricts(GStrings(O, TEXT("districts")));
			for (const FString& F : GStrings(O, TEXT("factions"))) { I.Factions.Add(FName(*F)); }
			I.TempMinC = static_cast<float>(GNum(O, TEXT("tempMin"), -10));
			I.TempMaxC = static_cast<float>(GNum(O, TEXT("tempMax"), 40));
			I.RainResistance = static_cast<float>(GNum(O, TEXT("rain"), 0));
			I.Conspicuousness = static_cast<float>(GNum(O, TEXT("conspicuous"), 0.2));
			I.Price = static_cast<int32>(GNum(O, TEXT("price"), 0));
			const FString Gender = GStr(O, TEXT("gender"), TEXT("any"));
			I.GenderFit = Gender == TEXT("fem") ? EMTAGenderFit::Feminine : (Gender == TEXT("masc") ? EMTAGenderFit::Masculine : EMTAGenderFit::Any);
			O->TryGetBoolField(TEXT("child"), I.bChildSize);
			for (const FString& B : GStrings(O, TEXT("blocks")))
			{
				EMTAWardrobeSlot Slot;
				if (UMTAAppearanceLibrary::SlotFromId(B, Slot)) { I.BlocksSlots.Add(Slot); }
			}
			for (const FString& T : GStrings(O, TEXT("tags"))) { I.Tags.Add(FName(*T)); }
			const FString MeshPath = GStr(O, TEXT("mesh"));
			if (!MeshPath.IsEmpty()) { I.Mesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(MeshPath)); }
			ItemIndex.Add(I.Id, Items.Add(I));
		}
	}

	// Presets
	if (Root->TryGetArrayField(TEXT("presets"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid()) { continue; }
			FMTAOutfitPreset P;
			P.Id = FName(*GStr(O, TEXT("id")));
			P.DisplayName = GStr(O, TEXT("name"));
			P.Faction = FName(*GStr(O, TEXT("faction")));
			P.Districts = ParseDistricts(GStrings(O, TEXT("districts")));
			UMTAAppearanceLibrary::StyleFromId(GStr(O, TEXT("style")), P.Style);
			for (const FString& T : GStrings(O, TEXT("tags"))) { P.Tags.Add(FName(*T)); }
			if (const TSharedPtr<FJsonObject>* SlotsObj = nullptr; O->TryGetObjectField(TEXT("slots"), SlotsObj) && SlotsObj)
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& S : (*SlotsObj)->Values)
				{
					EMTAWardrobeSlot Slot;
					const TSharedPtr<FJsonObject> SO = S.Value->AsObject();
					if (!SO.IsValid() || !UMTAAppearanceLibrary::SlotFromId(S.Key, Slot)) { continue; }
					FMTAOutfitSlotRule Rule;
					for (const FString& It : GStrings(SO, TEXT("items"))) { Rule.Items.Add(FName(*It)); }
					Rule.Chance = static_cast<float>(GNum(SO, TEXT("p"), 1.0));
					P.Slots.Add(Slot, Rule);
				}
			}
			if (const TSharedPtr<FJsonObject>* PalObj = nullptr; O->TryGetObjectField(TEXT("palette"), PalObj) && PalObj)
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& S : (*PalObj)->Values)
				{
					EMTAWardrobeSlot Slot;
					FString Name;
					if (UMTAAppearanceLibrary::SlotFromId(S.Key, Slot) && S.Value->TryGetString(Name)) { P.Palette.Add(Slot, FName(*Name)); }
				}
			}
			PresetIndex.Add(P.Id, Presets.Add(P));
		}
	}
	UE_LOG(LogMTAWardrobe, Log, TEXT("MTA6: Garderobe geladen: %d Kleidungsstuecke, %d Presets"), Items.Num(), Presets.Num());
	return Items.Num() > 0;
}

const FMTAWardrobeItem* UMTAWardrobeSubsystem::FindItem(FName Id) const
{
	const int32* I = ItemIndex.Find(Id);
	return I ? &Items[*I] : nullptr;
}

const FMTAOutfitPreset* UMTAWardrobeSubsystem::FindPreset(FName Id) const
{
	const int32* I = PresetIndex.Find(Id);
	return I ? &Presets[*I] : nullptr;
}

bool UMTAWardrobeSubsystem::GetItem(FName Id, FMTAWardrobeItem& OutItem) const
{
	if (const FMTAWardrobeItem* I = FindItem(Id)) { OutItem = *I; return true; }
	return false;
}

const TArray<FLinearColor>* UMTAWardrobeSubsystem::FindPalette(FName Name) const
{
	return Palettes.Find(Name);
}

bool UMTAWardrobeSubsystem::ItemMatches(const FMTAWardrobeItem& I, const FMTAWardrobeQuery& Q) const
{
	if (Q.bFilterSlot && I.Slot != Q.Slot) { return false; }
	if (Q.bFilterDistrict && I.Districts.Num() > 0 && !I.Districts.Contains(Q.District)) { return false; }
	if (!Q.Faction.IsNone() && I.Factions.Num() > 0 && !I.Factions.Contains(Q.Faction)) { return false; }
	if (Q.bFilterWeather && (Q.TemperatureC < I.TempMinC - 3.f || Q.TemperatureC > I.TempMaxC + 3.f)) { return false; }
	if (!FitsFrame(I, Q.Frame)) { return false; }
	if (I.bChildSize != Q.bChild) { return false; }
	if (I.Conspicuousness > Q.MaxConspicuousness) { return false; }
	if (Q.bOnlyForSale && I.HasTag(TEXT("not_for_sale"))) { return false; }
	return true;
}

TArray<FName> UMTAWardrobeSubsystem::QueryItems(const FMTAWardrobeQuery& Query) const
{
	TArray<FName> Out;
	for (const FMTAWardrobeItem& I : Items)
	{
		if (ItemMatches(I, Query)) { Out.Add(I.Id); }
	}
	return Out;
}

TArray<FName> UMTAWardrobeSubsystem::GetPresetsForFaction(FName Faction) const
{
	TArray<FName> Out;
	for (const FMTAOutfitPreset& P : Presets)
	{
		if (P.Faction == Faction && !P.Tags.Contains(TEXT("overlay"))) { Out.Add(P.Id); }
	}
	return Out;
}

TArray<FName> UMTAWardrobeSubsystem::GetPresetsForDistrict(EMTADistrict District) const
{
	TArray<FName> Out;
	for (const FMTAOutfitPreset& P : Presets)
	{
		if (P.Districts.Contains(District) && !P.Tags.Contains(TEXT("overlay"))) { Out.Add(P.Id); }
	}
	return Out;
}

bool UMTAWardrobeSubsystem::BuildOutfitFromPreset(FName PresetId, FRandomStream& Stream, EMTABodyFrame Frame, bool bChild,
	TMap<EMTAWardrobeSlot, FName>& OutItems, TMap<EMTAWardrobeSlot, FLinearColor>& OutTints) const
{
	const FMTAOutfitPreset* Preset = FindPreset(PresetId);
	if (!Preset)
	{
		return false;
	}
	OutItems.Reset();
	OutTints.Reset();
	TSet<EMTAWardrobeSlot> Blocked;

	for (const EMTAWardrobeSlot Slot : GBuildOrder)
	{
		const FMTAOutfitSlotRule* Rule = Preset->Slots.Find(Slot);
		if (!Rule || Rule->Items.Num() == 0)
		{
			continue;
		}
		// Zufallszahlen immer verbrauchen, damit das Ergebnis nicht von Sperren abhaengt.
		const float Roll = Stream.FRand();
		const int32 Pick = Stream.RandRange(0, Rule->Items.Num() - 1);
		const float TintRoll = Stream.FRand();
		if (Blocked.Contains(Slot) || Roll >= Rule->Chance)
		{
			continue;
		}

		// Geschlechts-/Groessenfilter; bleibt nichts uebrig, wird ungefiltert gewaehlt.
		TArray<FName> Candidates;
		for (const FName& Id : Rule->Items)
		{
			const FMTAWardrobeItem* I = FindItem(Id);
			if (I && FitsFrame(*I, Frame) && I->bChildSize == bChild) { Candidates.Add(Id); }
		}
		const FName Chosen = Candidates.Num() > 0 ? Candidates[Pick % Candidates.Num()] : Rule->Items[Pick];
		const FMTAWardrobeItem* Item = FindItem(Chosen);
		if (!Item)
		{
			continue;
		}
		OutItems.Add(Slot, Chosen);

		if (Slot == EMTAWardrobeSlot::Fullbody && Item->BlocksSlots.Num() == 0)
		{
			for (EMTAWardrobeSlot B : Rules.FullbodyDefaultBlocks) { Blocked.Add(B); }
		}
		for (EMTAWardrobeSlot B : Item->BlocksSlots) { Blocked.Add(B); }

		if (const FName* PalName = Preset->Palette.Find(Slot))
		{
			if (const TArray<FLinearColor>* Colors = Palettes.Find(*PalName))
			{
				if (Colors->Num() > 0)
				{
					OutTints.Add(Slot, (*Colors)[FMath::Min(static_cast<int32>(TintRoll * Colors->Num()), Colors->Num() - 1)]);
				}
			}
		}
	}
	return true;
}

void UMTAWardrobeSubsystem::AdaptOutfitToWeather(TMap<EMTAWardrobeSlot, FName>& Items_, TMap<EMTAWardrobeSlot, FLinearColor>& Tints,
	const FMTALookContext& Ctx, FRandomStream& Stream, EMTABodyFrame Frame, bool bChild, FName Faction) const
{
	const float Roll = Stream.FRand();
	const float Roll2 = Stream.FRand();
	const bool bFullBlocksJacket = [&]()
	{
		const FName* Full = Items_.Find(EMTAWardrobeSlot::Fullbody);
		const FMTAWardrobeItem* FI = Full ? FindItem(*Full) : nullptr;
		return FI && FI->BlocksSlots.Contains(EMTAWardrobeSlot::Jacket);
	}();

	auto PickFrom = [&](EMTAWardrobeSlot Slot, auto&& Predicate, float R) -> FName
	{
		FMTAWardrobeQuery Q;
		Q.bFilterSlot = true; Q.Slot = Slot; Q.bFilterDistrict = true; Q.District = Ctx.District;
		Q.Frame = Frame; Q.bChild = bChild; Q.Faction = NAME_None;
		TArray<FName> Out;
		for (const FMTAWardrobeItem& I : Items)
		{
			if (ItemMatches(I, Q) && Predicate(I) && !I.HasTag(TEXT("kcg_issue")) && I.Factions.Num() == 0) { Out.Add(I.Id); }
		}
		return Out.Num() > 0 ? Out[FMath::Min(static_cast<int32>(R * Out.Num()), Out.Num() - 1)] : NAME_None;
	};
	auto HasRainGear = [&]()
	{
		for (const TPair<EMTAWardrobeSlot, FName>& P : Items_)
		{
			if (const FMTAWardrobeItem* I = FindItem(P.Value)) { if (I->RainResistance >= 0.8f || I->HasTag(TEXT("rain_gear"))) { return true; } }
		}
		return false;
	};

	if (Ctx.bRaining && !HasRainGear())
	{
		if (!bFullBlocksJacket && Roll < 0.7f)
		{
			const FName J = PickFrom(EMTAWardrobeSlot::Jacket, [&](const FMTAWardrobeItem& I) { return I.RainResistance >= 0.8f && Ctx.TemperatureC >= I.TempMinC && Ctx.TemperatureC <= I.TempMaxC; }, Roll2);
			if (!J.IsNone()) { Items_.Add(EMTAWardrobeSlot::Jacket, J); Tints.Remove(EMTAWardrobeSlot::Jacket); }
		}
		else if (!Items_.Contains(EMTAWardrobeSlot::Bag))
		{
			const FName B = PickFrom(EMTAWardrobeSlot::Bag, [](const FMTAWardrobeItem& I) { return I.HasTag(TEXT("rain_gear")); }, Roll2);
			if (!B.IsNone()) { Items_.Add(EMTAWardrobeSlot::Bag, B); }
		}
	}

	if (Ctx.TemperatureC < Rules.ColdBelowC)
	{
		const FName* Cur = Items_.Find(EMTAWardrobeSlot::Jacket);
		const FMTAWardrobeItem* CI = Cur ? FindItem(*Cur) : nullptr;
		if (!bFullBlocksJacket && (!CI || CI->TempMinC > Ctx.TemperatureC))
		{
			const FName J = PickFrom(EMTAWardrobeSlot::Jacket, [&](const FMTAWardrobeItem& I) { return Ctx.TemperatureC >= I.TempMinC && Ctx.TemperatureC <= I.TempMaxC && I.TempMaxC <= 20.f; }, Roll2);
			if (!J.IsNone()) { Items_.Add(EMTAWardrobeSlot::Jacket, J); Tints.Remove(EMTAWardrobeSlot::Jacket); }
		}
		if (Ctx.TemperatureC < 2.f && !Items_.Contains(EMTAWardrobeSlot::Hat) && Roll < 0.5f)
		{
			const FName H = PickFrom(EMTAWardrobeSlot::Hat, [&](const FMTAWardrobeItem& I) { return Ctx.TemperatureC <= I.TempMaxC; }, Roll2);
			if (!H.IsNone()) { Items_.Add(EMTAWardrobeSlot::Hat, H); }
		}
	}
	else if (Ctx.TemperatureC > Rules.HotAboveC)
	{
		// Ausnahme: Dienst-/Schutzkleidung wird auch bei Hitze getragen.
		for (EMTAWardrobeSlot S : { EMTAWardrobeSlot::Jacket, EMTAWardrobeSlot::Gloves, EMTAWardrobeSlot::Neck })
		{
			const FName* Cur = Items_.Find(S);
			const FMTAWardrobeItem* CI = Cur ? FindItem(*Cur) : nullptr;
			if (CI && CI->TempMaxC < Ctx.TemperatureC && !CI->HasTag(TEXT("kcg_issue")) && Faction != TEXT("kcg"))
			{
				Items_.Remove(S);
				Tints.Remove(S);
			}
		}
	}
}

FMTAOutfitReport UMTAWardrobeSubsystem::ValidateOutfit(const TMap<EMTAWardrobeSlot, FName>& Outfit) const
{
	FMTAOutfitReport Rep;
	TSet<EMTAWardrobeSlot> Blocked;
	TArray<const FMTAWardrobeItem*> Worn;
	for (const TPair<EMTAWardrobeSlot, FName>& P : Outfit)
	{
		const FMTAWardrobeItem* I = FindItem(P.Value);
		if (!I) { Rep.Errors.Add(FString::Printf(TEXT("Unbekanntes Kleidungsstueck: %s"), *P.Value.ToString())); continue; }
		if (I->Slot != P.Key) { Rep.Errors.Add(FString::Printf(TEXT("%s gehoert nicht in diesen Slot"), *P.Value.ToString())); }
		Worn.Add(I);
		TArray<EMTAWardrobeSlot> B = I->BlocksSlots;
		if (I->Slot == EMTAWardrobeSlot::Fullbody && B.Num() == 0) { B = Rules.FullbodyDefaultBlocks; }
		for (EMTAWardrobeSlot S : B) { Blocked.Add(S); }
	}
	for (const FMTAWardrobeItem* I : Worn)
	{
		if (Blocked.Contains(I->Slot))
		{
			Rep.Errors.Add(FString::Printf(TEXT("%s wird von einem anderen Stueck verdeckt"), *I->Id.ToString()));
		}
	}
	const bool bFull = Outfit.Contains(EMTAWardrobeSlot::Fullbody);
	for (EMTAWardrobeSlot S : Rules.RequiredSlots)
	{
		if (!Outfit.Contains(S)) { Rep.Errors.Add(FString::Printf(TEXT("Pflicht-Slot fehlt: %s"), *UMTAAppearanceLibrary::SlotToId(S))); }
	}
	if (!bFull)
	{
		for (EMTAWardrobeSlot S : Rules.RequiredUnlessFullbody)
		{
			if (!Outfit.Contains(S)) { Rep.Errors.Add(FString::Printf(TEXT("Slot fehlt (kein Fullbody): %s"), *UMTAAppearanceLibrary::SlotToId(S))); }
		}
	}
	for (const TPair<FName, FName>& C : Rules.TagConflicts)
	{
		bool bA = false, bB = false;
		for (const FMTAWardrobeItem* I : Worn) { bA |= I->HasTag(C.Key); bB |= I->HasTag(C.Value); }
		if (bA && bB) { Rep.Errors.Add(FString::Printf(TEXT("Tag-Konflikt: %s und %s"), *C.Key.ToString(), *C.Value.ToString())); }
	}
	for (const TPair<EMTAClothingStyle, EMTAClothingStyle>& C : Rules.StyleClashes)
	{
		bool bA = false, bB = false;
		for (const FMTAWardrobeItem* I : Worn) { bA |= I->Style == C.Key; bB |= I->Style == C.Value; }
		if (bA && bB) { Rep.Warnings.Add(TEXT("Stilbruch zwischen Kleidungsstuecken")); }
	}
	return Rep;
}

float UMTAWardrobeSubsystem::ComputeConspicuousness(const TMap<EMTAWardrobeSlot, FName>& Outfit) const
{
	float Max = 0.f, Sum = 0.f;
	int32 N = 0;
	for (const TPair<EMTAWardrobeSlot, FName>& P : Outfit)
	{
		if (const FMTAWardrobeItem* I = FindItem(P.Value))
		{
			Max = FMath::Max(Max, I->Conspicuousness);
			Sum += I->Conspicuousness;
			++N;
		}
	}
	return N > 0 ? FMath::Clamp(0.6f * Max + 0.4f * (Sum / N), 0.f, 1.f) : 0.f;
}

int32 UMTAWardrobeSubsystem::ComputeOutfitPrice(const TMap<EMTAWardrobeSlot, FName>& Outfit) const
{
	int32 Sum = 0;
	for (const TPair<EMTAWardrobeSlot, FName>& P : Outfit)
	{
		if (const FMTAWardrobeItem* I = FindItem(P.Value)) { Sum += I->Price; }
	}
	return Sum;
}

void UMTAWardrobeSubsystem::ApplyOutfit(USkeletalMeshComponent* Body, const FMTAAppearance& Appearance,
	TMap<EMTAWardrobeSlot, USkeletalMeshComponent*>& SlotComps) const
{
	if (!Body || !Body->GetOwner())
	{
		return;
	}
	for (const EMTAWardrobeSlot Slot : GBuildOrder)
	{
		const FName* ItemId = Appearance.Outfit.Find(Slot);
		const FMTAWardrobeItem* Item = ItemId ? FindItem(*ItemId) : nullptr;
		USkeletalMeshComponent** Existing = SlotComps.Find(Slot);

		USkeletalMesh* Mesh = (Item && Item->HasMesh()) ? Item->Mesh.LoadSynchronous() : nullptr;
		if (!Mesh)
		{
			// Platzhalter: kein Mesh - eventuell vorhandene Komponente leeren.
			if (Existing && *Existing) { (*Existing)->SetSkeletalMesh(nullptr); }
			continue;
		}
		USkeletalMeshComponent* Comp = (Existing && *Existing) ? *Existing : nullptr;
		if (!Comp)
		{
			Comp = NewObject<USkeletalMeshComponent>(Body->GetOwner(), NAME_None, RF_Transient);
			Comp->SetupAttachment(Body);
			Comp->RegisterComponent();
			SlotComps.Add(Slot, Comp);
		}
		Comp->SetSkeletalMesh(Mesh);
		Comp->SetLeaderPoseComponent(Body);

		if (const FLinearColor* Tint = Appearance.OutfitTints.Find(Slot))
		{
			for (int32 i = 0; i < Comp->GetNumMaterials(); ++i)
			{
				if (UMaterialInstanceDynamic* MID = Comp->CreateDynamicMaterialInstance(i))
				{
					MID->SetVectorParameterValue(TEXT("MTA_Tint"), *Tint);
				}
			}
		}
	}
}

int32 UMTAWardrobeSubsystem::ValidateData(TArray<FString>& Errors) const
{
	const int32 Start = Errors.Num();
	for (const FMTAOutfitPreset& P : Presets)
	{
		for (const TPair<EMTAWardrobeSlot, FMTAOutfitSlotRule>& S : P.Slots)
		{
			for (const FName& Id : S.Value.Items)
			{
				const FMTAWardrobeItem* I = FindItem(Id);
				if (!I) { Errors.Add(FString::Printf(TEXT("Preset %s: Item %s fehlt"), *P.Id.ToString(), *Id.ToString())); }
				else if (I->Slot != S.Key) { Errors.Add(FString::Printf(TEXT("Preset %s: Item %s im falschen Slot"), *P.Id.ToString(), *Id.ToString())); }
			}
		}
		for (const TPair<EMTAWardrobeSlot, FName>& Pal : P.Palette)
		{
			if (!Palettes.Contains(Pal.Value)) { Errors.Add(FString::Printf(TEXT("Preset %s: Palette %s fehlt"), *P.Id.ToString(), *Pal.Value.ToString())); }
		}
	}
	return Errors.Num() - Start;
}
