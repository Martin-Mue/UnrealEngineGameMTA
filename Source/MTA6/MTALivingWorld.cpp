#include "MTALivingWorld.h"

#include "MTALivingNPC.h"
#include "MTA6GameMode.h"
#include "MTACityGenerator.h"
#include "MTADialogueSubsystem.h"
#include "MTAGossipSubsystem.h"
#include "MTANPCSubsystem.h"
#include "MTARelationshipSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTALiving, Log, All);

namespace
{
	/** Halbe Kapselhoehe + Luft: Actor-Mitte ueber dem Boden. */
	constexpr float GLivingCapsuleLift = 97.f;

	FName LivingDistrictToId(EMTADistrict D)
	{
		switch (D)
		{
		case EMTADistrict::Saltmarket: return FName(TEXT("saltmarket"));
		case EMTADistrict::LanternHill: return FName(TEXT("lantern"));
		case EMTADistrict::Greenmoor: return FName(TEXT("greenmoor"));
		case EMTADistrict::Rustline: return FName(TEXT("rustline"));
		case EMTADistrict::Sunstrip: return FName(TEXT("sunstrip"));
		default: return FName(TEXT("core"));
		}
	}

	uint32 LivingCrc(const FString& S)
	{
		return FCrc::StrCrc32(*S);
	}

	uint32 LivingCrcName(FName N)
	{
		return FCrc::StrCrc32(*N.ToString());
	}

	float LivingYawOf(const FVector& Dir)
	{
		return FMath::RadiansToDegrees(FMath::Atan2((float)Dir.Y, (float)Dir.X));
	}

	bool LivingIsGroupActivity(EMTAActivity Act, bool bTrading)
	{
		switch (Act)
		{
		case EMTAActivity::Bar:
		case EMTAActivity::Leisure:
		case EMTAActivity::Shelter:
		case EMTAActivity::Strike:
		case EMTAActivity::Worship:
		case EMTAActivity::Commute:
		case EMTAActivity::Talk:
			return true;
		case EMTAActivity::Market:
			return !bTrading;
		default:
			return false;
		}
	}

	float LivingNearest(const TArray<float>& Values, float V)
	{
		float Best = Values.Num() > 0 ? Values[0] : V;
		for (const float X : Values)
		{
			if (FMath::Abs(X - V) < FMath::Abs(Best - V)) { Best = X; }
		}
		return Best;
	}

	float LivingLineDuration(const FString& Line)
	{
		return FMath::Clamp(1.6f + 0.055f * (float)Line.Len(), 2.4f, 5.5f);
	}

	// Antwort-Vorlagen fuer NPC-NPC-Begegnungen (Spieltexte: ASCII und Umlaute).
	const TCHAR* const GLivingChatReplies[] = {
		TEXT("Ach was, echt?"), TEXT("Hab ich auch gehört."), TEXT("Mhm. Typisch Kestrel Bay."), TEXT("Erzähl mir was Neues."),
		TEXT("Wem sagst du das."), TEXT("Na ja, so ist das hier eben."), TEXT("Nicht dein Ernst!"), TEXT("Ich sag ja nichts mehr.") };
	const TCHAR* const GLivingChatClosers[] = {
		TEXT("Na dann, bis später."), TEXT("Mach's gut."), TEXT("Pass auf dich auf."), TEXT("Wir sehen uns."), TEXT("Grüß die Familie.") };
	const TCHAR* const GLivingRumorReplies[] = {
		TEXT("Nein! Von wem hast du das?"), TEXT("Das glaub ich erst, wenn ich es sehe."), TEXT("Pssst, nicht so laut."), TEXT("Das erklärt einiges.") };
	const TCHAR* const GLivingArgueReplies[] = {
		TEXT("Pass auf, was du sagst!"), TEXT("Das ist doch gelogen!"), TEXT("Fass mich nicht an!"), TEXT("Immer dasselbe mit dir!") };
	const TCHAR* const GLivingArgueClosers[] = {
		TEXT("Wir sind noch nicht fertig."), TEXT("Verschwinde einfach."), TEXT("Das merk ich mir.") };
	const TCHAR* const GLivingMediateReplies[] = {
		TEXT("Ist ja gut. Ist ja gut."), TEXT("Na schön, für heute."), TEXT("Nur dir zuliebe.") };
	const TCHAR* const GLivingTradeOpeners[] = {
		TEXT("Was kostet das hier?"), TEXT("Hast du noch was von dem Zeug?"), TEXT("Ich nehm zwei. Was machst du mir für einen Preis?") };
	const TCHAR* const GLivingTradeAnswers[] = {
		TEXT("Für dich? Sonderpreis."), TEXT("Zwanzig. Und keine Fragen."), TEXT("Morgen kommt neue Ware.") };
	const TCHAR* const GLivingTradeClosers[] = {
		TEXT("Abgemacht."), TEXT("Zu teuer. Aber gut."), TEXT("Schreib's an, ja?") };
	const TCHAR* const GLivingCoopOpeners[] = {
		TEXT("Schaffen wir das bis Schichtende?"), TEXT("Halt mal kurz fest."), TEXT("Der Vorarbeiter schaut schon wieder.") };
	const TCHAR* const GLivingCoopAnswers[] = {
		TEXT("Wenn keiner dazwischenfunkt, ja."), TEXT("Klar, zu zweit geht's schneller."), TEXT("Lass ihn schauen.") };

	template <SIZE_T N>
	FString LivingPick(const TCHAR* const (&Lines)[N], FRandomStream& Rng)
	{
		return FString(Lines[Rng.RandRange(0, (int32)N - 1)]);
	}

	FString LivingInteractionVerb(EMTAInteractionType T)
	{
		switch (T)
		{
		case EMTAInteractionType::Trade: return TEXT("handelt mit");
		case EMTAInteractionType::Argue: return TEXT("streitet mit");
		case EMTAInteractionType::Mediate: return TEXT("schlichtet bei");
		case EMTAInteractionType::Cooperate: return TEXT("bespricht sich mit");
		case EMTAInteractionType::Shadow: return TEXT("beobachtet");
		case EMTAInteractionType::Betray: return TEXT("tuschelt mit");
		default: return TEXT("plaudert mit");
		}
	}

	FName LivingBarkCategoryFor(EMTAInteractionType T)
	{
		if (T == EMTAInteractionType::Argue) { return FName(TEXT("argue")); }
		if (T == EMTAInteractionType::Mediate) { return FName(TEXT("mediate")); }
		return FName(TEXT("ambient_chat"));
	}
}

// ================================================================================================
// Aufbau
// ================================================================================================

AMTALivingWorldManager::AMTALivingWorldManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("LivingWorldRoot"));
	SetActorEnableCollision(false);
}

AMTALivingWorldManager* AMTALivingWorldManager::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AMTALivingWorldManager> It(World);
	return It ? *It : nullptr;
}

AMTALivingWorldManager* AMTALivingWorldManager::GetOrSpawn(const UObject* WorldContextObject)
{
	if (AMTALivingWorldManager* Existing = Get(WorldContextObject))
	{
		return Existing;
	}
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AMTALivingWorldManager>(AMTALivingWorldManager::StaticClass(), FTransform::Identity, SpawnParams);
}

void AMTALivingWorldManager::BeginPlay()
{
	Super::BeginPlay();
	SimRng.Initialize(Seed);
	SocialRng.Initialize(Seed * 31 + 7);
	if (AMTACityGenerator::FindCityGenerator(this))
	{
		InitializeWorld();
	}
}

void AMTALivingWorldManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (FMTALivingAgent& A : Agents)
	{
		AMTALivingNPC* Npc = A.Actor.Get();
		if (Npc && A.bKey && NPCSys && NPCSys->GetNPCActor(A.Id) == Npc)
		{
			NPCSys->UnregisterNPCActor(A.Id);
		}
	}
	Super::EndPlay(EndPlayReason);
}

double AMTALivingWorldManager::GetNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

bool AMTALivingWorldManager::InitializeWorld()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		NPCSys = GI->GetSubsystem<UMTANPCSubsystem>();
		DialogueSys = GI->GetSubsystem<UMTADialogueSubsystem>();
		RelationSys = GI->GetSubsystem<UMTARelationshipSubsystem>();
		GossipSys = GI->GetSubsystem<UMTAGossipSubsystem>();
	}
	CityGen = AMTACityGenerator::FindCityGenerator(this);

	BuildGrid();
	BuildLocations();
	BuildAgents();

	const float Hour = GetCurrentHour();
	for (int32 I = 0; I < Agents.Num(); ++I)
	{
		EvaluateAgent(I, Hour, true);
	}
	bWorldReady = true;
	UpdateLOD();

	int32 Keys = 0;
	int32 WithDialogue = 0;
	for (const FMTALivingAgent& A : Agents)
	{
		Keys += A.bKey ? 1 : 0;
		WithDialogue += A.bHasDialogue ? 1 : 0;
	}
	UE_LOG(LogMTALiving, Display, TEXT("MTA6: Lebendige Welt bereit: %d Orte, %d Profile (%d mit Dialog), %d Strassenfiguren, Budget %d sichtbar%s"),
		Locations.Num(), Keys, WithDialogue, StreetProfiles.Num(), MaxVisibleNPCs, CityGen ? TEXT("") : TEXT(" (ohne Stadtgenerator)"));
	return true;
}

void AMTALivingWorldManager::BuildGrid()
{
	RoadXs.Reset();
	RoadYs.Reset();
	if (CityGen)
	{
		GridBlocksX = FMath::Max(1, CityGen->BlocksX);
		GridBlocksY = FMath::Max(1, CityGen->BlocksY);
		for (int32 K = 0; K <= GridBlocksX; ++K) { RoadXs.Add((float)CityGen->GetIntersectionLocation(K, 0).X); }
		for (int32 K = 0; K <= GridBlocksY; ++K) { RoadYs.Add((float)CityGen->GetIntersectionLocation(0, K).Y); }
		RoadHalfWidth = CityGen->RoadWidth * 0.5f;
		RoadZ = (float)CityGen->GetActorLocation().Z;
		SidewalkZ = (float)CityGen->GetBlockCenterLocation(0, 0).Z;
	}
	else
	{
		GridBlocksX = MTACity::Blocks;
		GridBlocksY = MTACity::Blocks;
		for (int32 K = 0; K <= GridBlocksX; ++K) { RoadXs.Add(MTACity::RoadCenter(K)); }
		for (int32 K = 0; K <= GridBlocksY; ++K) { RoadYs.Add(MTACity::RoadCenter(K)); }
		RoadHalfWidth = MTACity::RoadHalfWidth;
		RoadZ = 0.f;
		SidewalkZ = 15.f;
	}

	const float Period = RoadXs.Num() > 1 ? FMath::Abs(RoadXs[1] - RoadXs[0]) : MTACity::Period;
	WalkOffset = FMath::Clamp(WalkLineOffset, RoadHalfWidth + 150.f, Period * 0.5f - 200.f);

	WalkLinesX.Reset();
	WalkLinesY.Reset();
	for (int32 K = 0; K < GridBlocksX; ++K) { WalkLinesX.Add(RoadXs[K] + WalkOffset); WalkLinesX.Add(RoadXs[K + 1] - WalkOffset); }
	for (int32 K = 0; K < GridBlocksY; ++K) { WalkLinesY.Add(RoadYs[K] + WalkOffset); WalkLinesY.Add(RoadYs[K + 1] - WalkOffset); }
	WalkLinesX.Sort();
	WalkLinesY.Sort();

	// Bloecke je Bezirk
	DistrictBlocks.Reset();
	const TArray<FName>& Ids = MTAStreetCrowd::GetDistrictIds();
	for (int32 BX = 0; BX < GridBlocksX; ++BX)
	{
		for (int32 BY = 0; BY < GridBlocksY; ++BY)
		{
			const FName D = CityGen ? LivingDistrictToId(CityGen->GetBlockDistrict(BX, BY)) : Ids[(BX + BY * 3) % Ids.Num()];
			DistrictBlocks.FindOrAdd(D).Add(FIntPoint(BX, BY));
		}
	}
}

FMTALivingSpot AMTALivingWorldManager::MakeSpot(int32 BX, int32 BY, int32 Side, float Along) const
{
	FMTALivingSpot S;
	if (!RoadXs.IsValidIndex(BX + 1) || !RoadYs.IsValidIndex(BY + 1))
	{
		return S;
	}
	const float X0 = RoadXs[BX] + WalkOffset;
	const float X1 = RoadXs[BX + 1] - WalkOffset;
	const float Y0 = RoadYs[BY] + WalkOffset;
	const float Y1 = RoadYs[BY + 1] - WalkOffset;
	const float T = FMath::Clamp(Along, 0.05f, 0.95f);
	switch (Side & 3)
	{
	case 0:
		S.Pos = FVector(X0, FMath::Lerp(Y0, Y1, T), SidewalkZ);
		S.Tangent = FVector(0.f, 1.f, 0.f);
		S.Normal = FVector(1.f, 0.f, 0.f);
		break;
	case 1:
		S.Pos = FVector(X1, FMath::Lerp(Y0, Y1, T), SidewalkZ);
		S.Tangent = FVector(0.f, 1.f, 0.f);
		S.Normal = FVector(-1.f, 0.f, 0.f);
		break;
	case 2:
		S.Pos = FVector(FMath::Lerp(X0, X1, T), Y0, SidewalkZ);
		S.Tangent = FVector(1.f, 0.f, 0.f);
		S.Normal = FVector(0.f, 1.f, 0.f);
		break;
	default:
		S.Pos = FVector(FMath::Lerp(X0, X1, T), Y1, SidewalkZ);
		S.Tangent = FVector(1.f, 0.f, 0.f);
		S.Normal = FVector(0.f, -1.f, 0.f);
		break;
	}
	S.bValid = true;
	return S;
}

FMTALivingSpot AMTALivingWorldManager::RandomSidewalkSpot(FName District, uint32 Salt) const
{
	const TArray<FIntPoint>* Blocks = DistrictBlocks.Find(District);
	TArray<FIntPoint> All;
	if (!Blocks || Blocks->Num() == 0)
	{
		for (int32 BX = 0; BX < GridBlocksX; ++BX)
		{
			for (int32 BY = 0; BY < GridBlocksY; ++BY) { All.Add(FIntPoint(BX, BY)); }
		}
		Blocks = &All;
	}
	if (Blocks->Num() == 0)
	{
		return FMTALivingSpot();
	}
	const FIntPoint B = (*Blocks)[(int32)(Salt % (uint32)Blocks->Num())];
	const int32 Side = (int32)((Salt >> 5) % 4u);
	const float Along = 0.15f + 0.7f * (float)((Salt >> 9) % 1000u) / 1000.f;
	return MakeSpot(B.X, B.Y, Side, Along);
}

FMTALivingSpot AMTALivingWorldManager::FallbackSpotForDistrict(FName District, uint32 Salt) const
{
	return RandomSidewalkSpot(District.IsNone() ? FName(TEXT("core")) : District, Salt);
}

bool AMTALivingWorldManager::IsInsideGrid(const FVector& P) const
{
	if (RoadXs.Num() < 2 || RoadYs.Num() < 2)
	{
		return false;
	}
	return P.X >= RoadXs[0] - RoadHalfWidth && P.X <= RoadXs.Last() + RoadHalfWidth
		&& P.Y >= RoadYs[0] - RoadHalfWidth && P.Y <= RoadYs.Last() + RoadHalfWidth;
}

bool AMTALivingWorldManager::IsOnRoadLocal(const FVector& P) const
{
	if (!IsInsideGrid(P))
	{
		return false;
	}
	for (const float X : RoadXs) { if (FMath::Abs((float)P.X - X) < RoadHalfWidth) { return true; } }
	for (const float Y : RoadYs) { if (FMath::Abs((float)P.Y - Y) < RoadHalfWidth) { return true; } }
	return false;
}

float AMTALivingWorldManager::GroundZAt(const FVector& P) const
{
	return IsOnRoadLocal(P) ? RoadZ : SidewalkZ;
}

void AMTALivingWorldManager::BuildLocations()
{
	Locations.Reset();
	LocationOrder.Reset();
	ShelterTags.Reset();

	const TSharedPtr<FJsonObject> Json = MTANPC::LoadJsonFile(TEXT("npcs.json"));
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Json.IsValid() || !Json->TryGetArrayField(TEXT("locations"), Arr) || !Arr)
	{
		UE_LOG(LogMTALiving, Warning, TEXT("npcs.json/locations fehlt: Orte koennen nicht aufgeloest werden"));
		return;
	}

	TArray<FIntPoint> AllBlocks;
	for (int32 BX = 0; BX < GridBlocksX; ++BX)
	{
		for (int32 BY = 0; BY < GridBlocksY; ++BY) { AllBlocks.Add(FIntPoint(BX, BY)); }
	}

	TMap<FName, TArray<FIntPoint>> Shuffled;
	TMap<FName, int32> CountPerDistrict;

	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
		if (!O.IsValid())
		{
			continue;
		}
		FString IdStr, DistrictStr, KindStr, NameStr;
		O->TryGetStringField(TEXT("id"), IdStr);
		O->TryGetStringField(TEXT("district"), DistrictStr);
		O->TryGetStringField(TEXT("kind"), KindStr);
		O->TryGetStringField(TEXT("name"), NameStr);
		if (IdStr.IsEmpty())
		{
			continue;
		}

		FMTALivingLocation L;
		L.Tag = FName(*IdStr);
		L.District = FName(*DistrictStr);
		L.Kind = FName(*KindStr);
		L.Name = NameStr.IsEmpty() ? IdStr : NameStr;

		// Bloecke des Bezirks deterministisch mischen (Seed = Bezirksname), dann der Reihe nach vergeben.
		TArray<FIntPoint>* List = Shuffled.Find(L.District);
		if (!List)
		{
			const TArray<FIntPoint>* Src = DistrictBlocks.Find(L.District);
			TArray<FIntPoint> Copy = (Src && Src->Num() > 0) ? *Src : AllBlocks;
			FRandomStream R((int32)(LivingCrc(DistrictStr) & 0x7fffffff));
			for (int32 I = Copy.Num() - 1; I > 0; --I)
			{
				Copy.Swap(I, R.RandRange(0, I));
			}
			List = &Shuffled.Add(L.District, MoveTemp(Copy));
		}
		if (List->Num() == 0)
		{
			continue;
		}
		const int32 Index = CountPerDistrict.FindOrAdd(L.District)++;
		const uint32 Crc = LivingCrc(IdStr);
		const int32 N = List->Num();
		const FIntPoint B = (*List)[Index % N];
		const int32 Pref = KindStr == TEXT("home") ? 0 : (KindStr == TEXT("work") ? 1 : 2 + (int32)(Crc & 1u));
		L.BlockX = B.X;
		L.BlockY = B.Y;
		L.Side = (Pref + Index / N) % 4;
		L.Along = 0.25f + 0.5f * (float)((Crc >> 8) % 1000u) / 1000.f;
		L.Spot = MakeSpot(L.BlockX, L.BlockY, L.Side, L.Along);

		if (!ShelterTags.Contains(L.District) && (KindStr == TEXT("bar") || KindStr == TEXT("food")))
		{
			ShelterTags.Add(L.District, L.Tag);
		}
		LocationOrder.Add(L.Tag);
		Locations.Add(L.Tag, L);
	}
}

void AMTALivingWorldManager::BuildAgents()
{
	Agents.Reset();
	AgentById.Reset();
	StreetProfiles.Reset();
	SlotOwners.Reset();
	Encounters.Reset();
	PairCooldowns.Reset();

	// Schluessel- und Nebenfiguren aus npcs.json
	if (NPCSys)
	{
		for (const FName& Id : NPCSys->GetAllNPCIds())
		{
			const FMTANPCProfile* P = NPCSys->FindProfile(Id);
			if (!P)
			{
				continue;
			}
			FMTALivingAgent A;
			A.Id = Id;
			A.DisplayName = P->DisplayName.IsEmpty() ? Id.ToString() : P->DisplayName;
			A.Occupation = P->Occupation;
			A.HomeDistrict = P->District;
			A.Faction = P->Faction;
			A.NPCRole = P->Role;
			A.bKey = true;
			A.bHasDialogue = DialogueSys && !DialogueSys->GetDialogueForNPC(Id).IsNone();
			A.Talk = P->Personality.Talk;
			A.Temper = P->Personality.Temper;
			const uint32 Crc = LivingCrcName(Id);
			A.WalkSpeed = 125.f + (float)(Crc % 55u);
			A.Scale = 0.94f + (float)((Crc >> 8) % 13u) / 100.f;
			A.NextLookTime = (double)((Crc >> 4) % 7u);
			AgentById.Add(Id, Agents.Add(A));
		}
	}

	// Strassenfiguren: Treffpunkte je Bezirk aus den Orts-Tags
	TMap<FName, FMTAStreetTagPool> Pools;
	for (const FName& Tag : LocationOrder)
	{
		const FMTALivingLocation& L = Locations[Tag];
		FMTAStreetTagPool& Pool = Pools.FindOrAdd(L.District);
		const FString Kind = L.Kind.ToString();
		if (Kind == TEXT("bar")) { Pool.Bars.Add(Tag); Pool.Social.Add(Tag); }
		else if (Kind == TEXT("food")) { Pool.Food.Add(Tag); Pool.Social.Add(Tag); }
		else if (Kind == TEXT("market") || Kind == TEXT("shop")) { Pool.Markets.Add(Tag); Pool.Social.Add(Tag); }
		else if (Kind == TEXT("public")) { Pool.Public.Add(Tag); Pool.Social.Add(Tag); }
	}
	const TArray<FMTAStreetArchetype> Archetypes = MTAStreetCrowd::LoadArchetypes();
	MTAStreetCrowd::GenerateProfiles(NumStreetNPCs, Seed, Archetypes, Pools,
		[this](FName District, uint32 Salt) -> FMTALivingSpot { return RandomSidewalkSpot(District, Salt); },
		StreetProfiles);

	for (int32 I = 0; I < StreetProfiles.Num(); ++I)
	{
		const FMTAStreetProfile& P = StreetProfiles[I];
		FMTALivingAgent A;
		A.Id = P.Id;
		A.DisplayName = P.DisplayName;
		A.Occupation = P.Occupation;
		A.HomeDistrict = P.District;
		A.Faction = P.Faction;
		A.NPCRole = P.ArchetypeId;
		A.bKey = false;
		A.bHasDialogue = false;
		A.StreetIndex = I;
		A.Talk = P.Talk;
		A.Temper = P.Temper;
		A.WalkSpeed = P.WalkSpeed;
		const uint32 Crc = LivingCrcName(P.Id);
		A.Scale = 0.93f + (float)((Crc >> 8) % 14u) / 100.f;
		A.NextLookTime = (double)((Crc >> 4) % 7u);
		AgentById.Add(P.Id, Agents.Add(A));
	}
}

// ================================================================================================
// Orte
// ================================================================================================

bool AMTALivingWorldManager::TryResolveLocation(FName Tag, FMTALivingSpot& OutSpot) const
{
	if (const FMTALivingLocation* L = Locations.Find(Tag))
	{
		OutSpot = L->Spot;
		return OutSpot.bValid;
	}
	return false;
}

FVector AMTALivingWorldManager::ResolveLocation(FName Tag) const
{
	FMTALivingSpot S;
	return TryResolveLocation(Tag, S) ? S.Pos : FVector::ZeroVector;
}

// ================================================================================================
// Tagesablauf
// ================================================================================================

float AMTALivingWorldManager::GetCurrentHour() const
{
	if (TestHourOverride >= 0.f)
	{
		return TestHourOverride;
	}
	return NPCSys ? NPCSys->GetHour() : 12.f;
}

bool AMTALivingWorldManager::ComputeDesired(const FMTALivingAgent& A, float Hour, FMTALivingDesired& Out) const
{
	Out = FMTALivingDesired();
	if (A.bKey)
	{
		if (!NPCSys)
		{
			return false;
		}
		const FMTAScheduleState S = NPCSys->EvaluateSchedule(A.Id, Hour);
		Out.Activity = S.Activity;
		Out.Tag = S.Location;
		Out.District = S.District.IsNone() ? A.HomeDistrict : S.District;
		Out.bTalkable = S.bTalkable;
		Out.bTrading = S.bTrading;
		Out.OverrideReason = S.OverrideReason;
		Out.bWander = S.Activity == EMTAActivity::Patrol;
		const FMTANPCRuntime* RT = NPCSys->FindRuntime(A.Id);
		if (RT && !RT->bActive)
		{
			// Aus der Welt genommen (Story): bleibt unsichtbar.
			Out.Activity = EMTAActivity::Hide;
			Out.bIndoors = true;
		}
	}
	else
	{
		if (!StreetProfiles.IsValidIndex(A.StreetIndex))
		{
			return false;
		}
		const FMTAStreetProfile& P = StreetProfiles[A.StreetIndex];
		const FMTALivingScheduleEntry* E = MTAStreetCrowd::FindEntry(P.Schedule, Hour);
		if (!E)
		{
			return false;
		}
		Out.Activity = E->Activity;
		Out.Tag = E->Tag;
		Out.Spot = E->Spot;
		Out.bTalkable = E->bTalkable;
		Out.bTrading = E->bTrading;
		Out.bWander = E->bWander;
		Out.District = P.District;

		// Regen/Sturm: Freizeit und Einkauf unter einem Dach (wie die Wetterregeln der Schluesselfiguren)
		const FName WeatherId = NPCSys ? NPCSys->GetWeather() : FName(TEXT("clear"));
		const bool bWet = WeatherId == FName(TEXT("rain")) || WeatherId == FName(TEXT("storm"));
		if (bWet && (Out.Activity == EMTAActivity::Leisure || (Out.Activity == EMTAActivity::Market && !Out.bTrading)))
		{
			if (const FName* Shelter = ShelterTags.Find(P.District))
			{
				Out.Activity = EMTAActivity::Shelter;
				Out.Tag = *Shelter;
				Out.bWander = false;
				Out.OverrideReason = WeatherId;
			}
		}
	}

	// Ort aufloesen
	if (!Out.Tag.IsNone())
	{
		if (const FMTALivingLocation* L = Locations.Find(Out.Tag))
		{
			Out.Spot = L->Spot;
			Out.LocationName = L->Name;
			Out.LocationKind = L->Kind;
			if (Out.District.IsNone()) { Out.District = L->District; }
		}
		else
		{
			Out.Spot = FallbackSpotForDistrict(Out.District.IsNone() ? A.HomeDistrict : Out.District, LivingCrcName(Out.Tag));
			Out.LocationName = NPCSys ? NPCSys->GetLocationName(Out.Tag) : Out.Tag.ToString();
			Out.LocationKind = FName(TEXT("public"));
		}
	}
	else
	{
		const bool bHome = Out.Activity == EMTAActivity::Sleep || Out.Activity == EMTAActivity::Home;
		Out.LocationName = bHome ? TEXT("Wohnung") : TEXT("Arbeit");
		Out.LocationKind = bHome ? FName(TEXT("home")) : FName(TEXT("work"));
	}
	if (!Out.Spot.bValid)
	{
		Out.Spot = FallbackSpotForDistrict(A.HomeDistrict, LivingCrcName(A.Id));
	}

	// Drinnen oder draussen?
	switch (Out.Activity)
	{
	case EMTAActivity::Sleep:
	case EMTAActivity::Home:
	case EMTAActivity::Hide:
		Out.bIndoors = true;
		break;
	case EMTAActivity::Work:
		if (Out.LocationKind == FName(TEXT("work")) || Out.LocationKind == FName(TEXT("home")))
		{
			// Buero/Werkstatt: drinnen, aber in manchen Stunden Pause vor der Tuer (deterministisch je Figur und Stunde).
			const uint32 HourIndex = (uint32)FMath::Max(0, FMath::FloorToInt32(Hour));
			const uint32 H = LivingCrcName(A.Id) + HourIndex * 2654435761u;
			Out.bOnBreak = ((H >> 7) % 100u) < 30u;
			Out.bIndoors = !Out.bOnBreak;
		}
		break;
	default:
		break;
	}
	return true;
}

bool AMTALivingWorldManager::DesiredDiffers(const FMTALivingAgent& A, const FMTALivingDesired& D) const
{
	return A.Activity != D.Activity || A.LocationTag != D.Tag || A.bIndoorAtTarget != D.bIndoors
		|| A.bTrading != D.bTrading || A.bOnBreak != D.bOnBreak
		|| (D.Tag.IsNone() && !A.BaseSpot.Pos.Equals(D.Spot.Pos, 10.f));
}

FString AMTALivingWorldManager::MakeActivityText(const FMTALivingAgent& A, const FMTALivingDesired& D, float Hour) const
{
	const uint32 V = LivingCrcName(A.Id) + (uint32)FMath::Max(0, FMath::FloorToInt32(Hour));
	auto PickVariant = [V](std::initializer_list<const TCHAR*> List) -> FString
	{
		const int32 Num = (int32)List.size();
		int32 I = 0;
		for (const TCHAR* S : List)
		{
			if (I == (int32)(V % (uint32)Num)) { return FString(S); }
			++I;
		}
		return FString();
	};

	switch (D.Activity)
	{
	case EMTAActivity::Sleep: return TEXT("schläft");
	case EMTAActivity::Home: return TEXT("zu Hause");
	case EMTAActivity::Hide: return TEXT("versteckt sich");
	case EMTAActivity::Work:
	{
		if (D.bOnBreak) { return PickVariant({ TEXT("macht Pause"), TEXT("raucht vor der Tür"), TEXT("telefoniert") }); }
		if (D.bTrading) { return TEXT("verkauft Waren"); }
		const FString R = A.NPCRole.ToString();
		if (R.Contains(TEXT("vendor")) || R.Contains(TEXT("kiosk"))) { return TEXT("verkauft Waren"); }
		if (R.Contains(TEXT("bartender"))) { return TEXT("zapft Bier"); }
		if (R.Contains(TEXT("police"))) { return TEXT("im Dienst"); }
		if (R.Contains(TEXT("mechanic"))) { return TEXT("schraubt"); }
		if (R.Contains(TEXT("doctor"))) { return TEXT("behandelt Patienten"); }
		if (R.Contains(TEXT("musician"))) { return TEXT("spielt Musik"); }
		if (R.Contains(TEXT("radio"))) { return TEXT("moderiert"); }
		if (R.Contains(TEXT("barista"))) { return TEXT("macht Kaffee"); }
		if (R.Contains(TEXT("dock")) || R.Contains(TEXT("smuggler"))) { return TEXT("schleppt Kisten"); }
		if (R.Contains(TEXT("bouncer"))) { return TEXT("bewacht die Tür"); }
		return TEXT("arbeitet");
	}
	case EMTAActivity::Shift: return TEXT("auf Schicht");
	case EMTAActivity::Patrol: return TEXT("auf Streife");
	case EMTAActivity::Eat: return PickVariant({ TEXT("isst"), TEXT("isst eine Kleinigkeit"), TEXT("trinkt Kaffee"), TEXT("isst Nudeln") });
	case EMTAActivity::Bar: return PickVariant({ TEXT("trinkt ein Bier"), TEXT("an der Bar"), TEXT("trinkt was"), TEXT("stößt an") });
	case EMTAActivity::Market:
		if (D.bTrading) { return TEXT("verkauft Waren"); }
		return PickVariant({ TEXT("kauft ein"), TEXT("schaut sich Waren an"), TEXT("feilscht") });
	case EMTAActivity::Leisure:
		return PickVariant({ TEXT("liest Zeitung"), TEXT("schaut aufs Handy"), TEXT("raucht"), TEXT("sitzt auf der Bank"),
			TEXT("wartet"), TEXT("hört Musik"), TEXT("schaut in die Ferne") });
	case EMTAActivity::Commute: return PickVariant({ TEXT("wartet auf die Bahn"), TEXT("auf dem Weg zur Arbeit") });
	case EMTAActivity::Perform: return TEXT("spielt Musik");
	case EMTAActivity::Worship: return TEXT("betet");
	case EMTAActivity::Strike: return TEXT("streikt");
	case EMTAActivity::Shelter: return TEXT("stellt sich unter");
	case EMTAActivity::Talk: return TEXT("im Gespräch");
	default: return TEXT("");
	}
}

int32 AMTALivingWorldManager::AcquireSlot(FName Key, int32 Index)
{
	TArray<int32>& Owners = SlotOwners.FindOrAdd(Key);
	for (int32 S = 0; S < Owners.Num(); ++S)
	{
		if (Owners[S] == INDEX_NONE)
		{
			Owners[S] = Index;
			return S;
		}
	}
	return Owners.Add(Index);
}

void AMTALivingWorldManager::ReleaseSlot(int32 Index)
{
	FMTALivingAgent& A = Agents[Index];
	if (A.Slot != INDEX_NONE && !A.SlotKey.IsNone())
	{
		if (TArray<int32>* Owners = SlotOwners.Find(A.SlotKey))
		{
			if (Owners->IsValidIndex(A.Slot) && (*Owners)[A.Slot] == Index)
			{
				(*Owners)[A.Slot] = INDEX_NONE;
			}
		}
	}
	A.Slot = INDEX_NONE;
	A.SlotKey = NAME_None;
}

void AMTALivingWorldManager::ComputeSlotPose(FMTALivingAgent& A) const
{
	const FVector P = A.BaseSpot.Pos;
	const FVector T = A.BaseSpot.Tangent;
	const FVector N = A.BaseSpot.Normal;
	if (A.bIndoorAtTarget)
	{
		// Haustuer: an der Wand, Blick ins Haus
		A.TargetPos = P + N * 60.f;
		A.TargetYaw = LivingYawOf(N);
		return;
	}
	if (A.Slot == INDEX_NONE)
	{
		// Freier Punkt (Pause vor der Arbeit): an der Wand, Blick zur Strasse
		A.TargetPos = P + N * 80.f;
		A.TargetYaw = LivingYawOf(-N);
		return;
	}
	if (LivingIsGroupActivity(A.Activity, A.bTrading))
	{
		// Grueppchen im Kreis, Blick zur Mitte (sieht nach Unterhaltung aus)
		if (A.Slot < 5)
		{
			const float Ang = FMath::DegreesToRadians(30.f + 72.f * (float)A.Slot);
			const FVector Off = (T * FMath::Cos(Ang) + N * FMath::Sin(Ang)) * 100.f;
			A.TargetPos = P + Off;
			A.TargetYaw = LivingYawOf(-Off);
		}
		else
		{
			const int32 K = A.Slot - 5;
			const float Along = (float)(K / 2 + 1) * 240.f * ((K % 2) ? 1.f : -1.f);
			A.TargetPos = P + T * Along + N * 60.f;
			A.TargetYaw = LivingYawOf(P - A.TargetPos);
		}
	}
	else
	{
		// Reihe an der Hauswand (Stand, Arbeit, Essen), Blick zur Strasse
		const int32 K = A.Slot;
		const float Along = (float)((K + 1) / 2) * 170.f * ((K % 2) ? 1.f : -1.f);
		A.TargetPos = P + T * Along + N * 90.f;
		A.TargetYaw = LivingYawOf(-N);
	}
	A.TargetPos.Z = P.Z;
}

void AMTALivingWorldManager::ApplyDesired(int32 Index, const FMTALivingDesired& D, bool bTeleport)
{
	ReleaseSlot(Index);
	FMTALivingAgent& A = Agents[Index];
	A.Activity = D.Activity;
	A.LocationTag = D.Tag;
	A.CurrentDistrict = D.District;
	A.LocationName = D.LocationName;
	A.LocationKind = D.LocationKind;
	A.bTalkable = D.bTalkable;
	A.bTrading = D.bTrading;
	A.bWander = D.bWander;
	A.bOnBreak = D.bOnBreak;
	A.bIndoorAtTarget = D.bIndoors;
	A.BaseSpot = D.Spot;
	if (!D.bIndoors && !D.Tag.IsNone())
	{
		A.Slot = AcquireSlot(D.Tag, Index);
		A.SlotKey = D.Tag;
	}
	ComputeSlotPose(A);
	A.ActivityText = MakeActivityText(A, D, GetCurrentHour());
	A.bWanderWalk = false;

	if (bTeleport)
	{
		A.SimPos = A.TargetPos;
		A.SimYaw = A.TargetYaw;
		A.Path.Reset();
		A.PathIndex = 0;
		A.Phase = D.bIndoors ? EMTALivingPhase::Indoors : EMTALivingPhase::Idle;
		AMTALivingNPC* Npc = A.Actor.Get();
		if (Npc && !bSelfTestRunning)
		{
			Npc->SetActorLocationAndRotation(A.TargetPos + FVector(0.f, 0.f, GLivingCapsuleLift), FRotator(0.f, A.TargetYaw, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
		}
		PushTextToActor(Index);
	}
	else
	{
		RouteTo(Index, A.TargetPos);
	}
}

void AMTALivingWorldManager::EvaluateAgent(int32 Index, float Hour, bool bTeleport)
{
	if (!Agents.IsValidIndex(Index))
	{
		return;
	}
	const FMTALivingAgent& A = Agents[Index];
	if (!bTeleport)
	{
		if (A.Phase == EMTALivingPhase::Encounter || A.Phase == EMTALivingPhase::TalkingToPlayer || A.PinnedUntil > GetNow())
		{
			return;
		}
	}
	FMTALivingDesired D;
	if (!ComputeDesired(A, Hour, D))
	{
		return;
	}
	if (bTeleport || DesiredDiffers(A, D))
	{
		ApplyDesired(Index, D, bTeleport);
	}
}

// ================================================================================================
// Wege (ohne NavMesh: Laufspuren entlang der Gehwege, Querung an Kreuzungen)
// ================================================================================================

FVector AMTALivingWorldManager::SnapToWalkNetwork(const FVector& P, bool& bOutOnXLine) const
{
	if (WalkLinesX.Num() == 0 || WalkLinesY.Num() == 0)
	{
		bOutOnXLine = true;
		return P;
	}
	const float NX = LivingNearest(WalkLinesX, (float)P.X);
	const float NY = LivingNearest(WalkLinesY, (float)P.Y);
	const float CX = FMath::Clamp((float)P.X, WalkLinesX[0], WalkLinesX.Last());
	const float CY = FMath::Clamp((float)P.Y, WalkLinesY[0], WalkLinesY.Last());
	// Kandidat A: auf der Linie x = NX (frei in Y); Kandidat B: auf der Linie y = NY (frei in X)
	const float CostA = FMath::Abs((float)P.X - NX) + FMath::Abs((float)P.Y - CY);
	const float CostB = FMath::Abs((float)P.Y - NY) + FMath::Abs((float)P.X - CX);
	if (CostA <= CostB)
	{
		bOutOnXLine = true;
		return FVector(NX, CY, P.Z);
	}
	bOutOnXLine = false;
	return FVector(CX, NY, P.Z);
}

void AMTALivingWorldManager::AppendSegment(TArray<FMTALivingWaypoint>& OutPath, const FVector& From, const FVector& To) const
{
	const bool bAlongY = FMath::Abs(From.X - To.X) < 1.f;
	const bool bAlongX = FMath::Abs(From.Y - To.Y) < 1.f;
	const float Margin = RoadHalfWidth + 60.f;

	// Bordsteine vor jeder Strassenquerung einfuegen (dort wird kurz gewartet)
	if (bAlongY && !bAlongX)
	{
		const float Dir = To.Y > From.Y ? 1.f : -1.f;
		TArray<float> Curbs;
		for (const float RY : RoadYs)
		{
			const float CurbY = RY - Dir * Margin;
			const bool bAhead = Dir > 0.f ? (CurbY > From.Y + 30.f && RY < To.Y) : (CurbY < From.Y - 30.f && RY > To.Y);
			if (bAhead) { Curbs.Add(CurbY); }
		}
		Curbs.Sort([Dir](float L, float R) { return Dir > 0.f ? L < R : L > R; });
		for (const float CY : Curbs)
		{
			FMTALivingWaypoint W;
			W.Pos = FVector(From.X, CY, 0.f);
			W.bCurb = true;
			OutPath.Add(W);
		}
	}
	else if (bAlongX && !bAlongY)
	{
		const float Dir = To.X > From.X ? 1.f : -1.f;
		TArray<float> Curbs;
		for (const float RX : RoadXs)
		{
			const float CurbX = RX - Dir * Margin;
			const bool bAhead = Dir > 0.f ? (CurbX > From.X + 30.f && RX < To.X) : (CurbX < From.X - 30.f && RX > To.X);
			if (bAhead) { Curbs.Add(CurbX); }
		}
		Curbs.Sort([Dir](float L, float R) { return Dir > 0.f ? L < R : L > R; });
		for (const float CX : Curbs)
		{
			FMTALivingWaypoint W;
			W.Pos = FVector(CX, From.Y, 0.f);
			W.bCurb = true;
			OutPath.Add(W);
		}
	}

	if (OutPath.Num() == 0 || FVector::Dist2D(OutPath.Last().Pos, To) > 20.f)
	{
		FMTALivingWaypoint W;
		W.Pos = FVector(To.X, To.Y, 0.f);
		OutPath.Add(W);
	}
}

void AMTALivingWorldManager::BuildPath(const FVector& From, const FVector& To, TArray<FMTALivingWaypoint>& OutPath) const
{
	OutPath.Reset();
	bool bFromX = true;
	bool bToX = true;
	const FVector S = SnapToWalkNetwork(From, bFromX);
	const FVector G = SnapToWalkNetwork(To, bToX);

	TArray<FVector> Pts;
	Pts.Add(S);
	const bool bSameX = FMath::Abs(S.X - G.X) < 1.f;
	const bool bSameY = FMath::Abs(S.Y - G.Y) < 1.f;
	if ((bFromX && bToX && bSameX) || (!bFromX && !bToX && bSameY))
	{
		// Gleiche Laufspur: direkt
	}
	else if (bFromX && !bToX)
	{
		Pts.Add(FVector(S.X, G.Y, 0.f));
	}
	else if (!bFromX && bToX)
	{
		Pts.Add(FVector(G.X, S.Y, 0.f));
	}
	else if (bFromX && bToX)
	{
		// Beide frei in Y auf verschiedenen Linien: ueber eine Querspur y = YC wechseln
		float Best = S.Y;
		float BestCost = TNumericLimits<float>::Max();
		for (const float Y : WalkLinesY)
		{
			const float Cost = FMath::Abs((float)S.Y - Y) + FMath::Abs((float)G.Y - Y) + 0.001f * FMath::Abs((float)S.Y - Y);
			if (Cost < BestCost) { BestCost = Cost; Best = Y; }
		}
		Pts.Add(FVector(S.X, Best, 0.f));
		Pts.Add(FVector(G.X, Best, 0.f));
	}
	else
	{
		float Best = S.X;
		float BestCost = TNumericLimits<float>::Max();
		for (const float X : WalkLinesX)
		{
			const float Cost = FMath::Abs((float)S.X - X) + FMath::Abs((float)G.X - X) + 0.001f * FMath::Abs((float)S.X - X);
			if (Cost < BestCost) { BestCost = Cost; Best = X; }
		}
		Pts.Add(FVector(Best, S.Y, 0.f));
		Pts.Add(FVector(Best, G.Y, 0.f));
	}
	Pts.Add(G);

	// Vom Standort auf die Laufspur
	if (FVector::Dist2D(From, S) > 30.f)
	{
		FMTALivingWaypoint W;
		W.Pos = FVector(S.X, S.Y, 0.f);
		OutPath.Add(W);
	}
	for (int32 I = 0; I + 1 < Pts.Num(); ++I)
	{
		if (FVector::Dist2D(Pts[I], Pts[I + 1]) > 1.f)
		{
			AppendSegment(OutPath, Pts[I], Pts[I + 1]);
		}
	}
	// Von der Laufspur zum Platz
	if (OutPath.Num() == 0 || FVector::Dist2D(OutPath.Last().Pos, To) > 20.f)
	{
		FMTALivingWaypoint W;
		W.Pos = FVector(To.X, To.Y, 0.f);
		OutPath.Add(W);
	}
}

FVector AMTALivingWorldManager::GetAgentLocation(const FMTALivingAgent& A) const
{
	if (!bSelfTestRunning)
	{
		if (const AMTALivingNPC* Npc = A.Actor.Get())
		{
			return Npc->GetActorLocation();
		}
	}
	return A.SimPos;
}

void AMTALivingWorldManager::RouteTo(int32 Index, const FVector& Dest)
{
	FMTALivingAgent& A = Agents[Index];
	const FVector From = GetAgentLocation(A);
	A.WaitTime = 0.f;
	A.StuckTime = 0.f;
	A.SidestepTime = 0.f;
	if (FVector::Dist2D(From, Dest) < 60.f)
	{
		OnArrived(Index);
		return;
	}
	BuildPath(From, Dest, A.Path);
	A.PathIndex = 0;
	A.Phase = EMTALivingPhase::Walking;
	PushTextToActor(Index);
}

void AMTALivingWorldManager::OnArrived(int32 Index)
{
	FMTALivingAgent& A = Agents[Index];
	A.Path.Reset();
	A.PathIndex = 0;
	A.WaitTime = 0.f;
	A.StuckTime = 0.f;
	const double Now = GetNow();
	if (A.bWanderWalk)
	{
		A.bWanderWalk = false;
		A.Phase = EMTALivingPhase::Idle;
		A.NextWanderTime = Now + SimRng.FRandRange(5.0, 12.0);
		PushTextToActor(Index);
		return;
	}
	A.Phase = A.bIndoorAtTarget ? EMTALivingPhase::Indoors : EMTALivingPhase::Idle;
	A.NextWanderTime = Now + SimRng.FRandRange(4.0, 10.0);
	// Drinnen: der Actor geht beim naechsten LOD-Durchlauf zurueck in den Pool ("betritt das Haus").
	PushTextToActor(Index);
}

// ================================================================================================
// Bewegung und Darstellung
// ================================================================================================

void AMTALivingWorldManager::SteerVisible(int32 Index, float DeltaSeconds)
{
	FMTALivingAgent& A = Agents[Index];
	AMTALivingNPC* Npc = A.Actor.Get();
	if (!Npc)
	{
		return;
	}
	const FVector Loc = Npc->GetActorLocation();
	const double Now = GetNow();

	// Begegnung: der Initiator geht auf den Partner zu
	if (A.Phase == EMTALivingPhase::Encounter)
	{
		const FMTALivingEncounter* Enc = FindEncounter(A.EncounterId);
		if (Enc && Enc->A == Index && (!Enc->bTalking || Enc->bOneSided) && Agents.IsValidIndex(Enc->B))
		{
			const FVector Other = GetAgentLocation(Agents[Enc->B]);
			const float Stop = Enc->bOneSided ? 420.f : 170.f;
			FVector To = Other - Loc;
			To.Z = 0.f;
			const float Dist = (float)To.Size();
			if (Dist > Stop)
			{
				Npc->AddMovementInput(To / Dist, Enc->bOneSided ? 0.6f : 1.f);
				return;
			}
		}
	}
	else if (A.Phase == EMTALivingPhase::Walking)
	{
		if (A.WaitTime > 0.f)
		{
			A.WaitTime -= DeltaSeconds;
			return;
		}
		if (!A.Path.IsValidIndex(A.PathIndex))
		{
			OnArrived(Index);
			return;
		}
		const FMTALivingWaypoint& WP = A.Path[A.PathIndex];
		FVector To = WP.Pos - Loc;
		To.Z = 0.f;
		const float Dist = (float)To.Size();
		if (Dist < 60.f)
		{
			if (WP.bCurb)
			{
				// Am Bordstein kurz warten (Ampel/Verkehr)
				A.WaitTime = (float)SimRng.FRandRange(0.3, 2.5);
			}
			++A.PathIndex;
			if (!A.Path.IsValidIndex(A.PathIndex))
			{
				OnArrived(Index);
			}
			return;
		}
		FVector Dir = To / Dist;

		// Haengen bleiben (Laterne, Baum, Spieler): ausweichen, im Notfall ein Stueck versetzen
		const float Speed2D = (float)Npc->GetVelocity().Size2D();
		A.StuckTime = Speed2D < 25.f ? A.StuckTime + DeltaSeconds : FMath::Max(0.f, A.StuckTime - DeltaSeconds);
		if (A.StuckTime > 1.2f && A.SidestepTime <= 0.f)
		{
			A.SidestepTime = 0.8f;
			A.SidestepSign = SimRng.FRand() < 0.5f ? -1.f : 1.f;
		}
		if (A.SidestepTime > 0.f)
		{
			A.SidestepTime -= DeltaSeconds;
			const FVector Side(-Dir.Y * A.SidestepSign, Dir.X * A.SidestepSign, 0.f);
			Dir = (Dir * 0.4f + Side).GetSafeNormal();
		}
		if (A.StuckTime > 6.f)
		{
			const FVector Jump = Loc + (To / Dist) * FMath::Min(Dist, 200.f);
			Npc->SetActorLocation(FVector(Jump.X, Jump.Y, Loc.Z), false, nullptr, ETeleportType::TeleportPhysics);
			A.StuckTime = 0.f;
		}
		Npc->AddMovementInput(Dir, 1.f);
		return;
	}

	// Stehen: Blickrichtung (Partner/Spieler, sonst Platzausrichtung mit gelegentlichem Umsehen)
	float Desired = (float)Npc->GetActorRotation().Yaw;
	bool bTurn = false;
	if (Now < A.FaceUntil)
	{
		Desired = LivingYawOf(A.FacePoint - Loc);
		bTurn = true;
	}
	else if (A.Phase == EMTALivingPhase::Idle && FVector::Dist2D(Loc, A.TargetPos) < 120.f)
	{
		Desired = A.TargetYaw + A.LookYawOffset;
		bTurn = true;
	}
	if (bTurn)
	{
		const float Cur = (float)Npc->GetActorRotation().Yaw;
		const float NewYaw = FMath::FixedTurn(Cur, Desired, 220.f * DeltaSeconds);
		if (!FMath::IsNearlyEqual(NewYaw, Cur, 0.05f))
		{
			Npc->SetActorRotation(FRotator(0.f, NewYaw, 0.f));
		}
	}
}

void AMTALivingWorldManager::AdvanceHidden(int32 Index, float DeltaSeconds, bool bIgnoreActors)
{
	FMTALivingAgent& A = Agents[Index];
	if (!bIgnoreActors && A.Actor.IsValid())
	{
		return;
	}
	if (A.Phase != EMTALivingPhase::Walking)
	{
		return;
	}
	float Budget = HiddenTravelSpeed * DeltaSeconds;
	while (Budget > 0.f && A.Path.IsValidIndex(A.PathIndex))
	{
		const FVector Target = A.Path[A.PathIndex].Pos;
		FVector Delta = Target - A.SimPos;
		Delta.Z = 0.f;
		const float Dist = (float)Delta.Size();
		if (Dist <= Budget)
		{
			A.SimPos.X = Target.X;
			A.SimPos.Y = Target.Y;
			Budget -= Dist;
			++A.PathIndex;
		}
		else
		{
			A.SimPos += Delta / Dist * Budget;
			A.SimYaw = LivingYawOf(Delta);
			Budget = 0.f;
		}
	}
	A.SimPos.Z = GroundZAt(A.SimPos);
	if (!A.Path.IsValidIndex(A.PathIndex))
	{
		OnArrived(Index);
	}
}

void AMTALivingWorldManager::UpdateIdleBehaviour(int32 Index, double Now)
{
	FMTALivingAgent& A = Agents[Index];
	if (A.Phase != EMTALivingPhase::Idle)
	{
		return;
	}
	if (A.bWander && Now >= A.NextWanderTime)
	{
		// Streife/Spaziergang: ein Stueck den Gehweg entlang
		const FVector Dest = A.BaseSpot.Pos + A.BaseSpot.Tangent * SimRng.FRandRange(-1300.0, 1300.0) + A.BaseSpot.Normal * SimRng.FRandRange(-40.0, 60.0);
		A.bWanderWalk = true;
		RouteTo(Index, Dest);
		return;
	}
	if (Now >= A.NextLookTime)
	{
		A.LookYawOffset = SimRng.FRand() < 0.5f ? 0.f : (float)SimRng.FRandRange(-40.0, 40.0);
		A.NextLookTime = Now + SimRng.FRandRange(4.0, 11.0);
	}
	// Vom Platz abgedraengt (Spieler, Ausweichen): zurueckgehen
	if (!A.bWander && FVector::Dist2D(GetAgentLocation(A), A.TargetPos) > 250.f)
	{
		RouteTo(Index, A.TargetPos);
	}
}

void AMTALivingWorldManager::PushTextToActor(int32 Index)
{
	if (bSelfTestRunning || !Agents.IsValidIndex(Index))
	{
		return;
	}
	const FMTALivingAgent& A = Agents[Index];
	AMTALivingNPC* Npc = A.Actor.Get();
	if (!Npc)
	{
		return;
	}
	FString Text;
	switch (A.Phase)
	{
	case EMTALivingPhase::Encounter:
		Text = A.EncounterText;
		break;
	case EMTALivingPhase::TalkingToPlayer:
		Text = TEXT("im Gespräch");
		break;
	case EMTALivingPhase::Walking:
		if (A.bWanderWalk)
		{
			Text = A.ActivityText;
		}
		else if (A.bIndoorAtTarget && (A.Activity == EMTAActivity::Sleep || A.Activity == EMTAActivity::Home))
		{
			Text = TEXT("auf dem Heimweg");
		}
		else
		{
			Text = FString::Printf(TEXT("unterwegs: %s"), *A.LocationName);
		}
		break;
	default:
		Text = A.ActivityText;
		break;
	}
	Npc->SetActivityText(Text);
	Npc->SetWalkSpeed(A.WalkSpeed);
}

void AMTALivingWorldManager::AssignActor(int32 Index)
{
	UWorld* World = GetWorld();
	if (!World || !Agents.IsValidIndex(Index))
	{
		return;
	}
	FMTALivingAgent& A = Agents[Index];

	AMTALivingNPC* Npc = nullptr;
	while (!Npc && ActorPool.Num() > 0)
	{
		AMTALivingNPC* Candidate = ActorPool.Pop();
		if (IsValid(Candidate))
		{
			Npc = Candidate;
		}
	}
	if (!Npc)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.Owner = this;
		Npc = World->SpawnActor<AMTALivingNPC>(AMTALivingNPC::StaticClass(), A.SimPos + FVector(0.f, 0.f, GLivingCapsuleLift),
			FRotator(0.f, A.SimYaw, 0.f), SpawnParams);
		if (!Npc)
		{
			return;
		}
		SpawnedActors.Add(Npc);
	}

	// Boden suchen (nur statische Geometrie, keine Figuren)
	FVector Ground(A.SimPos.X, A.SimPos.Y, GroundZAt(A.SimPos));
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MTALivingGround), false);
	Query.AddIgnoredActor(Npc);
	if (World->LineTraceSingleByObjectType(Hit, Ground + FVector(0.f, 0.f, 300.f), Ground - FVector(0.f, 0.f, 400.f),
		FCollisionObjectQueryParams(ECC_WorldStatic), Query) && Hit.ImpactNormal.Z > 0.7f)
	{
		Ground.Z = Hit.ImpactPoint.Z;
	}
	const float Yaw = A.Phase == EMTALivingPhase::Idle ? A.TargetYaw : A.SimYaw;
	Npc->SetActorLocationAndRotation(Ground + FVector(0.f, 0.f, GLivingCapsuleLift * A.Scale), FRotator(0.f, Yaw, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	Npc->BindAgent(this, Index, A.Id, A.DisplayName, A.bKey, A.bHasDialogue, A.WalkSpeed, A.Scale);
	Npc->SetNameLabel(A.bHasDialogue || A.Occupation.IsEmpty() ? A.DisplayName : FString::Printf(TEXT("%s (%s)"), *A.DisplayName, *A.Occupation));
	A.Actor = Npc;
	PushTextToActor(Index);
	if (A.bKey && NPCSys)
	{
		// Sim-LOD "Near": Barks, sichtbare Interaktionen und Aktivitaetswechsel gehen an diesen Actor.
		NPCSys->RegisterNPCActor(A.Id, Npc);
	}
}

void AMTALivingWorldManager::ReleaseActor(int32 Index)
{
	FMTALivingAgent& A = Agents[Index];
	AMTALivingNPC* Npc = A.Actor.Get();
	A.Actor.Reset();
	if (!Npc)
	{
		return;
	}
	const FVector L = Npc->GetActorLocation();
	A.SimPos = FVector(L.X, L.Y, GroundZAt(L));
	A.SimYaw = (float)Npc->GetActorRotation().Yaw;
	if (A.bKey && NPCSys && NPCSys->GetNPCActor(A.Id) == Npc)
	{
		NPCSys->UnregisterNPCActor(A.Id);
	}
	Npc->UnbindAgent();
	ActorPool.Add(Npc);
}

void AMTALivingWorldManager::UpdateLOD()
{
	if (bSelfTestRunning)
	{
		return;
	}
	FVector ViewLoc;
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		ViewLoc = Pawn->GetActorLocation();
	}
	else if (const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		ViewLoc = Cam->GetCameraLocation();
	}
	else
	{
		return;
	}
	const double Now = GetNow();
	const float SpawnSq = SpawnRadius * SpawnRadius;
	const float DespawnSq = DespawnRadius * DespawnRadius;

	TArray<TPair<float, int32>> Cands;
	for (int32 I = 0; I < Agents.Num(); ++I)
	{
		const FMTALivingAgent& A = Agents[I];
		const bool bPinned = A.Phase == EMTALivingPhase::TalkingToPlayer || A.PinnedUntil > Now;
		if (!A.bActiveInWorld || (A.Phase == EMTALivingPhase::Indoors && !bPinned))
		{
			continue;
		}
		const float D2 = (float)FVector::DistSquared2D(GetAgentLocation(A), ViewLoc);
		if (!bPinned && D2 > (A.Actor.IsValid() ? DespawnSq : SpawnSq))
		{
			continue;
		}
		Cands.Emplace(bPinned ? -1.f : D2, I);
	}
	Cands.Sort([](const TPair<float, int32>& L, const TPair<float, int32>& R) { return L.Key < R.Key; });

	const int32 Budget = FMath::Clamp(MaxVisibleNPCs, 0, 80);
	TArray<bool> Wanted;
	Wanted.Init(false, Agents.Num());
	for (int32 K = 0; K < Cands.Num() && K < Budget; ++K)
	{
		Wanted[Cands[K].Value] = true;
	}

	// Erst freigeben (fuellt den Pool), dann zuweisen
	for (int32 I = 0; I < Agents.Num(); ++I)
	{
		if (!Wanted[I] && Agents[I].Actor.IsValid())
		{
			ReleaseActor(I);
		}
		else if (!Agents[I].Actor.IsValid())
		{
			Agents[I].Actor.Reset();
		}
	}
	int32 NewSpawns = 0;
	for (int32 K = 0; K < Cands.Num() && K < Budget; ++K)
	{
		const int32 Idx = Cands[K].Value;
		if (Agents[Idx].Actor.IsValid())
		{
			continue;
		}
		if (ActorPool.Num() == 0)
		{
			if (NewSpawns >= MaxSpawnsPerUpdate)
			{
				continue;
			}
			++NewSpawns;
		}
		AssignActor(Idx);
	}

	VisibleIndices.Reset();
	for (int32 I = 0; I < Agents.Num(); ++I)
	{
		if (AMTALivingNPC* Npc = Agents[I].Actor.Get())
		{
			VisibleIndices.Add(I);
			const float D = (float)FVector::Dist2D(Npc->GetActorLocation(), ViewLoc);
			Npc->SetLabelRange(D < LabelDistance, D < SpeechDistance);
		}
	}
}

// ================================================================================================
// NPC-NPC-Begegnungen
// ================================================================================================

uint64 AMTALivingWorldManager::MakePairKey(int32 IA, int32 IB)
{
	const uint32 Lo = (uint32)FMath::Min(IA, IB);
	const uint32 Hi = (uint32)FMath::Max(IA, IB);
	return ((uint64)Lo << 32) | (uint64)Hi;
}

FMTALivingEncounter* AMTALivingWorldManager::FindEncounter(int32 EncounterId)
{
	for (FMTALivingEncounter& E : Encounters)
	{
		if (E.Id == EncounterId) { return &E; }
	}
	return nullptr;
}

bool AMTALivingWorldManager::IsSocialEligible(const FMTALivingAgent& A, double Now) const
{
	if (!A.Actor.IsValid() || A.EncounterId != INDEX_NONE || Now < A.NextSocialTime || A.PinnedUntil > Now || !A.bActiveInWorld)
	{
		return false;
	}
	if (A.Phase != EMTALivingPhase::Idle && A.Phase != EMTALivingPhase::Walking)
	{
		return false;
	}
	if (A.bKey && NPCSys)
	{
		const FMTANPCRuntime* RT = NPCSys->FindRuntime(A.Id);
		if (!RT || !RT->bActive || RT->BusyUntil > NPCSys->GetTotalHours())
		{
			return false;
		}
	}
	return true;
}

void AMTALivingWorldManager::UpdateSocial(double Now)
{
	if (!bEnableSocial || Encounters.Num() >= MaxConcurrentEncounters)
	{
		return;
	}
	const float SocialSq = SocialRadius * SocialRadius;
	for (int32 II = 0; II < VisibleIndices.Num(); ++II)
	{
		const int32 I = VisibleIndices[II];
		if (!IsSocialEligible(Agents[I], Now))
		{
			continue;
		}
		const FVector LI = GetAgentLocation(Agents[I]);
		for (int32 JJ = II + 1; JJ < VisibleIndices.Num(); ++JJ)
		{
			const int32 J = VisibleIndices[JJ];
			if (!IsSocialEligible(Agents[J], Now))
			{
				continue;
			}
			const float D2 = (float)FVector::DistSquared2D(LI, GetAgentLocation(Agents[J]));
			const bool bBothIdle = Agents[I].Phase == EMTALivingPhase::Idle && Agents[J].Phase == EMTALivingPhase::Idle;
			bool bCandidate = bBothIdle && D2 < SocialSq;
			if (!bCandidate && Agents[I].bKey && Agents[J].bKey && D2 < 260.f * 260.f && RelationSys)
			{
				// Bekannte begegnen sich unterwegs und bleiben stehen
				bCandidate = RelationSys->GetAffinity(Agents[I].Id, Agents[J].Id) >= 25.f;
			}
			if (!bCandidate)
			{
				continue;
			}
			const uint64 Key = MakePairKey(I, J);
			if (const double* Cd = PairCooldowns.Find(Key))
			{
				if (*Cd > Now) { continue; }
			}
			if (TryStartEncounter(I, J, false, nullptr))
			{
				return; // hoechstens eine neue Begegnung je Pruefung
			}
			PairCooldowns.Add(Key, Now + 40.0);
		}
	}
}

FString AMTALivingWorldManager::GetBarkLine(FName Category, FName District, FName Faction, FName InRole) const
{
	if (!DialogueSys)
	{
		return FString();
	}
	FMTABarkContext Ctx;
	Ctx.Category = Category;
	Ctx.District = District;
	Ctx.Faction = Faction;
	Ctx.Role = InRole;
	Ctx.bIgnoreCooldown = true;
	return DialogueSys->GetBark(Ctx);
}

bool AMTALivingWorldManager::TryStartEncounter(int32 IA, int32 IB, bool bForce, const FMTANPCInteractionEvent* SimEvent)
{
	if (IA == IB || !Agents.IsValidIndex(IA) || !Agents.IsValidIndex(IB))
	{
		return false;
	}
	FMTALivingAgent& A = Agents[IA];
	FMTALivingAgent& B = Agents[IB];
	if (A.EncounterId != INDEX_NONE || B.EncounterId != INDEX_NONE)
	{
		return false;
	}
	const double Now = GetNow();
	const FName District = A.CurrentDistrict.IsNone() ? A.HomeDistrict : A.CurrentDistrict;

	FMTANPCInteractionEvent Ev;
	bool bApplyEffects = false;
	if (SimEvent)
	{
		// Von der stuendlichen Simulation gewaehlt und bereits angewendet: nur darstellen.
		Ev = *SimEvent;
	}
	else if (A.bKey && B.bKey && RelationSys && NPCSys)
	{
		FMTARelationship Edge;
		const bool bEdge = RelationSys->GetRelationship(A.Id, B.Id, Edge);
		const float Chance = bForce ? 1.f : FMath::Clamp(RelationSys->PairInteractionRate * 3.f * (0.5f + (A.Talk + B.Talk) * 0.5f) * (bEdge ? 1.5f : 1.f), 0.05f, 0.9f);
		if (SocialRng.FRand() > Chance)
		{
			return false;
		}
		TArray<FName> Present = { A.Id, B.Id };
		if (!A.LocationTag.IsNone())
		{
			for (const FName& Other : NPCSys->GetNPCsAtLocation(A.LocationTag)) { Present.AddUnique(Other); }
		}
		if (!RelationSys->ChooseInteraction(A.Id, B.Id, Present, SocialRng, Ev))
		{
			return false;
		}
		Ev.bVisible = true;
		Ev.Line = GetBarkLine(LivingBarkCategoryFor(Ev.Type), District, A.Faction, A.NPCRole);
		bApplyEffects = true;
	}
	else
	{
		const float Chance = bForce ? 1.f : FMath::Clamp(0.25f * (A.Talk + B.Talk), 0.05f, 0.6f);
		if (SocialRng.FRand() > Chance)
		{
			return false;
		}
		Ev.A = A.Id;
		Ev.B = B.Id;
		Ev.Location = A.LocationTag;
		Ev.GameHour = NPCSys ? NPCSys->GetTotalHours() : 0.0;
		Ev.bVisible = true;
		const float R = SocialRng.FRand();
		if (R < 0.08f * (A.Temper + B.Temper)) { Ev.Type = EMTAInteractionType::Argue; }
		else if ((A.bTrading || B.bTrading) && R < 0.45f) { Ev.Type = EMTAInteractionType::Trade; }
		else { Ev.Type = EMTAInteractionType::Chat; }
		Ev.Line = GetBarkLine(LivingBarkCategoryFor(Ev.Type), District, A.Faction, A.NPCRole);
	}

	FMTALivingEncounter Enc;
	Enc.Id = NextEncounterId++;
	Enc.A = IA;
	Enc.B = IB;
	Enc.Type = Ev.Type;
	Enc.bOneSided = Ev.Type == EMTAInteractionType::Shadow;
	BuildEncounterLines(Enc, Ev);
	const float Dist = (float)FVector::Dist2D(GetAgentLocation(A), GetAgentLocation(B));
	Enc.bTalking = Dist <= 220.f || bSelfTestRunning || !A.Actor.IsValid();

	A.EncounterId = Enc.Id;
	A.Phase = EMTALivingPhase::Encounter;
	A.EncounterText = FString::Printf(TEXT("%s %s"), *LivingInteractionVerb(Ev.Type), *B.DisplayName);
	if (!Enc.bOneSided)
	{
		B.EncounterId = Enc.Id;
		B.Phase = EMTALivingPhase::Encounter;
		B.EncounterText = FString::Printf(TEXT("%s %s"), *LivingInteractionVerb(Ev.Type == EMTAInteractionType::Mediate ? EMTAInteractionType::Chat : Ev.Type), *A.DisplayName);
	}
	if (AMTALivingNPC* NA = (bSelfTestRunning ? nullptr : A.Actor.Get())) { NA->GetCharacterMovement()->StopMovementImmediately(); }
	if (AMTALivingNPC* NB = (bSelfTestRunning || Enc.bOneSided ? nullptr : B.Actor.Get())) { NB->GetCharacterMovement()->StopMovementImmediately(); }
	Encounters.Add(Enc);
	PushTextToActor(IA);
	PushTextToActor(IB);
	PairCooldowns.Add(MakePairKey(IA, IB), Now + 150.0);

	if (bApplyEffects)
	{
		// Wirkung wie in der stuendlichen Simulation: Beziehung, Geruechte weitererzaehlen, beschaeftigt.
		RelationSys->ApplyInteraction(Ev, SocialRng);
		if (GossipSys && (Ev.Type == EMTAInteractionType::Chat || Ev.Type == EMTAInteractionType::Trade || Ev.Type == EMTAInteractionType::Cooperate))
		{
			GossipSys->ShareBetween(Ev.A, Ev.B, SocialRng);
			GossipSys->ShareBetween(Ev.B, Ev.A, SocialRng);
		}
		NPCSys->SetBusy(Ev.A, 0.25f);
		NPCSys->SetBusy(Ev.B, 0.25f);
		if (!bSelfTestRunning)
		{
			// Delegate OnNPCInteraction (Debug/HUD); die Actors ignorieren das Echo, weil die Begegnung schon laeuft.
			NPCSys->BroadcastInteraction(Ev);
		}
	}
	return true;
}

void AMTALivingWorldManager::BuildEncounterLines(FMTALivingEncounter& Enc, const FMTANPCInteractionEvent& Ev)
{
	const FMTALivingAgent& A = Agents[Enc.A];
	const FName District = A.CurrentDistrict.IsNone() ? A.HomeDistrict : A.CurrentDistrict;
	auto AddLine = [&Enc](uint8 Who, const FString& Line)
	{
		if (!Line.IsEmpty())
		{
			Enc.Lines.Add(Line);
			Enc.Speakers.Add(Who);
		}
	};
	auto OrBark = [this, &A, District](const FString& Preferred, FName Category) -> FString
	{
		return Preferred.IsEmpty() ? GetBarkLine(Category, District, A.Faction, A.NPCRole) : Preferred;
	};

	switch (Ev.Type)
	{
	case EMTAInteractionType::Argue:
		AddLine(0, OrBark(Ev.Line, TEXT("argue")));
		AddLine(1, LivingPick(GLivingArgueReplies, SocialRng));
		AddLine(0, GetBarkLine(TEXT("argue"), District, A.Faction, A.NPCRole));
		AddLine(1, LivingPick(GLivingArgueClosers, SocialRng));
		break;
	case EMTAInteractionType::Mediate:
		AddLine(0, OrBark(Ev.Line, TEXT("mediate")));
		AddLine(1, LivingPick(GLivingMediateReplies, SocialRng));
		break;
	case EMTAInteractionType::Trade:
		AddLine(0, LivingPick(GLivingTradeOpeners, SocialRng));
		AddLine(1, LivingPick(GLivingTradeAnswers, SocialRng));
		AddLine(0, LivingPick(GLivingTradeClosers, SocialRng));
		break;
	case EMTAInteractionType::Cooperate:
		AddLine(0, LivingPick(GLivingCoopOpeners, SocialRng));
		AddLine(1, LivingPick(GLivingCoopAnswers, SocialRng));
		break;
	case EMTAInteractionType::Betray:
		AddLine(0, TEXT("Ich weiß da etwas. Das ist was wert."));
		AddLine(1, TEXT("Erzähl. Aber leise."));
		AddLine(0, TEXT("Nicht hier. Später."));
		break;
	case EMTAInteractionType::Shadow:
		// Beschatten: stumm aus Distanz beobachten
		break;
	case EMTAInteractionType::Chat:
	default:
	{
		AddLine(0, OrBark(Ev.Line, TEXT("ambient_chat")));
		AddLine(1, LivingPick(GLivingChatReplies, SocialRng));
		// Ein bekanntes Geruecht wird sichtbar weitererzaehlt
		FString Rumor;
		if (A.bKey && GossipSys)
		{
			const TArray<FMTARumor> Known = GossipSys->GetRumorsKnownBy(A.Id);
			if (Known.Num() > 0)
			{
				Rumor = GossipSys->GetRumorText(Known.Last().Id, A.Id);
			}
		}
		if (!Rumor.IsEmpty())
		{
			AddLine(0, FString::Printf(TEXT("Hast du schon gehört? %s"), *Rumor));
			AddLine(1, LivingPick(GLivingRumorReplies, SocialRng));
		}
		else
		{
			AddLine(0, LivingPick(GLivingChatClosers, SocialRng));
		}
		break;
	}
	}
	if (Enc.Lines.Num() == 0 && Ev.Type != EMTAInteractionType::Shadow)
	{
		AddLine(0, LivingPick(GLivingChatReplies, SocialRng));
	}
}

void AMTALivingWorldManager::Speak(int32 Index, const FString& Text, float Duration, FColor Color, bool bBroadcast)
{
	if (!Agents.IsValidIndex(Index) || Text.IsEmpty())
	{
		return;
	}
	FMTALivingAgent& A = Agents[Index];
	A.LastSpeech = Text;
	A.LastSpeechTime = GetNow();
	if (bSelfTestRunning)
	{
		return;
	}
	if (AMTALivingNPC* Npc = A.Actor.Get())
	{
		Npc->ShowSpeech(Text, Duration, Color);
	}
	if (bBroadcast)
	{
		OnLivingSpeech.Broadcast(A.Id, A.DisplayName, Text);
	}
}

void AMTALivingWorldManager::UpdateEncounters(float DeltaSeconds)
{
	const double Now = GetNow();
	for (int32 E = Encounters.Num() - 1; E >= 0; --E)
	{
		FMTALivingEncounter& Enc = Encounters[E];
		if (!Agents.IsValidIndex(Enc.A) || !Agents.IsValidIndex(Enc.B))
		{
			Encounters.RemoveAt(E);
			continue;
		}
		FMTALivingAgent& A = Agents[Enc.A];
		FMTALivingAgent& B = Agents[Enc.B];
		const FVector LA = GetAgentLocation(A);
		const FVector LB = GetAgentLocation(B);

		if (Enc.bOneSided)
		{
			Enc.OneSidedTime += DeltaSeconds;
			A.FacePoint = LB;
			A.FaceUntil = Now + 0.5;
			if (Enc.OneSidedTime > 10.f)
			{
				EndEncounter(E);
			}
			continue;
		}
		if (!Enc.bTalking)
		{
			Enc.ApproachTime += DeltaSeconds;
			if (FVector::Dist2D(LA, LB) <= 220.f || !A.Actor.IsValid() || Enc.ApproachTime > 8.f)
			{
				Enc.bTalking = true;
				Enc.LineTimer = 0.f;
			}
			else
			{
				B.FacePoint = LA;
				B.FaceUntil = Now + 0.5;
				continue;
			}
		}

		// Einander zuwenden
		A.FacePoint = LB;
		A.FaceUntil = Now + 0.5;
		B.FacePoint = LA;
		B.FaceUntil = Now + 0.5;

		Enc.LineTimer -= DeltaSeconds;
		if (Enc.LineTimer > 0.f)
		{
			continue;
		}
		++Enc.LineIndex;
		if (!Enc.Lines.IsValidIndex(Enc.LineIndex))
		{
			EndEncounter(E);
			continue;
		}
		const int32 Speaker = Enc.Speakers[Enc.LineIndex] == 0 ? Enc.A : Enc.B;
		FColor Color = FColor::White;
		if (Enc.Type == EMTAInteractionType::Argue) { Color = FColor(255, 150, 130); }
		else if (Enc.Type == EMTAInteractionType::Mediate) { Color = FColor(170, 255, 170); }
		else if (Enc.Type == EMTAInteractionType::Betray) { Color = FColor(210, 180, 255); }
		else if (Enc.Type == EMTAInteractionType::Trade) { Color = FColor(255, 235, 160); }
		const FString& Line = Enc.Lines[Enc.LineIndex];
		const float Dur = LivingLineDuration(Line);
		Speak(Speaker, Line, Dur, Color, true);
		Enc.LineTimer = Dur + 0.3f;
	}
}

void AMTALivingWorldManager::EndEncounter(int32 EncounterIndex)
{
	if (!Encounters.IsValidIndex(EncounterIndex))
	{
		return;
	}
	const FMTALivingEncounter Enc = Encounters[EncounterIndex];
	Encounters.RemoveAt(EncounterIndex);
	const double Now = GetNow();
	const int32 Members[2] = { Enc.A, Enc.bOneSided ? INDEX_NONE : Enc.B };
	for (const int32 Idx : Members)
	{
		if (!Agents.IsValidIndex(Idx))
		{
			continue;
		}
		FMTALivingAgent& M = Agents[Idx];
		if (M.EncounterId != Enc.Id)
		{
			continue;
		}
		M.EncounterId = INDEX_NONE;
		M.EncounterText.Reset();
		M.NextSocialTime = Now + SocialRng.FRandRange(30.0, 70.0);
		M.FaceUntil = 0.0;
		M.bForceEval = true;
		if (M.Phase == EMTALivingPhase::Encounter)
		{
			M.Phase = EMTALivingPhase::Idle;
			RouteTo(Idx, M.TargetPos);
		}
		PushTextToActor(Idx);
	}
}

// ================================================================================================
// Barks und Spieler
// ================================================================================================

int32 AMTALivingWorldManager::GetPlayerWantedLevel() const
{
	const UWorld* World = GetWorld();
	const AMTA6GameMode* GM = World ? World->GetAuthGameMode<AMTA6GameMode>() : nullptr;
	return GM ? GM->GetWantedStars() : 0;
}

void AMTALivingWorldManager::ChooseBarkCategories(const FMTALivingAgent& A, bool bInVehicle, float SpeedKmh, float Dist, int32 Wanted,
	bool bOnSidewalk, TArray<FName>& OutCategories)
{
	OutCategories.Reset();
	if (Wanted >= 3)
	{
		OutCategories.Add(TEXT("wanted_high"));
	}
	else if (Wanted >= 1 && SimRng.FRand() < 0.7f)
	{
		OutCategories.Add(TEXT("wanted_low"));
	}

	if (bInVehicle)
	{
		if (bOnSidewalk && SpeedKmh > 8.f) { OutCategories.Add(TEXT("sidewalk")); }
		if (SpeedKmh > 70.f) { OutCategories.Add(TEXT("reckless_driving")); }
		else if (SpeedKmh > 35.f && Dist < 450.f) { OutCategories.Add(TEXT("near_miss")); }
		else if (SpeedKmh < 20.f && SimRng.FRand() < 0.4f) { OutCategories.Add(TEXT("nice_car")); }
		return;
	}

	const FName WeatherId = NPCSys ? NPCSys->GetWeather() : FName(TEXT("clear"));
	if ((WeatherId == FName(TEXT("rain")) || WeatherId == FName(TEXT("storm")) || WeatherId == FName(TEXT("fog")) || WeatherId == FName(TEXT("heat")))
		&& SimRng.FRand() < 0.35f)
	{
		OutCategories.Add(FName(*FString::Printf(TEXT("weather_%s"), *WeatherId.ToString())));
	}
	const float Hour = GetCurrentHour();
	if (Hour >= 5.f && Hour < 9.5f && SimRng.FRand() < 0.3f) { OutCategories.Add(TEXT("time_morning")); }
	if ((Hour >= 22.f || Hour < 4.f) && SimRng.FRand() < 0.3f) { OutCategories.Add(TEXT("time_night")); }
	if (!A.Faction.IsNone() && A.Faction != FName(TEXT("none")) && SimRng.FRand() < 0.2f) { OutCategories.Add(TEXT("faction_idle")); }
	OutCategories.Add(TEXT("greeting"));
}

void AMTALivingWorldManager::UpdateBarks(double Now)
{
	if (!bEnableBarks || !DialogueSys || Now < NextGlobalBarkTime || DialogueSys->IsInConversation())
	{
		return;
	}
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		return;
	}
	const bool bInVehicle = !Pawn->IsA<ACharacter>();
	const FVector PLoc = Pawn->GetActorLocation();
	const float SpeedKmh = (float)Pawn->GetVelocity().Size() * 0.036f;
	const float Radius = bInVehicle ? BarkRadiusInVehicle : BarkRadiusOnFoot;

	int32 Best = INDEX_NONE;
	float BestDist = Radius;
	for (const int32 I : VisibleIndices)
	{
		const FMTALivingAgent& A = Agents[I];
		if ((A.Phase != EMTALivingPhase::Idle && A.Phase != EMTALivingPhase::Walking) || Now < A.NextBarkTime)
		{
			continue;
		}
		const AMTALivingNPC* Npc = A.Actor.Get();
		if (!Npc || Npc->IsSpeaking())
		{
			continue;
		}
		const float D = (float)FVector::Dist(Npc->GetActorLocation(), PLoc);
		if (D < BestDist)
		{
			BestDist = D;
			Best = I;
		}
	}
	if (Best == INDEX_NONE)
	{
		return;
	}
	// Nicht jede Figur ruft etwas (sonst wird es laut)
	if (SimRng.FRand() > (bInVehicle ? 0.8f : 0.55f))
	{
		Agents[Best].NextBarkTime = Now + 6.0;
		NextGlobalBarkTime = Now + 1.0;
		return;
	}

	const int32 Wanted = GetPlayerWantedLevel();
	const bool bOnSidewalk = IsInsideGrid(PLoc) && !IsOnRoadLocal(PLoc);
	TArray<FName> Categories;
	ChooseBarkCategories(Agents[Best], bInVehicle, SpeedKmh, BestDist, Wanted, bOnSidewalk, Categories);

	FString Text;
	for (const FName& Cat : Categories)
	{
		const FMTALivingAgent& A = Agents[Best];
		if (A.bKey)
		{
			// Zeigt die Blase ueber IMTANPCActor::PlayBark (-> NotifyBarkShown)
			Text = DialogueSys->PlayNPCBark(A.Id, Cat, Wanted, SpeedKmh);
		}
		else
		{
			FMTABarkContext Ctx;
			Ctx.Category = Cat;
			Ctx.District = A.CurrentDistrict.IsNone() ? A.HomeDistrict : A.CurrentDistrict;
			Ctx.Faction = A.Faction;
			Ctx.Role = A.NPCRole;
			Ctx.WantedLevel = Wanted;
			Ctx.SpeedKmh = SpeedKmh;
			Text = DialogueSys->GetBark(Ctx);
			if (!Text.IsEmpty())
			{
				Speak(Best, Text, FMath::Clamp(2.f + 0.05f * (float)Text.Len(), 2.5f, 5.f), FColor::White, true);
			}
		}
		if (!Text.IsEmpty())
		{
			break;
		}
	}
	FMTALivingAgent& Speaker = Agents[Best];
	Speaker.NextBarkTime = Now + BarkCooldownPerNPC + SimRng.FRandRange(0.0, 10.0);
	NextGlobalBarkTime = Now + (Text.IsEmpty() ? 1.5 : (double)GlobalBarkInterval);
	if (!Text.IsEmpty() && !bInVehicle)
	{
		Speaker.FacePoint = PLoc;
		Speaker.FaceUntil = Now + 3.0;
	}
}

void AMTALivingWorldManager::UpdatePlayerConversation()
{
	const bool bTalking = DialogueSys && DialogueSys->IsInConversation();
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	const double Now = GetNow();
	for (int32 I = 0; I < Agents.Num(); ++I)
	{
		FMTALivingAgent& A = Agents[I];
		if (A.Phase != EMTALivingPhase::TalkingToPlayer)
		{
			continue;
		}
		if (!bTalking)
		{
			A.Phase = EMTALivingPhase::Idle;
			A.bForceEval = true;
			RouteTo(I, A.TargetPos);
			PushTextToActor(I);
		}
		else if (Pawn)
		{
			A.FacePoint = Pawn->GetActorLocation();
			A.FaceUntil = Now + 0.5;
		}
	}
}

void AMTALivingWorldManager::NotifyActivityChanged(int32 AgentIndex)
{
	if (Agents.IsValidIndex(AgentIndex))
	{
		Agents[AgentIndex].bForceEval = true;
	}
}

void AMTALivingWorldManager::NotifyBarkShown(int32 AgentIndex, const FString& Text, FName Category)
{
	if (Agents.IsValidIndex(AgentIndex))
	{
		Speak(AgentIndex, Text, FMath::Clamp(2.f + 0.05f * (float)Text.Len(), 2.5f, 5.f), FColor::White, true);
	}
}

void AMTALivingWorldManager::NotifyInteractionEvent(int32 AgentIndex, const FMTANPCInteractionEvent& Event)
{
	if (!bWorldReady || bSelfTestRunning)
	{
		return;
	}
	// Die Simulation ruft beide Actors auf: nur einmal darstellen.
	const double Now = GetNow();
	const uint32 Hash = HashCombine(HashCombine(GetTypeHash(Event.A), GetTypeHash(Event.B)), GetTypeHash((int64)(Event.GameHour * 100.0)));
	if (Hash == LastSimEventHash && Now - LastSimEventTime < 5.0)
	{
		return;
	}
	LastSimEventHash = Hash;
	LastSimEventTime = Now;

	const int32* IA = AgentById.Find(Event.A);
	const int32* IB = AgentById.Find(Event.B);
	if (!IA || !IB || Agents[*IA].EncounterId != INDEX_NONE || Agents[*IB].EncounterId != INDEX_NONE)
	{
		return;
	}
	const bool bBothVisible = Agents[*IA].Actor.IsValid() && Agents[*IB].Actor.IsValid();
	if (bBothVisible && FVector::Dist2D(GetAgentLocation(Agents[*IA]), GetAgentLocation(Agents[*IB])) < 2500.f
		&& Agents[*IA].Phase != EMTALivingPhase::TalkingToPlayer && Agents[*IB].Phase != EMTALivingPhase::TalkingToPlayer)
	{
		TryStartEncounter(*IA, *IB, true, &Event);
		return;
	}
	// Nur einer sichtbar: ein einzelner Satz
	const int32 Speaker = Agents[*IA].Actor.IsValid() ? *IA : *IB;
	const FMTALivingAgent& S = Agents[Speaker];
	const FString Line = Event.Line.IsEmpty()
		? GetBarkLine(LivingBarkCategoryFor(Event.Type), S.CurrentDistrict, S.Faction, S.NPCRole) : Event.Line;
	Speak(Speaker, Line, 4.f, Event.Type == EMTAInteractionType::Argue ? FColor(255, 150, 130) : FColor::White, true);
}

void AMTALivingWorldManager::NotifyPlayerInteract(int32 AgentIndex, AActor* Interactor)
{
	if (!Agents.IsValidIndex(AgentIndex))
	{
		return;
	}
	// Laufende Begegnung abbrechen
	if (Agents[AgentIndex].EncounterId != INDEX_NONE)
	{
		for (int32 E = 0; E < Encounters.Num(); ++E)
		{
			if (Encounters[E].Id == Agents[AgentIndex].EncounterId)
			{
				EndEncounter(E);
				break;
			}
		}
	}
	FMTALivingAgent& A = Agents[AgentIndex];
	const double Now = GetNow();
	AMTALivingNPC* Npc = A.Actor.Get();
	if (Interactor)
	{
		A.FacePoint = Interactor->GetActorLocation();
		A.FaceUntil = Now + 4.0;
		if (Npc)
		{
			const FVector To = A.FacePoint - Npc->GetActorLocation();
			Npc->SetActorRotation(FRotator(0.f, LivingYawOf(To), 0.f));
		}
	}
	if (A.bHasDialogue && DialogueSys)
	{
		// Nicht ansprechbar -> das Dialogsystem sagt selbst "not_available" (Blase ueber PlayBark).
		DialogueSys->StartConversation(A.Id);
		if (DialogueSys->IsInConversation())
		{
			A.Phase = EMTALivingPhase::TalkingToPlayer;
			A.Path.Reset();
			if (Npc)
			{
				Npc->GetCharacterMovement()->StopMovementImmediately();
			}
			PushTextToActor(AgentIndex);
		}
		return;
	}
	// Ohne Dialog: kurzer Gruss
	const FName District = A.CurrentDistrict.IsNone() ? A.HomeDistrict : A.CurrentDistrict;
	FString Line = GetBarkLine(TEXT("greeting"), District, A.Faction, A.NPCRole);
	if (Line.IsEmpty())
	{
		Line = TEXT("Hallo.");
	}
	Speak(AgentIndex, Line, 3.5f, FColor::White, true);
}

void AMTALivingWorldManager::NotifyConversationState(int32 AgentIndex, bool bInConversation)
{
	if (!Agents.IsValidIndex(AgentIndex))
	{
		return;
	}
	FMTALivingAgent& A = Agents[AgentIndex];
	if (bInConversation)
	{
		A.Phase = EMTALivingPhase::TalkingToPlayer;
		A.Path.Reset();
	}
	else if (A.Phase == EMTALivingPhase::TalkingToPlayer)
	{
		A.Phase = EMTALivingPhase::Idle;
		A.bForceEval = true;
		RouteTo(AgentIndex, A.TargetPos);
	}
	PushTextToActor(AgentIndex);
}

// ================================================================================================
// Abfragen fuer HUD/Tests
// ================================================================================================

AMTALivingNPC* AMTALivingWorldManager::FindNearestTalkableNPC(const FVector& Location, float MaxDistance) const
{
	AMTALivingNPC* Best = nullptr;
	float BestDist = MaxDistance;
	for (const int32 I : VisibleIndices)
	{
		AMTALivingNPC* Npc = Agents[I].Actor.Get();
		if (!Npc)
		{
			continue;
		}
		const float D = (float)FVector::Dist2D(Npc->GetActorLocation(), Location);
		if (D < BestDist)
		{
			BestDist = D;
			Best = Npc;
		}
	}
	return Best;
}

bool AMTALivingWorldManager::GetNearestSpeech(const FVector& Location, float MaxDistance, FString& OutSpeaker, FString& OutText) const
{
	float BestDist = MaxDistance;
	bool bFound = false;
	for (const int32 I : VisibleIndices)
	{
		const AMTALivingNPC* Npc = Agents[I].Actor.Get();
		if (!Npc || !Npc->IsSpeaking())
		{
			continue;
		}
		const float D = (float)FVector::Dist(Npc->GetActorLocation(), Location);
		if (D < BestDist)
		{
			BestDist = D;
			OutSpeaker = Agents[I].DisplayName;
			OutText = Npc->GetSpeechText();
			bFound = true;
		}
	}
	return bFound;
}

AMTALivingNPC* AMTALivingWorldManager::BringDialogueNPCTo(const FVector& Location)
{
	if (!bWorldReady)
	{
		InitializeWorld();
	}
	int32 Pick = INDEX_NONE;
	for (int32 I = 0; I < Agents.Num(); ++I)
	{
		if (!Agents[I].bHasDialogue)
		{
			continue;
		}
		if (Pick == INDEX_NONE)
		{
			Pick = I;
		}
		if (NPCSys && NPCSys->IsAvailableForDialogue(Agents[I].Id))
		{
			Pick = I;
			break;
		}
	}
	if (Pick == INDEX_NONE)
	{
		return nullptr;
	}
	if (Agents[Pick].EncounterId != INDEX_NONE)
	{
		for (int32 E = 0; E < Encounters.Num(); ++E)
		{
			if (Encounters[E].Id == Agents[Pick].EncounterId) { EndEncounter(E); break; }
		}
	}
	FMTALivingAgent& A = Agents[Pick];
	const double Now = GetNow();
	A.PinnedUntil = Now + 60.0;
	A.SimPos = FVector(Location.X, Location.Y, GroundZAt(Location));
	A.TargetPos = A.SimPos;
	A.Path.Reset();
	A.PathIndex = 0;
	A.Phase = EMTALivingPhase::Idle;
	A.bIndoorAtTarget = false;
	A.bWander = false;
	A.ActivityText = TEXT("wartet");

	if (AMTALivingNPC* Npc = A.Actor.Get())
	{
		Npc->SetActorLocation(FVector(Location.X, Location.Y, Location.Z), false, nullptr, ETeleportType::TeleportPhysics);
		PushTextToActor(Pick);
		return Npc;
	}
	// Budget einhalten: notfalls die entfernteste sichtbare Figur freigeben
	if (VisibleIndices.Num() >= FMath::Clamp(MaxVisibleNPCs, 0, 80) && VisibleIndices.Num() > 0)
	{
		int32 Far = VisibleIndices[0];
		float FarDist = -1.f;
		for (const int32 I : VisibleIndices)
		{
			const float D = (float)FVector::DistSquared2D(GetAgentLocation(Agents[I]), Location);
			if (D > FarDist && Agents[I].Phase != EMTALivingPhase::TalkingToPlayer) { FarDist = D; Far = I; }
		}
		ReleaseActor(Far);
		VisibleIndices.Remove(Far);
	}
	AssignActor(Pick);
	VisibleIndices.AddUnique(Pick);
	return Agents[Pick].Actor.Get();
}

// ================================================================================================
// Tick
// ================================================================================================

void AMTALivingWorldManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bWorldReady)
	{
		// Auf den Stadtgenerator warten (hoechstens 3 s), dann notfalls mit dem Standardraster starten.
		InitRetryTime += DeltaSeconds;
		if (AMTACityGenerator::FindCityGenerator(this) || InitRetryTime > 3.f)
		{
			InitializeWorld();
		}
		return;
	}

	const double Now = GetNow();

	// Jedes Frame: sichtbare Figuren steuern, Begegnungen fortschreiben
	for (const int32 I : VisibleIndices)
	{
		SteerVisible(I, DeltaSeconds);
	}
	UpdateEncounters(DeltaSeconds);

	// Langsamer Takt (4 Hz)
	SlowAccumulator += DeltaSeconds;
	if (SlowAccumulator < 0.25f)
	{
		return;
	}
	const float Step = SlowAccumulator;
	SlowAccumulator = 0.f;
	const float Hour = GetCurrentHour();

	// Zeitplaene gestaffelt (jede Figur etwa einmal je Sekunde) plus erzwungene
	if (Agents.Num() > 0)
	{
		const int32 PerStep = Agents.Num() / 4 + 1;
		for (int32 K = 0; K < PerStep; ++K)
		{
			EvalCursor = (EvalCursor + 1) % Agents.Num();
			EvaluateAgent(EvalCursor, Hour, false);
		}
		for (int32 I = 0; I < Agents.Num(); ++I)
		{
			if (Agents[I].bForceEval)
			{
				Agents[I].bForceEval = false;
				EvaluateAgent(I, Hour, false);
			}
		}
	}
	for (int32 I = 0; I < Agents.Num(); ++I)
	{
		AdvanceHidden(I, Step, false);
	}
	for (const int32 I : VisibleIndices)
	{
		UpdateIdleBehaviour(I, Now);
	}
	UpdatePlayerConversation();

	LODAccumulator += Step;
	if (LODAccumulator >= 0.5f)
	{
		LODAccumulator = 0.f;
		UpdateLOD();
	}
	BarkAccumulator += Step;
	if (BarkAccumulator >= 0.5f)
	{
		BarkAccumulator = 0.f;
		UpdateBarks(Now);
	}
	SocialAccumulator += Step;
	if (SocialAccumulator >= SocialCheckInterval)
	{
		SocialAccumulator = 0.f;
		UpdateSocial(Now);
	}
}

// ================================================================================================
// Selbsttest
// ================================================================================================

void AMTALivingWorldManager::ResetAllActorsAfterTest()
{
	for (int32 I = 0; I < Agents.Num(); ++I)
	{
		if (Agents[I].Actor.IsValid())
		{
			PushTextToActor(I);
		}
	}
}

FString AMTALivingWorldManager::RunLivingWorldSelfTest()
{
	if (!bWorldReady)
	{
		InitializeWorld();
	}
	FString Report = TEXT("Lebendige Welt - Selbsttest\n");
	int32 Failures = 0;
	auto Check = [&Report, &Failures](const TCHAR* Name, bool bOk, const FString& Detail)
	{
		UE_LOG(LogMTALiving, Display, TEXT("SELFTEST LebendigeWelt %s: %s %s"), Name, bOk ? TEXT("OK") : TEXT("FEHLER"), *Detail);
		Report += FString::Printf(TEXT("[%s] %s %s\n"), bOk ? TEXT("OK") : TEXT("FEHLER"), Name, *Detail);
		if (!bOk) { ++Failures; }
	};

	// 1) Orts-Tags
	{
		int32 Resolved = 0;
		TSet<FIntVector> Unique;
		for (const FName& Tag : LocationOrder)
		{
			const FVector P = ResolveLocation(Tag);
			if (!P.IsZero() && IsInsideGrid(P) && !IsOnRoadLocal(P))
			{
				++Resolved;
				Unique.Add(FIntVector(FMath::RoundToInt32((float)P.X / 50.f), FMath::RoundToInt32((float)P.Y / 50.f), 0));
			}
		}
		Check(TEXT("Orts-Tags aufloesbar"), LocationOrder.Num() > 0 && Resolved == LocationOrder.Num(),
			FString::Printf(TEXT("(%d/%d auf dem Gehweg, %d verschiedene Punkte)"), Resolved, LocationOrder.Num(), Unique.Num()));
		Check(TEXT("Unbekannter Tag liefert Nullvektor"), ResolveLocation(FName(TEXT("gibt.es.nicht"))).IsZero(), FString());
		const FVector Noodle = ResolveLocation(FName(TEXT("core.noodle_vault")));
		Check(TEXT("core.noodle_vault liegt im Core"), !Noodle.IsZero() && (!CityGen || LivingDistrictToId(CityGen->GetDistrictAtLocation(Noodle)) == FName(TEXT("core"))),
			FString::Printf(TEXT("(%.0f, %.0f)"), Noodle.X, Noodle.Y));
	}

	// 2) Jede Schluesselfigur hat zu jeder Testzeit eine Position
	{
		const float Hours[4] = { 3.f, 8.5f, 12.5f, 20.f };
		int32 Keys = 0;
		int32 Missing = 0;
		int32 IndoorsAt3 = 0;
		for (const FMTALivingAgent& A : Agents)
		{
			if (!A.bKey) { continue; }
			++Keys;
			for (const float H : Hours)
			{
				FMTALivingDesired D;
				if (!ComputeDesired(A, H, D) || !D.Spot.bValid || !IsInsideGrid(D.Spot.Pos))
				{
					++Missing;
				}
				else if (H == 3.f && D.bIndoors)
				{
					++IndoorsAt3;
				}
			}
		}
		Check(TEXT("Schluesselfiguren haben Positionen (3, 8:30, 12:30, 20 Uhr)"), Keys > 0 && Missing == 0,
			FString::Printf(TEXT("(%d Figuren, %d fehlend)"), Keys, Missing));
		Check(TEXT("Nachts sind die meisten drinnen (3 Uhr)"), Keys > 0 && IndoorsAt3 * 2 >= Keys,
			FString::Printf(TEXT("(%d/%d)"), IndoorsAt3, Keys));
	}

	// Ab hier wird simuliert: Zustand sichern, Actors bleiben unberuehrt.
	const TArray<FMTALivingAgent> AgentBackup = Agents;
	const TMap<FName, TArray<int32>> SlotBackup = SlotOwners;
	const TArray<FMTALivingEncounter> EncounterBackup = Encounters;
	const TMap<uint64, double> CooldownBackup = PairCooldowns;
	const FString NpcSnapshot = NPCSys ? NPCSys->SaveStateToJson() : FString();
	bSelfTestRunning = true;

	// 3) Bewegung nach simulierter Zeitspanne (6:00 -> 12:30, 30 s Sim-LOD-Reise)
	{
		TestHourOverride = 6.f;
		for (int32 I = 0; I < Agents.Num(); ++I) { EvaluateAgent(I, 6.f, true); }
		TArray<FVector> Start;
		for (const FMTALivingAgent& A : Agents) { Start.Add(A.SimPos); }
		TestHourOverride = 12.5f;
		int32 Started = 0;
		float LongestDiagonal = 0.f;
		for (int32 I = 0; I < Agents.Num(); ++I)
		{
			EvaluateAgent(I, 12.5f, false);
			if (Agents[I].Phase == EMTALivingPhase::Walking)
			{
				++Started;
				FVector Prev = Agents[I].SimPos;
				for (const FMTALivingWaypoint& W : Agents[I].Path)
				{
					const bool bAxis = FMath::Abs(W.Pos.X - Prev.X) < 1.0 || FMath::Abs(W.Pos.Y - Prev.Y) < 1.0;
					if (!bAxis) { LongestDiagonal = FMath::Max(LongestDiagonal, (float)FVector::Dist2D(W.Pos, Prev)); }
					Prev = W.Pos;
				}
			}
		}
		for (int32 Step = 0; Step < 30; ++Step)
		{
			for (int32 I = 0; I < Agents.Num(); ++I) { AdvanceHidden(I, 1.f, true); }
		}
		int32 Moved = 0;
		int32 Arrived = 0;
		for (int32 I = 0; I < Agents.Num(); ++I)
		{
			if (FVector::Dist2D(Agents[I].SimPos, Start[I]) > 100.f) { ++Moved; }
			if (Agents[I].Phase != EMTALivingPhase::Walking && FVector::Dist2D(Agents[I].SimPos, Start[I]) > 100.f) { ++Arrived; }
		}
		Check(TEXT("Figuren bewegen sich nach simulierter Zeit"), Started > 0 && Moved >= FMath::Min(10, Started),
			FString::Printf(TEXT("(6:00->12:30: %d losgelaufen, %d bewegt, %d angekommen)"), Started, Moved, Arrived));
		Check(TEXT("Wege folgen den Gehwegen"), LongestDiagonal < 450.f,
			FString::Printf(TEXT("(laengstes Schraegstueck %.0f cm)"), LongestDiagonal));
	}

	// 4) NPC-NPC-Interaktion (Schluesselfiguren mit Beziehung, danach zwei Strassenfiguren)
	{
		int32 PA = INDEX_NONE;
		int32 PB = INDEX_NONE;
		if (RelationSys)
		{
			for (int32 I = 0; I < Agents.Num() && PA == INDEX_NONE; ++I)
			{
				if (!Agents[I].bKey) { continue; }
				for (const FMTARelationship& R : RelationSys->GetRelationshipsOf(Agents[I].Id))
				{
					const FName Other = R.A == Agents[I].Id ? R.B : R.A;
					if (const int32* J = AgentById.Find(Other))
					{
						PA = I;
						PB = *J;
						break;
					}
				}
			}
		}
		if (PA == INDEX_NONE && Agents.Num() >= 2) { PA = 0; PB = 1; }

		auto RunPair = [this](int32 IA, int32 IB, FString& OutDetail) -> bool
		{
			if (!Agents.IsValidIndex(IA) || !Agents.IsValidIndex(IB))
			{
				OutDetail = TEXT("(keine Figuren)");
				return false;
			}
			for (const int32 Idx : { IA, IB })
			{
				FMTALivingAgent& M = Agents[Idx];
				M.EncounterId = INDEX_NONE;
				M.Phase = EMTALivingPhase::Idle;
				M.NextSocialTime = 0.0;
				M.LastSpeech.Reset();
			}
			Agents[IB].SimPos = Agents[IA].SimPos + FVector(150.f, 0.f, 0.f);
			const bool bStarted = TryStartEncounter(IA, IB, true, nullptr);
			const FMTALivingEncounter* Enc = bStarted ? FindEncounter(Agents[IA].EncounterId) : nullptr;
			const FString TypeName = Enc ? MTANPC::InteractionToString(Enc->Type) : FString(TEXT("-"));
			const int32 NumLines = Enc ? Enc->Lines.Num() : 0;
			const bool bOneSided = Enc && Enc->bOneSided;
			for (int32 Step = 0; Step < 60 && Agents[IA].EncounterId != INDEX_NONE; ++Step)
			{
				UpdateEncounters(0.5f);
			}
			const bool bSpoken = !Agents[IA].LastSpeech.IsEmpty() || !Agents[IB].LastSpeech.IsEmpty();
			const bool bEnded = Agents[IA].EncounterId == INDEX_NONE && Agents[IB].EncounterId == INDEX_NONE;
			OutDetail = FString::Printf(TEXT("(%s & %s: %s, %d Zeilen, \"%s\")"), *Agents[IA].DisplayName, *Agents[IB].DisplayName,
				*TypeName, NumLines, *(Agents[IA].LastSpeech.IsEmpty() ? Agents[IB].LastSpeech : Agents[IA].LastSpeech));
			return bStarted && bEnded && (bSpoken || bOneSided);
		};

		FString Detail;
		const float AffBefore = (RelationSys && PA != INDEX_NONE) ? RelationSys->GetAffinity(Agents[PA].Id, Agents[PB].Id) : 0.f;
		const bool bKeyOk = RunPair(PA, PB, Detail);
		const float AffAfter = (RelationSys && PA != INDEX_NONE) ? RelationSys->GetAffinity(Agents[PA].Id, Agents[PB].Id) : 0.f;
		Check(TEXT("NPC-NPC-Interaktion (Beziehungssystem)"), bKeyOk,
			FString::Printf(TEXT("%s Zuneigung %.1f -> %.1f"), *Detail, AffBefore, AffAfter));

		int32 SA = INDEX_NONE;
		int32 SB = INDEX_NONE;
		for (int32 I = 0; I < Agents.Num(); ++I)
		{
			if (Agents[I].bKey) { continue; }
			if (SA == INDEX_NONE) { SA = I; }
			else { SB = I; break; }
		}
		FString StreetDetail;
		const bool bStreetOk = RunPair(SA, SB, StreetDetail);
		Check(TEXT("Strassenfiguren unterhalten sich"), bStreetOk, StreetDetail);
	}

	// Zustand zurueck
	Agents = AgentBackup;
	SlotOwners = SlotBackup;
	Encounters = EncounterBackup;
	PairCooldowns = CooldownBackup;
	TestHourOverride = -1.f;
	bSelfTestRunning = false;
	if (NPCSys && !NpcSnapshot.IsEmpty())
	{
		NPCSys->LoadStateFromJson(NpcSnapshot);
	}
	ResetAllActorsAfterTest();

	// 5) Barks
	{
		const FString Bark = GetBarkLine(TEXT("greeting"), TEXT("saltmarket"), TEXT("union"), NAME_None);
		Check(TEXT("Barks verfuegbar"), !Bark.IsEmpty(), FString::Printf(TEXT("(\"%s\")"), *Bark));
	}

	// 6) Budget und Bestand
	{
		int32 Moving = 0;
		for (const int32 I : VisibleIndices)
		{
			if (const AMTALivingNPC* Npc = Agents[I].Actor.Get())
			{
				Moving += Npc->GetVelocity().Size2D() > 20.0 ? 1 : 0;
			}
		}
		Check(TEXT("Mindestens 40 Strassenfiguren"), StreetProfiles.Num() >= 40, FString::Printf(TEXT("(%d)"), StreetProfiles.Num()));
		Check(TEXT("Budget sichtbarer Figuren"), VisibleIndices.Num() <= FMath::Min(MaxVisibleNPCs, 80) && SpawnedActors.Num() <= 81,
			FString::Printf(TEXT("(%d sichtbar, %d Actors erzeugt, %d laufen gerade, %d Figuren gesamt)"),
				VisibleIndices.Num(), SpawnedActors.Num(), Moving, Agents.Num()));
	}

	Report += FString::Printf(TEXT("ERGEBNIS: %d Fehler\n"), Failures);
	UE_LOG(LogMTALiving, Display, TEXT("SELFTEST LebendigeWelt ERGEBNIS: %d Fehler"), Failures);
	return Report;
}
