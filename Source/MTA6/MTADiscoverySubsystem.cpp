#include "MTADiscoverySubsystem.h"
#include "MTAStorySubsystem.h"
#include "MTAHeatSubsystem.h"
#include "MTAWorldEventSubsystem.h"
#include "MTACityGenerator.h"
#include "MTA6GameMode.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#define LOCTEXT_NAMESPACE "MTADiscovery"

// =====================================================================================================
// Hilfsfunktionen
// =====================================================================================================

namespace MTADiscovery
{
	FName DistrictToId(EMTADistrict District)
	{
		switch (District)
		{
		case EMTADistrict::Core:        return TEXT("core");
		case EMTADistrict::Saltmarket:  return TEXT("saltmarket");
		case EMTADistrict::LanternHill: return TEXT("lantern");
		case EMTADistrict::Greenmoor:   return TEXT("greenmoor");
		case EMTADistrict::Rustline:    return TEXT("rustline");
		case EMTADistrict::Sunstrip:    return TEXT("sunstrip");
		default:                        return NAME_None;
		}
	}

	bool IdToDistrict(FName Id, EMTADistrict& OutDistrict)
	{
		for (int32 I = 0; I < MTADistrict::Num(); ++I)
		{
			const EMTADistrict D = static_cast<EMTADistrict>(I);
			if (DistrictToId(D) == Id)
			{
				OutDistrict = D;
				return true;
			}
		}
		return false;
	}

	EMTADiscoveryType TypeFromString(const FString& S)
	{
		static const TMap<FString, EMTADiscoveryType> Map = {
			{TEXT("rooftop_garden"), EMTADiscoveryType::RooftopGarden}, {TEXT("mural"), EMTADiscoveryType::Mural},
			{TEXT("viewpoint"), EMTADiscoveryType::Viewpoint}, {TEXT("tunnel"), EMTADiscoveryType::Tunnel},
			{TEXT("stunt"), EMTADiscoveryType::Stunt}, {TEXT("abandoned_station"), EMTADiscoveryType::AbandonedStation},
			{TEXT("hacker_terminal"), EMTADiscoveryType::HackerTerminal}, {TEXT("concert"), EMTADiscoveryType::Concert},
			{TEXT("smuggler_cave"), EMTADiscoveryType::SmugglerCave}, {TEXT("black_market"), EMTADiscoveryType::BlackMarket},
			{TEXT("urban_legend"), EMTADiscoveryType::UrbanLegend}, {TEXT("animal"), EMTADiscoveryType::Animal},
			{TEXT("echo_glitch"), EMTADiscoveryType::EchoGlitch}, {TEXT("time_capsule"), EMTADiscoveryType::TimeCapsule},
			{TEXT("love_letter"), EMTADiscoveryType::LoveLetter}, {TEXT("ghost_ship"), EMTADiscoveryType::GhostShip},
			{TEXT("hidden_garage"), EMTADiscoveryType::HiddenGarage}, {TEXT("memorial"), EMTADiscoveryType::Memorial},
			{TEXT("oddity"), EMTADiscoveryType::Oddity}, {TEXT("ruin"), EMTADiscoveryType::Ruin},
			{TEXT("waterfall"), EMTADiscoveryType::Waterfall}, {TEXT("cave"), EMTADiscoveryType::Cave}};
		const EMTADiscoveryType* T = Map.Find(S);
		return T ? *T : EMTADiscoveryType::Other;
	}

	EMTADiscoverMode ModeFromString(const FString& S)
	{
		if (S == TEXT("dwell")) return EMTADiscoverMode::Dwell;
		if (S == TEXT("interact")) return EMTADiscoverMode::Interact;
		if (S == TEXT("photo")) return EMTADiscoverMode::Photo;
		if (S == TEXT("script")) return EMTADiscoverMode::Script;
		return EMTADiscoverMode::Proximity;
	}

	EMTARewardType RewardTypeFromString(const FString& S)
	{
		if (S == TEXT("vehicle")) return EMTARewardType::Vehicle;
		if (S == TEXT("skillPoint")) return EMTARewardType::SkillPoint;
		if (S == TEXT("lore")) return EMTARewardType::Lore;
		if (S == TEXT("standing")) return EMTARewardType::Standing;
		if (S == TEXT("cosmetic")) return EMTARewardType::Cosmetic;
		if (S == TEXT("item")) return EMTARewardType::Item;
		if (S == TEXT("aegis")) return EMTARewardType::Aegis;
		return EMTARewardType::Money;
	}

	EMTAHintKind HintKindFromString(const FString& S)
	{
		if (S == TEXT("sound")) return EMTAHintKind::Sound;
		if (S == TEXT("visual")) return EMTAHintKind::Visual;
		if (S == TEXT("radio")) return EMTAHintKind::Radio;
		if (S == TEXT("note")) return EMTAHintKind::Note;
		if (S == TEXT("smell")) return EMTAHintKind::Smell;
		return EMTAHintKind::Rumor;
	}
}

namespace
{
	FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		FString S;
		if (O.IsValid()) { O->TryGetStringField(Key, S); }
		return S;
	}

	double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default)
	{
		double V = Default;
		if (O.IsValid()) { O->TryGetNumberField(Key, V); }
		return V;
	}

	bool CmpNum(const FString& Op, double A, double B)
	{
		if (Op == TEXT(">=")) return A >= B;
		if (Op == TEXT(">")) return A > B;
		if (Op == TEXT("<=")) return A <= B;
		if (Op == TEXT("<")) return A < B;
		if (Op == TEXT("==")) return FMath::IsNearlyEqual(A, B);
		if (Op == TEXT("!=")) return !FMath::IsNearlyEqual(A, B);
		return false;
	}

	/** Wie UMTAStorySubsystem::ValueToString (Flags werden dort als String gespeichert). */
	FString ValueToString(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid()) return FString();
		switch (V->Type)
		{
		case EJson::Boolean: return V->AsBool() ? TEXT("true") : TEXT("false");
		case EJson::Number: return FString::SanitizeFloat(V->AsNumber());
		default: return V->AsString();
		}
	}

	/** Stundenfenster [From, To), ueber Mitternacht, wenn From > To. */
	bool InHourWindow(float H, float From, float To)
	{
		if (FMath::IsNearlyEqual(From, To) || (From <= 0.f && To >= 24.f)) return true;
		return From < To ? (H >= From && H < To) : (H >= From || H < To);
	}

	bool ArrayHasString(const TArray<TSharedPtr<FJsonValue>>& Arr, const FString& S)
	{
		for (const TSharedPtr<FJsonValue>& V : Arr)
		{
			if (V.IsValid() && V->AsString() == S) return true;
		}
		return false;
	}

	FString SerializeArray(const TArray<TSharedPtr<FJsonValue>>& Arr)
	{
		FString Out;
		FJsonSerializer::Serialize(Arr, TJsonWriterFactory<>::Create(&Out));
		return Out;
	}

	bool LoadJsonFile(const FString& File, TSharedPtr<FJsonObject>& Out)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			return false;
		}
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
	}
}

// =====================================================================================================
// Lebenszyklus und Laden
// =====================================================================================================

void UMTADiscoverySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMTAStorySubsystem>();
	LoadDefinitions();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMTADiscoverySubsystem::HandleTick), FMath::Max(0.05f, TickInterval));
}

void UMTADiscoverySubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickHandle);
	TickHandle.Reset();
	Super::Deinitialize();
}

bool UMTADiscoverySubsystem::LoadDefinitions(const FString& SecretsPath, const FString& WildernessPath)
{
	Defs.Reset();
	Runtime.Reset();
	IndexById.Reset();
	Chains.Reset();
	Collections.Reset();
	Landmarks.Reset();
	DistrictNames.Reset();

	const FString SecretsFile = SecretsPath.IsEmpty() ? FPaths::ProjectContentDir() / TEXT("Data/secrets.json") : SecretsPath;
	const FString WildFile = WildernessPath.IsEmpty() ? FPaths::ProjectContentDir() / TEXT("Data/wilderness.json") : WildernessPath;

	bool bOk = true;
	TSharedPtr<FJsonObject> Root;
	if (LoadJsonFile(SecretsFile, Root))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Root->TryGetArrayField(TEXT("districts"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				DistrictNames.Add(FName(*Str(O, TEXT("id"))), Str(O, TEXT("name")));
			}
		}
		if (Root->TryGetArrayField(TEXT("landmarks"), Arr)) { ParseLandmarks(*Arr); }
		if (Root->TryGetArrayField(TEXT("chains"), Arr)) { ParseChains(*Arr); }
		if (Root->TryGetArrayField(TEXT("collections"), Arr)) { ParseCollections(*Arr); }
		if (Root->TryGetArrayField(TEXT("secrets"), Arr)) { ParseDiscoveryArray(*Arr, NAME_None); }
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("MTA6: secrets.json nicht lesbar/gueltig: %s"), *SecretsFile);
		bOk = false;
	}

	TSharedPtr<FJsonObject> Wild;
	if (LoadJsonFile(WildFile, Wild))
	{
		const TSharedPtr<FJsonObject>* Region = nullptr;
		FName RegionId = TEXT("verdanta");
		if (Wild->TryGetObjectField(TEXT("region"), Region))
		{
			RegionId = FName(*Str(*Region, TEXT("id")));
			DistrictNames.Add(RegionId, Str(*Region, TEXT("name")));
		}
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Wild->TryGetArrayField(TEXT("chains"), Arr)) { ParseChains(*Arr); }
		if (Wild->TryGetArrayField(TEXT("collections"), Arr)) { ParseCollections(*Arr); }
		if (Wild->TryGetArrayField(TEXT("discoveries"), Arr)) { ParseDiscoveryArray(*Arr, RegionId); }
	}
	else
	{
		// Wildnis ist optional (spaetere Ausbaustufe).
		UE_LOG(LogTemp, Warning, TEXT("MTA6: wilderness.json nicht geladen: %s"), *WildFile);
	}

	RebuildRuntimeStates();
	return bOk;
}

void UMTADiscoverySubsystem::ParseDiscoveryArray(const TArray<TSharedPtr<FJsonValue>>& Arr, FName RegionId)
{
	for (const TSharedPtr<FJsonValue>& V : Arr)
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
		if (!O.IsValid()) continue;

		FMTADiscoveryDef D;
		D.Id = FName(*Str(O, TEXT("id")));
		if (D.Id.IsNone() || IndexById.Contains(D.Id))
		{
			UE_LOG(LogTemp, Warning, TEXT("MTA6: Entdeckung ohne/mit doppelter Id uebersprungen: %s"), *D.Id.ToString());
			continue;
		}
		D.DistrictId = FName(*Str(O, TEXT("district")));
		D.RegionId = RegionId;
		D.Type = MTADiscovery::TypeFromString(Str(O, TEXT("type")));
		D.Mode = MTADiscovery::ModeFromString(Str(O, TEXT("discover")));
		D.Title = Str(O, TEXT("title"));
		D.Lore = Str(O, TEXT("lore"));
		D.DwellSeconds = (float)Num(O, TEXT("dwellSeconds"), D.Mode == EMTADiscoverMode::Dwell ? 5.0 : 0.0);

		const TSharedPtr<FJsonObject>* Pos = nullptr;
		if (O->TryGetObjectField(TEXT("pos"), Pos))
		{
			const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
			if ((*Pos)->TryGetArrayField(TEXT("block"), A) && A->Num() == 2)
			{
				D.Block = FIntPoint((int32)(*A)[0]->AsNumber(), (int32)(*A)[1]->AsNumber());
			}
			if ((*Pos)->TryGetArrayField(TEXT("at"), A) && A->Num() == 2)
			{
				D.BlockAt = FVector2D((*A)[0]->AsNumber(), (*A)[1]->AsNumber());
			}
			if ((*Pos)->TryGetArrayField(TEXT("localM"), A) && A->Num() == 3)
			{
				D.LocalM = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
			}
			D.HeightM = (float)Num(*Pos, TEXT("heightM"), 0.0);
			D.RadiusM = (float)Num(*Pos, TEXT("radiusM"), 8.0);
			const FString Anchor = Str(*Pos, TEXT("anchorTag"));
			D.AnchorTag = Anchor.IsEmpty() ? D.Id : FName(*Anchor);
		}

		const TSharedPtr<FJsonObject>* Chain = nullptr;
		if (O->TryGetObjectField(TEXT("chain"), Chain))
		{
			D.ChainId = FName(*Str(*Chain, TEXT("id")));
			D.ChainStep = (int32)Num(*Chain, TEXT("step"), 0.0);
		}

		const TArray<TSharedPtr<FJsonValue>>* Hints = nullptr;
		if (O->TryGetArrayField(TEXT("hints"), Hints))
		{
			for (const TSharedPtr<FJsonValue>& HV : *Hints)
			{
				const TSharedPtr<FJsonObject> HO = HV->AsObject();
				if (!HO.IsValid()) continue;
				FMTADiscoveryHint H;
				H.Kind = MTADiscovery::HintKindFromString(Str(HO, TEXT("kind")));
				H.Text = Str(HO, TEXT("text"));
				H.SoundCue = FName(*Str(HO, TEXT("cue")));
				H.AudibleRadiusM = (float)Num(HO, TEXT("radiusM"), 0.0);
				D.Hints.Add(H);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Rewards = nullptr;
		O->TryGetArrayField(TEXT("rewards"), Rewards);
		D.Rewards = ParseRewards(Rewards);

		const TSharedPtr<FJsonObject>* Accent = nullptr;
		if (O->TryGetObjectField(TEXT("accent"), Accent))
		{
			const FString Hex = Str(*Accent, TEXT("color"));
			if (!Hex.IsEmpty()) { D.Accent.Color = FLinearColor(FColor::FromHex(Hex)); }
			D.Accent.LightStyle = FName(*Str(*Accent, TEXT("light")));
			D.Accent.Sightline = Str(*Accent, TEXT("sightline"));
		}

		FMTADiscoveryRuntime RT;
		const TArray<TSharedPtr<FJsonValue>>* Conds = nullptr;
		if (O->TryGetArrayField(TEXT("visibleWhen"), Conds)) { RT.VisibleWhen = *Conds; }
		if (O->TryGetArrayField(TEXT("unlockWhen"), Conds)) { RT.UnlockWhen = *Conds; }
		D.VisibleWhenJson = SerializeArray(RT.VisibleWhen);
		D.UnlockWhenJson = SerializeArray(RT.UnlockWhen);

		IndexById.Add(D.Id, Defs.Num());
		Defs.Add(MoveTemp(D));
		Runtime.SetNum(Defs.Num());
		Runtime.Last() = MoveTemp(RT);
	}
}

TArray<FMTADiscoveryReward> UMTADiscoverySubsystem::ParseRewards(const TArray<TSharedPtr<FJsonValue>>* Arr)
{
	TArray<FMTADiscoveryReward> Out;
	if (!Arr) return Out;
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
		if (!O.IsValid()) continue;
		FMTADiscoveryReward R;
		R.Type = MTADiscovery::RewardTypeFromString(Str(O, TEXT("type")));
		R.Id = FName(*Str(O, TEXT("id")));
		R.Amount = (int32)Num(O, TEXT("amount"), R.Type == EMTARewardType::SkillPoint ? 1.0 : 0.0);
		R.Text = Str(O, TEXT("text"));
		Out.Add(R);
	}
	return Out;
}

void UMTADiscoverySubsystem::ParseChains(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
	for (const TSharedPtr<FJsonValue>& V : Arr)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid()) continue;
		FMTAChainDef C;
		C.Id = FName(*Str(O, TEXT("id")));
		C.Title = Str(O, TEXT("title"));
		C.Description = Str(O, TEXT("description"));
		C.NumSteps = (int32)Num(O, TEXT("steps"), 0.0);
		const TArray<TSharedPtr<FJsonValue>>* R = nullptr;
		O->TryGetArrayField(TEXT("finalRewards"), R);
		C.FinalRewards = ParseRewards(R);
		Chains.Add(C.Id, C);
	}
}

void UMTADiscoverySubsystem::ParseCollections(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
	for (const TSharedPtr<FJsonValue>& V : Arr)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid()) continue;
		FMTACollectionDef C;
		C.Id = FName(*Str(O, TEXT("id")));
		C.DistrictId = FName(*Str(O, TEXT("district")));
		C.Title = Str(O, TEXT("title"));
		C.Description = Str(O, TEXT("description"));
		C.Count = (int32)Num(O, TEXT("count"), 0.0);
		const TArray<TSharedPtr<FJsonValue>>* R = nullptr;
		O->TryGetArrayField(TEXT("rewards"), R);
		C.Rewards = ParseRewards(R);
		Collections.Add(C.Id, C);
	}
}

void UMTADiscoverySubsystem::ParseLandmarks(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
	for (const TSharedPtr<FJsonValue>& V : Arr)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid()) continue;
		FMTALandmark L;
		L.Id = FName(*Str(O, TEXT("id")));
		L.DistrictId = FName(*Str(O, TEXT("district")));
		L.Title = Str(O, TEXT("title"));
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O->TryGetArrayField(TEXT("block"), A) && A->Num() == 2)
		{
			L.Block = FIntPoint((int32)(*A)[0]->AsNumber(), (int32)(*A)[1]->AsNumber());
		}
		L.HeightM = (float)Num(O, TEXT("heightM"), 0.0);
		L.Tier = (int32)Num(O, TEXT("tier"), 3.0);
		const FString Hex = Str(O, TEXT("color"));
		if (!Hex.IsEmpty()) { L.AccentColor = FLinearColor(FColor::FromHex(Hex)); }
		L.NightLight = Str(O, TEXT("nightLight"));
		L.SightlineRule = Str(O, TEXT("sightline"));
		Landmarks.Add(L);
	}
}

void UMTADiscoverySubsystem::RebuildRuntimeStates()
{
	for (int32 I = 0; I < Defs.Num(); ++I)
	{
		FMTADiscoveryRuntime& RT = Runtime[I];
		const int32* Hints = HintCounts.Find(Defs[I].Id);
		RT.HintsRevealed = Hints ? *Hints : 0;
		RT.DwellTime = 0.f;
		RT.bAnchorResolved = false;
		RT.State = FoundIds.Contains(Defs[I].Id) ? EMTADiscoveryState::Found
			: (RT.HintsRevealed > 0 ? EMTADiscoveryState::Hinted : EMTADiscoveryState::Hidden);
	}
}

void UMTADiscoverySubsystem::ResetProgress()
{
	FoundIds.Reset();
	HintCounts.Reset();
	Items.Reset();
	CollectedItems.Reset();
	RegionStates.Reset();
	CompletedChains.Reset();
	UnlockedVehicles.Reset();
	UnlockedCosmetics.Reset();
	UnlockedLore.Reset();
	PendingMoney = 0;
	PendingSkillPoints.Reset();
	PendingStoryEffects.Reset();
	RebuildRuntimeStates();
}

bool UMTADiscoverySubsystem::GetDiscovery(FName Id, FMTADiscoveryDef& OutDef) const
{
	const int32 I = FindIndex(Id);
	if (I == INDEX_NONE) return false;
	OutDef = Defs[I];
	return true;
}

int32 UMTADiscoverySubsystem::FindIndex(FName Id) const
{
	const int32* I = IndexById.Find(Id);
	return I ? *I : INDEX_NONE;
}

int32 UMTADiscoverySubsystem::FindChainStepIndex(FName ChainId, int32 Step) const
{
	for (int32 I = 0; I < Defs.Num(); ++I)
	{
		if (Defs[I].ChainId == ChainId && Defs[I].ChainStep == Step) return I;
	}
	return INDEX_NONE;
}

// =====================================================================================================
// Umgebung
// =====================================================================================================

UWorld* UMTADiscoverySubsystem::GetGameWorld() const
{
	const UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	return (World && World->IsGameWorld()) ? World : nullptr;
}

void UMTADiscoverySubsystem::SetEnvironment(float Hour, int32 InDayIndex, FName InWeather)
{
	HourOverride = Hour < 0.f ? -1.f : FMath::Fmod(Hour, 24.f);
	DayIndex = InDayIndex;
	Weather = InWeather.IsNone() ? FName(TEXT("clear")) : InWeather;
}

float UMTADiscoverySubsystem::GetHour() const
{
	if (HourOverride >= 0.f) return HourOverride;
	if (const UWorld* World = GetGameWorld())
	{
		if (const AMTA6GameMode* GM = Cast<AMTA6GameMode>(World->GetAuthGameMode()))
		{
			return GM->GetHour();
		}
	}
	return 12.f;
}

void UMTADiscoverySubsystem::RegisterRegion(FName RegionId, const FTransform& RegionTransform)
{
	Regions.Add(RegionId, RegionTransform);
}

void UMTADiscoverySubsystem::UnregisterRegion(FName RegionId)
{
	Regions.Remove(RegionId);
}

void UMTADiscoverySubsystem::RefreshAnchors()
{
	// Naechster ResolveLocation-Aufruf sucht die Tag-Anker erneut (Ergebnis wird sonst je Welt zwischengespeichert).
	for (FMTADiscoveryRuntime& R : Runtime)
	{
		R.bAnchorResolved = false;
		R.bHasAnchor = false;
	}
}

void UMTADiscoverySubsystem::RefreshStorySnapshot()
{
	StorySnapshot.Reset();
	const UGameInstance* GI = GetGameInstance();
	UMTAStorySubsystem* Story = GI ? GI->GetSubsystem<UMTAStorySubsystem>() : nullptr;
	if (!Story) return;
	TSharedPtr<FJsonObject> Parsed;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Story->StateToJson()), Parsed) && Parsed.IsValid())
	{
		StorySnapshot = Parsed;
	}
}

// =====================================================================================================
// Bedingungen
// =====================================================================================================

bool UMTADiscoverySubsystem::EvaluateConditions(const TArray<TSharedPtr<FJsonValue>>& Conditions) const
{
	for (const TSharedPtr<FJsonValue>& V : Conditions)
	{
		if (!V.IsValid() || !CheckCond(V->AsObject())) return false;
	}
	return true;
}

bool UMTADiscoverySubsystem::EvaluateConditionsJson(const FString& ConditionsJson)
{
	if (ConditionsJson.IsEmpty()) return true;
	TArray<TSharedPtr<FJsonValue>> Arr;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ConditionsJson), Arr)) return false;
	RefreshStorySnapshot();
	return EvaluateConditions(Arr);
}

bool UMTADiscoverySubsystem::CheckCond(const TSharedPtr<FJsonObject>& C) const
{
	if (!C.IsValid()) return false;
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	FString S;

	// --- Story-Schema (gleiche Semantik wie UMTAStorySubsystem::Check) ---
	auto StoryNum = [this](const TCHAR* Section, const FString& Key) -> double
	{
		const TSharedPtr<FJsonObject>* Sec = nullptr;
		double V = 0.0;
		if (StorySnapshot.IsValid() && StorySnapshot->TryGetObjectField(Section, Sec)) { (*Sec)->TryGetNumberField(Key, V); }
		return V;
	};
	if (C->TryGetArrayField(TEXT("standing"), A) && A->Num() == 3)
	{
		return CmpNum((*A)[1]->AsString(), StoryNum(TEXT("standing"), (*A)[0]->AsString()), (*A)[2]->AsNumber());
	}
	if (C->TryGetArrayField(TEXT("npcTrust"), A) && A->Num() == 3)
	{
		return CmpNum((*A)[1]->AsString(), StoryNum(TEXT("npcTrust"), (*A)[0]->AsString()), (*A)[2]->AsNumber());
	}
	if (C->TryGetArrayField(TEXT("flag"), A) && A->Num() == 3)
	{
		FString Cur = TEXT("false");
		const TSharedPtr<FJsonObject>* Flags = nullptr;
		if (StorySnapshot.IsValid() && StorySnapshot->TryGetObjectField(TEXT("flags"), Flags)) { (*Flags)->TryGetStringField((*A)[0]->AsString(), Cur); }
		const FString Want = ValueToString((*A)[2]);
		return (*A)[1]->AsString() == TEXT("!=") ? Cur != Want : Cur == Want;
	}
	if (C->TryGetArrayField(TEXT("aegis"), A) && A->Num() == 2)
	{
		const double Aegis = StorySnapshot.IsValid() ? Num(StorySnapshot, TEXT("aegis"), 50.0) : 50.0;
		return CmpNum((*A)[0]->AsString(), Aegis, (*A)[1]->AsNumber());
	}
	if (C->TryGetStringField(TEXT("done"), S))
	{
		const TArray<TSharedPtr<FJsonValue>>* Done = nullptr;
		return StorySnapshot.IsValid() && StorySnapshot->TryGetArrayField(TEXT("completed"), Done) && ArrayHasString(*Done, S);
	}
	if (C->TryGetArrayField(TEXT("tag"), A) && A->Num() == 3)
	{
		const TSharedPtr<FJsonObject>* Tags = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		return StorySnapshot.IsValid() && StorySnapshot->TryGetObjectField(TEXT("tags"), Tags)
			&& (*Tags)->TryGetArrayField((*A)[0]->AsString(), List) && ArrayHasString(*List, (*A)[2]->AsString());
	}
	if (C->TryGetArrayField(TEXT("any"), A))
	{
		for (const TSharedPtr<FJsonValue>& Sub : *A) { if (CheckCond(Sub->AsObject())) return true; }
		return false;
	}
	if (C->TryGetArrayField(TEXT("all"), A))
	{
		for (const TSharedPtr<FJsonValue>& Sub : *A) { if (!CheckCond(Sub->AsObject())) return false; }
		return true;
	}
	if (C->TryGetObjectField(TEXT("not"), Obj))
	{
		return !CheckCond(*Obj);
	}

	// --- Erweiterungen des Entdeckungssystems ---
	if (C->TryGetArrayField(TEXT("hour"), A) && A->Num() == 2)
	{
		return InHourWindow(GetHour(), (float)(*A)[0]->AsNumber(), (float)(*A)[1]->AsNumber());
	}
	if (C->TryGetArrayField(TEXT("weekday"), A))
	{
		const int32 W = GetWeekday();
		for (const TSharedPtr<FJsonValue>& V : *A) { if ((int32)V->AsNumber() == W) return true; }
		return false;
	}
	if (C->TryGetArrayField(TEXT("weather"), A))
	{
		return ArrayHasString(*A, Weather.ToString());
	}
	if (C->TryGetStringField(TEXT("item"), S))
	{
		return Items.Contains(FName(*S));
	}
	if (C->TryGetStringField(TEXT("discovered"), S))
	{
		return FoundIds.Contains(FName(*S));
	}
	if (C->TryGetStringField(TEXT("event"), S))
	{
		const UGameInstance* GI = GetGameInstance();
		const UMTAWorldEventSubsystem* Events = GI ? GI->GetSubsystem<UMTAWorldEventSubsystem>() : nullptr;
		return Events && Events->IsEventActive(FName(*S));
	}
	if (C->TryGetArrayField(TEXT("heat"), A) && A->Num() == 3)
	{
		const UWorld* World = GetGameWorld();
		const UMTAHeatSubsystem* Heat = World ? World->GetSubsystem<UMTAHeatSubsystem>() : nullptr;
		const double V = Heat ? Heat->GetHeat(FName(*(*A)[0]->AsString())) : 0.0;
		return CmpNum((*A)[1]->AsString(), V, (*A)[2]->AsNumber());
	}
	if (C->TryGetArrayField(TEXT("collection"), A) && A->Num() == 3)
	{
		return CmpNum((*A)[1]->AsString(), GetCollectionProgress(FName(*(*A)[0]->AsString())).Collected, (*A)[2]->AsNumber());
	}
	if (C->TryGetArrayField(TEXT("regionState"), A) && A->Num() == 3)
	{
		return CmpNum((*A)[1]->AsString(), GetRegionState(FName(*(*A)[0]->AsString())), (*A)[2]->AsNumber());
	}

	UE_LOG(LogTemp, Warning, TEXT("MTA6: unbekannte Entdeckungs-Bedingung"));
	return false;
}

bool UMTADiscoverySubsystem::IsVisibleIndex(int32 Index) const
{
	const FMTADiscoveryDef& D = Defs[Index];
	if (D.ChainStep > 1)
	{
		const int32 Prev = FindChainStepIndex(D.ChainId, D.ChainStep - 1);
		if (Prev != INDEX_NONE && !FoundIds.Contains(Defs[Prev].Id)) return false;
	}
	return EvaluateConditions(Runtime[Index].VisibleWhen);
}

bool UMTADiscoverySubsystem::IsUnlockedIndex(int32 Index) const
{
	return IsVisibleIndex(Index) && EvaluateConditions(Runtime[Index].UnlockWhen);
}

bool UMTADiscoverySubsystem::IsVisible(FName Id)
{
	const int32 I = FindIndex(Id);
	if (I == INDEX_NONE) return false;
	RefreshStorySnapshot();
	return IsVisibleIndex(I);
}

bool UMTADiscoverySubsystem::IsUnlocked(FName Id)
{
	const int32 I = FindIndex(Id);
	if (I == INDEX_NONE) return false;
	RefreshStorySnapshot();
	return IsUnlockedIndex(I);
}

EMTADiscoveryState UMTADiscoverySubsystem::GetState(FName Id) const
{
	const int32 I = FindIndex(Id);
	return I == INDEX_NONE ? EMTADiscoveryState::Hidden : Runtime[I].State;
}

// =====================================================================================================
// Positionen
// =====================================================================================================

bool UMTADiscoverySubsystem::ResolveLocation(int32 Index, FVector& OutLocation)
{
	UWorld* World = GetGameWorld();
	if (World != AnchorWorld.Get())
	{
		AnchorWorld = World;
		CachedCity.Reset();
		bCityLookupDone = false;
		for (FMTADiscoveryRuntime& R : Runtime) { R.bAnchorResolved = false; R.bHasAnchor = false; }
	}

	const FMTADiscoveryDef& D = Defs[Index];
	FMTADiscoveryRuntime& RT = Runtime[Index];

	// 1) Level-Actor mit Tag (genaueste Quelle, vom Level-Design gesetzt).
	if (!RT.bAnchorResolved && World)
	{
		RT.bAnchorResolved = true;
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsWithTag(World, D.AnchorTag.IsNone() ? D.Id : D.AnchorTag, Found);
		if (Found.Num() > 0 && Found[0])
		{
			RT.bHasAnchor = true;
			RT.AnchorLocation = Found[0]->GetActorLocation();
		}
	}
	if (RT.bHasAnchor)
	{
		OutLocation = RT.AnchorLocation;
		return true;
	}

	// 2) Wildnis: relativ zur registrierten Region.
	if (!D.RegionId.IsNone())
	{
		const FTransform* T = Regions.Find(D.RegionId);
		if (!T) return false;
		OutLocation = T->TransformPosition(D.LocalM * 100.0);
		return true;
	}

	// 3) Parzellenraster des Stadtgenerators (Block.X = bu -> Unreal Y, Block.Y = bv -> Unreal X).
	if (!bCityLookupDone && World)
	{
		bCityLookupDone = true;
		CachedCity = AMTACityGenerator::FindCityGenerator(World);
	}
	const AMTACityGenerator* City = Cast<AMTACityGenerator>(CachedCity.Get());
	const FVector Center = City
		? City->GetBlockCenterLocation(D.Block.Y, D.Block.X)
		: FallbackGridOrigin + FVector(MTACity::BlockCenter(D.Block.Y), MTACity::BlockCenter(D.Block.X), 0.f);
	const double Inner = MTACity::Period - MTACity::RoadWidth;
	OutLocation = Center + FVector((D.BlockAt.Y - 0.5) * Inner, (D.BlockAt.X - 0.5) * Inner, D.HeightM * 100.0);
	return true;
}

bool UMTADiscoverySubsystem::GetDiscoveryLocation(FName Id, FVector& OutLocation)
{
	const int32 I = FindIndex(Id);
	return I != INDEX_NONE && ResolveLocation(I, OutLocation);
}

// =====================================================================================================
// Pruefdurchlauf
// =====================================================================================================

bool UMTADiscoverySubsystem::HandleTick(float DeltaSeconds)
{
	if (!bAutoTrackPlayer) return true;
	UWorld* World = GetGameWorld();
	if (!World || World->IsPaused()) return true;
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		UpdatePlayer(Pawn->GetActorLocation(), DeltaSeconds);
	}
	return true;
}

void UMTADiscoverySubsystem::UpdatePlayer(const FVector& PlayerLocation, float DeltaSeconds)
{
	RefreshStorySnapshot();
	for (int32 I = 0; I < Defs.Num(); ++I)
	{
		FMTADiscoveryRuntime& RT = Runtime[I];
		if (RT.State == EMTADiscoveryState::Found) continue;
		const FMTADiscoveryDef& D = Defs[I];

		if (!IsVisibleIndex(I))
		{
			RT.DwellTime = 0.f;
			continue;
		}
		FVector Loc;
		if (!ResolveLocation(I, Loc)) continue;

		const bool b2D = FMath::IsNearlyZero(D.HeightM) && D.RegionId.IsNone() && !RT.bHasAnchor;
		const double Dist = b2D ? FVector::Dist2D(PlayerLocation, Loc) : FVector::Dist(PlayerLocation, Loc);

		// Geraeusch-Hinweis: im Hoerradius gilt der Ort als angedeutet.
		if (RT.State == EMTADiscoveryState::Hidden)
		{
			for (const FMTADiscoveryHint& H : D.Hints)
			{
				if (H.Kind == EMTAHintKind::Sound && H.AudibleRadiusM > 0.f && Dist <= H.AudibleRadiusM * 100.0)
				{
					RevealHint(I);
					break;
				}
			}
		}

		const bool bInside = Dist <= D.RadiusM * 100.0;
		if (D.Mode == EMTADiscoverMode::Proximity)
		{
			if (bInside && IsUnlockedIndex(I)) { Discover(I); }
		}
		else if (D.Mode == EMTADiscoverMode::Dwell)
		{
			if (bInside && IsUnlockedIndex(I))
			{
				RT.DwellTime += DeltaSeconds;
				if (RT.DwellTime >= D.DwellSeconds) { Discover(I); }
			}
			else
			{
				RT.DwellTime = 0.f;
			}
		}
	}
}

FName UMTADiscoverySubsystem::TryInteract(const FVector& PlayerLocation)
{
	RefreshStorySnapshot();
	int32 Best = INDEX_NONE;
	double BestDist = TNumericLimits<double>::Max();
	for (int32 I = 0; I < Defs.Num(); ++I)
	{
		if (Runtime[I].State == EMTADiscoveryState::Found || Defs[I].Mode != EMTADiscoverMode::Interact) continue;
		FVector Loc;
		if (!ResolveLocation(I, Loc)) continue;
		const double Dist = FVector::Dist(PlayerLocation, Loc);
		if (Dist <= Defs[I].RadiusM * 100.0 && Dist < BestDist && IsUnlockedIndex(I))
		{
			Best = I;
			BestDist = Dist;
		}
	}
	if (Best == INDEX_NONE) return NAME_None;
	Discover(Best);
	return Defs[Best].Id;
}

TArray<FName> UMTADiscoverySubsystem::ReportPhoto(const FVector& CameraLocation, const FVector& ViewDirection, float MaxAngleDeg)
{
	TArray<FName> Result;
	RefreshStorySnapshot();
	const FVector Dir = ViewDirection.GetSafeNormal();
	const double CosMax = FMath::Cos(FMath::DegreesToRadians(MaxAngleDeg));
	for (int32 I = 0; I < Defs.Num(); ++I)
	{
		if (Runtime[I].State == EMTADiscoveryState::Found || Defs[I].Mode != EMTADiscoverMode::Photo) continue;
		FVector Loc;
		if (!ResolveLocation(I, Loc)) continue;
		const FVector To = Loc - CameraLocation;
		if (To.Size() > Defs[I].RadiusM * 100.0) continue;
		if (FVector::DotProduct(To.GetSafeNormal(), Dir) < CosMax) continue;
		if (!IsUnlockedIndex(I)) continue;
		Discover(I);
		Result.Add(Defs[I].Id);
	}
	return Result;
}

bool UMTADiscoverySubsystem::ForceDiscover(FName Id, bool bCheckConditions)
{
	const int32 I = FindIndex(Id);
	if (I == INDEX_NONE || Runtime[I].State == EMTADiscoveryState::Found) return false;
	if (bCheckConditions)
	{
		RefreshStorySnapshot();
		if (!IsUnlockedIndex(I)) return false;
	}
	Discover(I);
	return true;
}

void UMTADiscoverySubsystem::MarkHintHeard(FName Id)
{
	const int32 I = FindIndex(Id);
	if (I != INDEX_NONE && Runtime[I].State != EMTADiscoveryState::Found)
	{
		RevealHint(I);
	}
}

void UMTADiscoverySubsystem::RevealHint(int32 Index)
{
	FMTADiscoveryRuntime& RT = Runtime[Index];
	const FMTADiscoveryDef& D = Defs[Index];
	if (RT.State == EMTADiscoveryState::Found) return;
	const int32 HintIdx = FMath::Min(RT.HintsRevealed, D.Hints.Num() - 1);
	RT.HintsRevealed = FMath::Min(RT.HintsRevealed + 1, FMath::Max(1, D.Hints.Num()));
	HintCounts.Add(D.Id, RT.HintsRevealed);
	RT.State = EMTADiscoveryState::Hinted;
	OnSecretHinted.Broadcast(D.Id, D.Hints.IsValidIndex(HintIdx) ? D.Hints[HintIdx] : FMTADiscoveryHint());
}

void UMTADiscoverySubsystem::Discover(int32 Index)
{
	FMTADiscoveryRuntime& RT = Runtime[Index];
	if (RT.State == EMTADiscoveryState::Found) return;
	const FMTADiscoveryDef D = Defs[Index];   // Kopie: Delegates duerfen Daten neu laden.
	RT.State = EMTADiscoveryState::Found;
	RT.DwellTime = 0.f;
	FoundIds.Add(D.Id);

	ApplyRewards(D.Id, D.Rewards);
	OnSecretFound.Broadcast(D, GetDistrictProgress(D.DistrictId));

	if (!D.ChainId.IsNone())
	{
		const FMTAChainProgress P = GetChainProgress(D.ChainId);
		OnChainProgress.Broadcast(P);
		if (P.bComplete && !CompletedChains.Contains(D.ChainId))
		{
			CompletedChains.Add(D.ChainId);
			if (const FMTAChainDef* C = Chains.Find(D.ChainId))
			{
				ApplyRewards(D.ChainId, C->FinalRewards);
			}
		}
		// Naechster Kettenschritt wird angedeutet: der Fundtext ist der Hinweis.
		const int32 Next = FindChainStepIndex(D.ChainId, D.ChainStep + 1);
		if (Next != INDEX_NONE && Runtime[Next].State == EMTADiscoveryState::Hidden)
		{
			RevealHint(Next);
		}
	}
}

// =====================================================================================================
// Belohnungen
// =====================================================================================================

void UMTADiscoverySubsystem::ApplyRewards(FName SourceId, const TArray<FMTADiscoveryReward>& Rewards)
{
	for (const FMTADiscoveryReward& R : Rewards) { ApplyReward(SourceId, R); }
}

void UMTADiscoverySubsystem::ApplyReward(FName SourceId, const FMTADiscoveryReward& R)
{
	switch (R.Type)
	{
	case EMTARewardType::Money:
		PendingMoney += R.Amount;
		break;
	case EMTARewardType::SkillPoint:
		PendingSkillPoints.FindOrAdd(R.Id) += FMath::Max(1, R.Amount);
		break;
	case EMTARewardType::Standing:
	{
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Pair;
		Pair.Add(MakeShared<FJsonValueString>(R.Id.ToString()));
		Pair.Add(MakeShared<FJsonValueNumber>(R.Amount));
		E->SetArrayField(TEXT("standing"), Pair);
		PendingStoryEffects.Add(MakeShared<FJsonValueObject>(E));
		break;
	}
	case EMTARewardType::Aegis:
	{
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetNumberField(TEXT("aegis"), R.Amount);
		PendingStoryEffects.Add(MakeShared<FJsonValueObject>(E));
		break;
	}
	case EMTARewardType::Item:
		Items.Add(R.Id);
		break;
	case EMTARewardType::Vehicle:
		UnlockedVehicles.AddUnique(R.Id);
		break;
	case EMTARewardType::Cosmetic:
		UnlockedCosmetics.AddUnique(R.Id);
		break;
	case EMTARewardType::Lore:
		UnlockedLore.AddUnique(R.Id.IsNone() ? SourceId : R.Id);
		break;
	}
	OnDiscoveryReward.Broadcast(SourceId, R);
}

int32 UMTADiscoverySubsystem::ConsumeMoney()
{
	const int32 V = PendingMoney;
	PendingMoney = 0;
	return V;
}

TMap<FName, int32> UMTADiscoverySubsystem::ConsumeSkillPoints()
{
	TMap<FName, int32> Out = MoveTemp(PendingSkillPoints);
	PendingSkillPoints.Reset();
	return Out;
}

FString UMTADiscoverySubsystem::ConsumeStoryEffectsJson()
{
	const FString Out = SerializeArray(PendingStoryEffects);
	PendingStoryEffects.Reset();
	return Out;
}

// =====================================================================================================
// Fortschritt, Hooks
// =====================================================================================================

FText UMTADiscoverySubsystem::GetDistrictDisplayName(FName DistrictId) const
{
	EMTADistrict D;
	if (MTADiscovery::IdToDistrict(DistrictId, D))
	{
		return MTADistrict::MakeDefaultProfile(D).DisplayName;
	}
	const FString* Name = DistrictNames.Find(DistrictId);
	return FText::FromString(Name ? *Name : DistrictId.ToString());
}

FMTADistrictProgress UMTADiscoverySubsystem::GetDistrictProgress(FName DistrictId) const
{
	FMTADistrictProgress P;
	P.DistrictId = DistrictId;
	P.DisplayName = GetDistrictDisplayName(DistrictId);
	for (const FMTADiscoveryDef& D : Defs)
	{
		if (D.DistrictId != DistrictId) continue;
		++P.Total;
		if (FoundIds.Contains(D.Id)) ++P.Found;
	}
	P.Summary = FText::Format(LOCTEXT("Progress", "Entdeckt {0} von {1}"), FText::AsNumber(P.Found), FText::AsNumber(P.Total));
	return P;
}

TArray<FMTADistrictProgress> UMTADiscoverySubsystem::GetAllDistrictProgress() const
{
	TArray<FName> Ids;
	for (const FMTADiscoveryDef& D : Defs) { Ids.AddUnique(D.DistrictId); }
	TArray<FMTADistrictProgress> Out;
	for (const FName& Id : Ids) { Out.Add(GetDistrictProgress(Id)); }
	return Out;
}

FMTAChainProgress UMTADiscoverySubsystem::GetChainProgress(FName ChainId) const
{
	FMTAChainProgress P;
	P.ChainId = ChainId;
	if (const FMTAChainDef* C = Chains.Find(ChainId))
	{
		P.Title = C->Title;
		P.NumSteps = C->NumSteps;
	}
	int32 Max = 0;
	for (const FMTADiscoveryDef& D : Defs)
	{
		if (D.ChainId != ChainId) continue;
		Max = FMath::Max(Max, D.ChainStep);
		if (FoundIds.Contains(D.Id)) ++P.StepsFound;
	}
	if (P.NumSteps <= 0) P.NumSteps = Max;
	P.bComplete = P.NumSteps > 0 && P.StepsFound >= P.NumSteps;
	return P;
}

TArray<FMTALandmark> UMTADiscoverySubsystem::GetLandmarks(FName DistrictId) const
{
	TArray<FMTALandmark> Out;
	for (const FMTALandmark& L : Landmarks)
	{
		if (DistrictId.IsNone() || L.DistrictId == DistrictId) Out.Add(L);
	}
	return Out;
}

TArray<FMTARumorHook> UMTADiscoverySubsystem::GetRumors(FName DistrictId, int32 MaxCount)
{
	TArray<FMTARumorHook> Out;
	RefreshStorySnapshot();
	for (int32 I = 0; I < Defs.Num() && Out.Num() < MaxCount; ++I)
	{
		const FMTADiscoveryDef& D = Defs[I];
		if (D.DistrictId != DistrictId || Runtime[I].State == EMTADiscoveryState::Found || !IsVisibleIndex(I)) continue;
		for (const FMTADiscoveryHint& H : D.Hints)
		{
			if (H.Kind == EMTAHintKind::Rumor || H.Kind == EMTAHintKind::Radio)
			{
				FMTARumorHook R;
				R.DiscoveryId = D.Id;
				R.DistrictId = D.DistrictId;
				R.Kind = H.Kind;
				R.Text = H.Text;
				Out.Add(R);
				break;
			}
		}
	}
	return Out;
}

TArray<FMTASoundHook> UMTADiscoverySubsystem::GetSoundHooksNear(const FVector& Location, float RadiusCm)
{
	TArray<FMTASoundHook> Out;
	RefreshStorySnapshot();
	for (int32 I = 0; I < Defs.Num(); ++I)
	{
		if (Runtime[I].State == EMTADiscoveryState::Found || !IsVisibleIndex(I)) continue;
		for (const FMTADiscoveryHint& H : Defs[I].Hints)
		{
			if (H.Kind != EMTAHintKind::Sound || H.SoundCue.IsNone()) continue;
			FVector Loc;
			if (!ResolveLocation(I, Loc) || FVector::Dist(Loc, Location) > RadiusCm + H.AudibleRadiusM * 100.f) continue;
			FMTASoundHook S;
			S.DiscoveryId = Defs[I].Id;
			S.SoundCue = H.SoundCue;
			S.Location = Loc;
			S.RadiusCm = H.AudibleRadiusM * 100.f;
			Out.Add(S);
		}
	}
	return Out;
}

// =====================================================================================================
// Sammelobjekte, Items, Regionszustand
// =====================================================================================================

bool UMTADiscoverySubsystem::CollectItem(FName CollectionId, int32 Index)
{
	const FMTACollectionDef* C = Collections.Find(CollectionId);
	if (!C || Index < 0 || Index >= C->Count) return false;
	TSet<int32>& Set = CollectedItems.FindOrAdd(CollectionId);
	if (Set.Contains(Index)) return false;
	Set.Add(Index);
	const FMTACollectionProgress P = GetCollectionProgress(CollectionId);
	OnCollectionProgress.Broadcast(P);
	if (P.Collected >= P.Total)
	{
		ApplyRewards(CollectionId, C->Rewards);
	}
	return true;
}

bool UMTADiscoverySubsystem::IsItemCollected(FName CollectionId, int32 Index) const
{
	const TSet<int32>* Set = CollectedItems.Find(CollectionId);
	return Set && Set->Contains(Index);
}

FMTACollectionProgress UMTADiscoverySubsystem::GetCollectionProgress(FName CollectionId) const
{
	FMTACollectionProgress P;
	P.CollectionId = CollectionId;
	if (const FMTACollectionDef* C = Collections.Find(CollectionId))
	{
		P.Title = C->Title;
		P.Total = C->Count;
	}
	const TSet<int32>* Set = CollectedItems.Find(CollectionId);
	P.Collected = Set ? Set->Num() : 0;
	return P;
}

void UMTADiscoverySubsystem::AddItem(FName ItemId)
{
	if (!ItemId.IsNone()) Items.Add(ItemId);
}

void UMTADiscoverySubsystem::SetRegionState(FName Key, int32 Value)
{
	const int32* Old = RegionStates.Find(Key);
	if (Old && *Old == Value) return;
	RegionStates.Add(Key, Value);
	OnRegionStateChanged.Broadcast(Key, Value);
}

int32 UMTADiscoverySubsystem::GetRegionState(FName Key) const
{
	const int32* V = RegionStates.Find(Key);
	return V ? *V : 0;
}

// =====================================================================================================
// Speichern / Laden
// =====================================================================================================

namespace
{
	TArray<TSharedPtr<FJsonValue>> NamesToJson(const TArray<FName>& Names)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FName& N : Names) { Arr.Add(MakeShared<FJsonValueString>(N.ToString())); }
		return Arr;
	}

	TArray<FName> JsonToNames(const TSharedPtr<FJsonObject>& Root, const TCHAR* Key)
	{
		TArray<FName> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Root->TryGetArrayField(Key, Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr) { Out.Add(FName(*V->AsString())); }
		}
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> PairsToJson(const TMap<FName, int32>& Map)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const auto& P : Map)
		{
			TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("k"), P.Key.ToString());
			E->SetNumberField(TEXT("v"), P.Value);
			Arr.Add(MakeShared<FJsonValueObject>(E));
		}
		return Arr;
	}

	void JsonToPairs(const TSharedPtr<FJsonObject>& Root, const TCHAR* Key, TMap<FName, int32>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Root->TryGetArrayField(Key, Arr)) return;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> E = V->AsObject();
			if (E.IsValid()) { Out.Add(FName(*Str(E, TEXT("k"))), (int32)Num(E, TEXT("v"), 0.0)); }
		}
	}
}

FString UMTADiscoverySubsystem::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema"), 1);
	Root->SetArrayField(TEXT("found"), NamesToJson(FoundIds.Array()));
	Root->SetArrayField(TEXT("items"), NamesToJson(Items.Array()));
	Root->SetArrayField(TEXT("completedChains"), NamesToJson(CompletedChains.Array()));
	Root->SetArrayField(TEXT("vehicles"), NamesToJson(UnlockedVehicles));
	Root->SetArrayField(TEXT("cosmetics"), NamesToJson(UnlockedCosmetics));
	Root->SetArrayField(TEXT("lore"), NamesToJson(UnlockedLore));

	// Schluessel-Wert-Paare als Arrays ({"k":..,"v":..}): unabhaengig vom Schluesseltyp von FJsonObject::Values.
	Root->SetArrayField(TEXT("hints"), PairsToJson(HintCounts));
	Root->SetArrayField(TEXT("regionStates"), PairsToJson(RegionStates));
	Root->SetArrayField(TEXT("pendingSkills"), PairsToJson(PendingSkillPoints));

	TArray<TSharedPtr<FJsonValue>> Cols;
	for (const auto& P : CollectedItems)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const int32 I : P.Value) { Arr.Add(MakeShared<FJsonValueNumber>(I)); }
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("k"), P.Key.ToString());
		E->SetArrayField(TEXT("v"), Arr);
		Cols.Add(MakeShared<FJsonValueObject>(E));
	}
	Root->SetArrayField(TEXT("collections"), Cols);

	Root->SetNumberField(TEXT("pendingMoney"), PendingMoney);
	Root->SetArrayField(TEXT("pendingStory"), PendingStoryEffects);

	FString Out;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Out));
	return Out;
}

bool UMTADiscoverySubsystem::FromJson(const FString& Json)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) return false;
	if ((int32)Num(Root, TEXT("schema"), 0.0) != 1) return false;

	ResetProgress();
	for (const FName& N : JsonToNames(Root, TEXT("found"))) { FoundIds.Add(N); }
	for (const FName& N : JsonToNames(Root, TEXT("items"))) { Items.Add(N); }
	for (const FName& N : JsonToNames(Root, TEXT("completedChains"))) { CompletedChains.Add(N); }
	UnlockedVehicles = JsonToNames(Root, TEXT("vehicles"));
	UnlockedCosmetics = JsonToNames(Root, TEXT("cosmetics"));
	UnlockedLore = JsonToNames(Root, TEXT("lore"));

	JsonToPairs(Root, TEXT("hints"), HintCounts);
	JsonToPairs(Root, TEXT("regionStates"), RegionStates);
	JsonToPairs(Root, TEXT("pendingSkills"), PendingSkillPoints);
	const TArray<TSharedPtr<FJsonValue>>* Cols = nullptr;
	if (Root->TryGetArrayField(TEXT("collections"), Cols))
	{
		for (const TSharedPtr<FJsonValue>& CV : *Cols)
		{
			const TSharedPtr<FJsonObject> E = CV->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* Idx = nullptr;
			if (!E.IsValid() || !E->TryGetArrayField(TEXT("v"), Idx)) continue;
			TSet<int32>& Set = CollectedItems.FindOrAdd(FName(*Str(E, TEXT("k"))));
			for (const TSharedPtr<FJsonValue>& V : *Idx) { Set.Add((int32)V->AsNumber()); }
		}
	}
	PendingMoney = (int32)Num(Root, TEXT("pendingMoney"), 0.0);
	const TArray<TSharedPtr<FJsonValue>>* Story = nullptr;
	if (Root->TryGetArrayField(TEXT("pendingStory"), Story)) { PendingStoryEffects = *Story; }

	RebuildRuntimeStates();
	return true;
}

#undef LOCTEXT_NAMESPACE
