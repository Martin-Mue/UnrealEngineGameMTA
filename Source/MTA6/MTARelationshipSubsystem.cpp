#include "MTARelationshipSubsystem.h"
#include "MTANPCSubsystem.h"
#include "MTAGossipSubsystem.h"
#include "MTADialogueSubsystem.h"
#include "Engine/GameInstance.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTARelationship, Log, All);

namespace
{
	float ActivitySocialFactor(EMTAActivity A)
	{
		switch (A)
		{
		case EMTAActivity::Bar: return 1.6f;
		case EMTAActivity::Strike: return 1.4f;
		case EMTAActivity::Market: return 1.3f;
		case EMTAActivity::Shelter: return 1.3f;
		case EMTAActivity::Eat: return 1.2f;
		case EMTAActivity::Leisure: return 1.2f;
		case EMTAActivity::Worship: return 0.8f;
		case EMTAActivity::Perform: return 0.7f;
		case EMTAActivity::Work: return 0.6f;
		case EMTAActivity::Shift: return 0.6f;
		case EMTAActivity::Patrol: return 0.5f;
		case EMTAActivity::Home: return 0.4f;
		default: return 0.f; // Sleep, Hide, Commute, Talk: keine Interaktion
		}
	}

	bool IsSocialActivity(EMTAActivity A) { return ActivitySocialFactor(A) > 0.f; }
}

void UMTARelationshipSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMTANPCSubsystem>();
	LoadFromNPCData(MTANPC::LoadJsonFile(TEXT("npcs.json")));
}

UMTANPCSubsystem* UMTARelationshipSubsystem::NPCs() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UMTANPCSubsystem>() : nullptr;
}

uint64 UMTARelationshipSubsystem::PairKey(FName A, FName B)
{
	if (B.Compare(A) < 0) { Swap(A, B); }
	return ((uint64)A.GetComparisonIndex().ToUnstableInt() << 32) | (uint64)B.GetComparisonIndex().ToUnstableInt();
}

void UMTARelationshipSubsystem::SetIndexForEdge(int32 Index)
{
	const FMTARelationship& E = Edges[Index];
	EdgeIndex.Add(PairKey(E.A, E.B), Index);
	EdgesByNPC.FindOrAdd(E.A).AddUnique(Index);
	EdgesByNPC.FindOrAdd(E.B).AddUnique(Index);
}

FMTARelationship& UMTARelationshipSubsystem::FindOrAddEdge(FName A, FName B)
{
	if (const int32* I = EdgeIndex.Find(PairKey(A, B))) { return Edges[*I]; }
	FMTARelationship E;
	E.A = B.Compare(A) < 0 ? B : A;
	E.B = B.Compare(A) < 0 ? A : B;
	const int32 Index = Edges.Add(E);
	SetIndexForEdge(Index);
	return Edges[Index];
}

const FMTARelationship* UMTARelationshipSubsystem::FindEdge(FName A, FName B) const
{
	const int32* I = EdgeIndex.Find(PairKey(A, B));
	return I ? &Edges[*I] : nullptr;
}

void UMTARelationshipSubsystem::LoadFromNPCData(const TSharedPtr<FJsonObject>& Root)
{
	Edges.Reset(); EdgeIndex.Reset(); EdgesByNPC.Reset();
	if (!Root.IsValid()) { return; }
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Root->TryGetArrayField(TEXT("relations"), Arr)) { return; }
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject> R = V->AsObject();
		FString SA, SB, SType, Note;
		double Value = 0.0;
		if (!R->TryGetStringField(TEXT("a"), SA) || !R->TryGetStringField(TEXT("b"), SB) || !R->TryGetStringField(TEXT("type"), SType)) { continue; }
		R->TryGetNumberField(TEXT("value"), Value);
		R->TryGetStringField(TEXT("note"), Note);
		EMTARelationKind Kind;
		if (!MTANPC::RelationKindFromString(SType, Kind))
		{
			UE_LOG(LogMTARelationship, Warning, TEXT("Unbekannter Beziehungstyp '%s'"), *SType);
			continue;
		}
		const FName A(*SA), B(*SB);
		FMTARelationship& E = FindOrAddEdge(A, B);
		const float Val = (float)Value;
		switch (Kind)
		{
		case EMTARelationKind::Friendship: E.Friendship = FMath::Clamp(E.Friendship + Val, -100.f, 100.f); break;
		case EMTARelationKind::Rivalry: E.Rivalry = FMath::Clamp(E.Rivalry + Val, 0.f, 100.f); E.Friendship -= Val * 0.3f; break;
		case EMTARelationKind::Love: E.Love = FMath::Clamp(Val, 0.f, 100.f); E.Friendship += Val * 0.5f; break;
		case EMTARelationKind::Kin: E.bKin = true; E.Friendship += Val * 0.5f; break;
		case EMTARelationKind::Debt: E.Debt += (E.A == A) ? Val : -Val; E.Rivalry = FMath::Clamp(E.Rivalry + FMath::Min(FMath::Abs(Val) / 100.f, 15.f), 0.f, 100.f); break;
		}
		E.Friendship = FMath::Clamp(E.Friendship, -100.f, 100.f);
		if (!Note.IsEmpty()) { E.Note = E.Note.IsEmpty() ? Note : E.Note + TEXT(" ") + Note; }
	}
	MarkDriftBaseline();
	LoadedEdgeCount = Edges.Num();
	UE_LOG(LogMTARelationship, Log, TEXT("Beziehungsgraph: %d Kanten"), Edges.Num());
}

bool UMTARelationshipSubsystem::GetRelationship(FName A, FName B, FMTARelationship& Out) const
{
	if (const FMTARelationship* E = FindEdge(A, B)) { Out = *E; return true; }
	return false;
}

TArray<FMTARelationship> UMTARelationshipSubsystem::GetRelationshipsOf(FName NPCId) const
{
	TArray<FMTARelationship> Out;
	if (const TArray<int32>* List = EdgesByNPC.Find(NPCId))
	{
		for (int32 I : *List) { Out.Add(Edges[I]); }
	}
	return Out;
}

void UMTARelationshipSubsystem::ModifyRelation(FName A, FName B, EMTARelationKind Kind, float Delta)
{
	if (A.IsNone() || B.IsNone() || A == B) { return; }
	FMTARelationship& E = FindOrAddEdge(A, B);
	switch (Kind)
	{
	case EMTARelationKind::Friendship: E.Friendship = FMath::Clamp(E.Friendship + Delta, -100.f, 100.f); break;
	case EMTARelationKind::Rivalry: E.Rivalry = FMath::Clamp(E.Rivalry + Delta, 0.f, 100.f); break;
	case EMTARelationKind::Love: E.Love = FMath::Clamp(E.Love + Delta, 0.f, 100.f); break;
	case EMTARelationKind::Debt: E.Debt += (E.A == A) ? Delta : -Delta; break;
	case EMTARelationKind::Kin: E.bKin = Delta > 0.f; break;
	}
}

float UMTARelationshipSubsystem::GetAffinity(FName A, FName B) const
{
	const FMTARelationship* E = FindEdge(A, B);
	return E ? FMath::Clamp(E->Friendship + E->Love * 0.5f - E->Rivalry, -100.f, 100.f) : 0.f;
}

float UMTARelationshipSubsystem::GetPlayerTrust(FName NPCId) const
{
	const float* T = PlayerTrust.Find(NPCId);
	return T ? *T : 0.f;
}

void UMTARelationshipSubsystem::ModifyPlayerTrust(FName NPCId, float Delta)
{
	float& T = PlayerTrust.FindOrAdd(NPCId);
	T = FMath::Clamp(T + Delta, -3.f, 3.f);
	if (UMTANPCSubsystem* N = NPCs()) { N->AddMood(NPCId, Delta * 0.1f); }
}

float UMTARelationshipSubsystem::GetPlayerAttitude(FName NPCId) const
{
	float Att = GetPlayerTrust(NPCId) / 3.f * 0.6f;
	const UMTANPCSubsystem* N = NPCs();
	const FMTANPCProfile* P = N ? N->FindProfile(NPCId) : nullptr;
	if (P)
	{
		if (const UGameInstance* GI = GetGameInstance())
		{
			if (const UMTAGossipSubsystem* G = GI->GetSubsystem<UMTAGossipSubsystem>())
			{
				const FMTAGossipDistrictEffect Eff = G->GetDistrictEffect(P->District);
				Att += Eff.Attitude * 0.4f;
				if (const float* F = Eff.FactionShift.Find(P->Faction)) { Att += *F * 0.3f; }
			}
		}
	}
	return FMath::Clamp(Att, -1.f, 1.f);
}

// ------------------------------------------------------------------------------------------------
// Simulation
// ------------------------------------------------------------------------------------------------

bool UMTARelationshipSubsystem::ChooseInteraction(FName A, FName B, const TArray<FName>& Present, FRandomStream& Rng, FMTANPCInteractionEvent& Out) const
{
	const UMTANPCSubsystem* N = NPCs();
	if (!N) { return false; }
	if (Rng.FRand() < 0.5f) { Swap(A, B); } // Rollen (Initiator/Partner) zufaellig verteilen
	const FMTANPCProfile* PA = N->FindProfile(A);
	const FMTANPCProfile* PB = N->FindProfile(B);
	const FMTANPCRuntime* RA = N->FindRuntime(A);
	if (!PA || !PB || !RA) { return false; }

	const FMTARelationship* E = FindEdge(A, B);
	const float F = E ? E->Friendship / 100.f : 0.f;
	const float R = E ? E->Rivalry / 100.f : 0.f;
	const float L = E ? E->Love / 100.f : 0.f;
	const bool bDebt = E && FMath::Abs(E->Debt) > 1.f;
	const EMTAActivity Act = RA->Current.Activity;
	const float Hour = N->GetHour();
	const bool bEvening = Hour >= 18.f || Hour < 2.f;
	const bool bLateBar = Act == EMTAActivity::Bar && (Hour >= 22.f || Hour < 3.f);
	const FMTAPersonality& XA = PA->Personality;
	const FMTAPersonality& XB = PB->Personality;
	const float MoodNeg = FMath::Max(0.f, -RA->Mood);

	float W[(int32)EMTAInteractionType::Count] = {};
	W[(int32)EMTAInteractionType::Chat] = (XA.Talk + XB.Talk) * 0.5f * (1.f + FMath::Max(F, 0.f) + L) * (bEvening || Act == EMTAActivity::Bar ? 1.4f : 1.f) * (1.f - R * 0.7f);
	W[(int32)EMTAInteractionType::Trade] = 0.6f * ((!PA->ShopId.IsNone() || !PB->ShopId.IsNone()) ? 1.5f : 0.3f) * (XA.Greed + XB.Greed) * 0.5f
		* ((Act == EMTAActivity::Market || Act == EMTAActivity::Work) ? 1.5f : 0.6f);
	W[(int32)EMTAInteractionType::Argue] = 0.5f * (XA.Temper + XB.Temper) * 0.5f * (R * 2.f + FMath::Max(-F, 0.f) + (bDebt ? 0.5f : 0.f) + 0.05f)
		* (bLateBar ? 1.6f : 1.f) * (1.f + MoodNeg);
	W[(int32)EMTAInteractionType::Cooperate] = 0.4f * (PA->Faction == PB->Faction && PA->Faction != TEXT("none") ? 1.5f : 0.4f) * (1.f + FMath::Max(F, 0.f))
		* ((Act == EMTAActivity::Work || Act == EMTAActivity::Shift || Act == EMTAActivity::Strike || Act == EMTAActivity::Patrol) ? 1.5f : 0.5f);
	W[(int32)EMTAInteractionType::Shadow] = PB->Secrets.Num() > 0
		? 0.15f * XA.Suspicion * XA.Curiosity * (R + 0.2f) * (PA->Faction == TEXT("kcg") ? 1.5f : 1.f) : 0.f;
	W[(int32)EMTAInteractionType::Betray] = (PB->Secrets.Num() > 0 && PA->Faction != PB->Faction)
		? 0.04f * XA.Greed * (1.f - XA.Loyalty) * (R + FMath::Max(-F, 0.f) + 0.1f) : 0.f;

	// Vermitteln: A ist mit B nicht verfeindet und ein Dritter C mit Streit gegen B ist anwesend.
	FName Third = NAME_None;
	if (F >= 0.f && R < 0.3f)
	{
		for (const FName& C : Present)
		{
			if (C == A || C == B) { continue; }
			const FMTARelationship* EBC = FindEdge(B, C);
			if (EBC && EBC->Rivalry > 30.f && GetAffinity(A, C) >= 0.f) { Third = C; break; }
		}
		if (!Third.IsNone())
		{
			W[(int32)EMTAInteractionType::Mediate] = 0.6f * XA.Loyalty * (1.f - XA.Temper) + 0.1f;
		}
	}

	float Total = 0.f;
	for (float X : W) { Total += FMath::Max(0.f, X); }
	if (Total <= KINDA_SMALL_NUMBER) { return false; }
	float Pick = Rng.FRand() * Total;
	int32 Chosen = 0;
	for (int32 I = 0; I < (int32)EMTAInteractionType::Count; ++I)
	{
		Pick -= FMath::Max(0.f, W[I]);
		if (Pick <= 0.f) { Chosen = I; break; }
	}
	Out = FMTANPCInteractionEvent();
	Out.A = A;
	Out.B = B;
	Out.Type = (EMTAInteractionType)Chosen;
	Out.Third = (Out.Type == EMTAInteractionType::Mediate) ? Third : NAME_None;
	Out.Location = RA->Current.Location;
	Out.GameHour = N->GetTotalHours();
	return true;
}

void UMTARelationshipSubsystem::ApplyInteraction(const FMTANPCInteractionEvent& Ev, FRandomStream& Rng)
{
	UMTANPCSubsystem* N = NPCs();
	if (!N) { return; }
	const FMTANPCProfile* PA = N->FindProfile(Ev.A);
	const FMTANPCProfile* PB = N->FindProfile(Ev.B);
	if (!PA || !PB) { return; }
	const UGameInstance* GI = GetGameInstance();
	UMTAGossipSubsystem* Gossip = GI ? GI->GetSubsystem<UMTAGossipSubsystem>() : nullptr;
	const FName District = N->GetLocationDistrict(Ev.Location).IsNone() ? PA->District : N->GetLocationDistrict(Ev.Location);
	FMTARelationship& E = FindOrAddEdge(Ev.A, Ev.B);

	switch (Ev.Type)
	{
	case EMTAInteractionType::Chat:
		E.Friendship = FMath::Clamp(E.Friendship + 2.f + (PA->Personality.Humor + PB->Personality.Humor), -100.f, 100.f);
		E.Rivalry = FMath::Max(0.f, E.Rivalry - 0.5f);
		if (E.Love > 0.f) { E.Love = FMath::Min(100.f, E.Love + 0.5f); }
		N->AddMood(Ev.A, 0.05f); N->AddMood(Ev.B, 0.05f);
		break;
	case EMTAInteractionType::Trade:
		E.Friendship = FMath::Clamp(E.Friendship + 1.f, -100.f, 100.f);
		if (Rng.FRand() < 0.3f)
		{
			// Anschreiben lassen: der Kaeufer (A) schuldet dem Haendler etwas.
			ModifyRelation(Ev.A, Ev.B, EMTARelationKind::Debt, 5.f + 25.f * PA->Personality.Greed);
		}
		break;
	case EMTAInteractionType::Argue:
		E.Rivalry = FMath::Clamp(E.Rivalry + 3.f + 2.f * (PA->Personality.Temper + PB->Personality.Temper), 0.f, 100.f);
		E.Friendship = FMath::Clamp(E.Friendship - 3.f, -100.f, 100.f);
		N->AddMood(Ev.A, -0.1f); N->AddMood(Ev.B, -0.1f);
		if (FMath::Abs(E.Debt) > 1.f && Rng.FRand() < 0.2f) { E.Debt *= 0.7f; } // Streit erzwingt Teilzahlung
		if (Gossip && Rng.FRand() < 0.25f)
		{
			Gossip->CreateRumor(TEXT("npc_feud"), Ev.A, Ev.B, District, Ev.Location, 0.3f);
		}
		break;
	case EMTAInteractionType::Mediate:
		if (!Ev.Third.IsNone())
		{
			FMTARelationship& EBC = FindOrAddEdge(Ev.B, Ev.Third);
			EBC.Rivalry = FMath::Max(0.f, EBC.Rivalry - 8.f);
			EBC.Friendship = FMath::Clamp(EBC.Friendship + 3.f, -100.f, 100.f);
			ModifyRelation(Ev.A, Ev.Third, EMTARelationKind::Friendship, 2.f);
		}
		E.Friendship = FMath::Clamp(E.Friendship + 2.f, -100.f, 100.f);
		break;
	case EMTAInteractionType::Cooperate:
		E.Friendship = FMath::Clamp(E.Friendship + 3.f, -100.f, 100.f);
		E.Rivalry = FMath::Max(0.f, E.Rivalry - 1.f);
		if (FMath::Abs(E.Debt) > 1.f) { E.Debt = FMath::Sign(E.Debt) * FMath::Max(0.f, FMath::Abs(E.Debt) - 10.f); }
		break;
	case EMTAInteractionType::Shadow:
		if (Gossip && PB->Secrets.Num() > 0 && Rng.FRand() < PA->Personality.Curiosity)
		{
			const FMTANPCSecret& S = PB->Secrets[Rng.RandRange(0, PB->Secrets.Num() - 1)];
			Gossip->CreateRumor(TEXT("npc_secret"), Ev.B, Ev.A, District, Ev.Location, S.Severity * 0.7f, S.Id);
		}
		if (Rng.FRand() < PB->Personality.Suspicion * 0.5f)
		{
			// Ertappt.
			E.Rivalry = FMath::Clamp(E.Rivalry + 10.f, 0.f, 100.f);
			E.Friendship = FMath::Clamp(E.Friendship - 5.f, -100.f, 100.f);
		}
		break;
	case EMTAInteractionType::Betray:
		E.Friendship = FMath::Clamp(E.Friendship - 25.f, -100.f, 100.f);
		E.Rivalry = FMath::Clamp(E.Rivalry + 20.f, 0.f, 100.f);
		E.bBetrayed = true;
		if (Gossip && PB->Secrets.Num() > 0)
		{
			const FMTANPCSecret& S = PB->Secrets[Rng.RandRange(0, PB->Secrets.Num() - 1)];
			Gossip->CreateRumor(TEXT("npc_secret"), Ev.B, Ev.A, District, Ev.Location, S.Severity, S.Id);
			if (Rng.FRand() < 0.5f)
			{
				Gossip->CreateRumor(TEXT("betrayal"), Ev.A, Ev.B, District, Ev.Location, 0.6f);
			}
		}
		break;
	default:
		break;
	}
}

void UMTARelationshipSubsystem::SimulateHour(FRandomStream& Rng, TArray<FMTANPCInteractionEvent>& OutEvents)
{
	UMTANPCSubsystem* N = NPCs();
	if (!N) { return; }

	// Vergessen: Rivalitaet sinkt langsam.
	for (FMTARelationship& E : Edges)
	{
		if (!E.bBetrayed) { E.Rivalry = FMath::Max(0.f, E.Rivalry - RivalryDecayPerHour); }
	}

	// Anwesende je Ort sammeln.
	TMap<FName, TArray<FName>> Groups;
	const double Now = N->GetTotalHours();
	for (const FName& Id : N->GetAllNPCIds())
	{
		const FMTANPCRuntime* R = N->FindRuntime(Id);
		if (!R || !R->bActive || R->BusyUntil > Now || !IsSocialActivity(R->Current.Activity) || R->Current.Location.IsNone()) { continue; }
		Groups.FindOrAdd(R->Current.Location).Add(Id);
	}

	const UGameInstance* GI = GetGameInstance();
	UMTADialogueSubsystem* Dlg = GI ? GI->GetSubsystem<UMTADialogueSubsystem>() : nullptr;

	for (TPair<FName, TArray<FName>>& G : Groups)
	{
		TArray<FName>& Present = G.Value;
		if (Present.Num() < 2) { continue; }
		const FMTANPCRuntime* First = N->FindRuntime(Present[0]);
		const float Social = ActivitySocialFactor(First->Current.Activity);
		bool bDetailed = false;
		for (const FName& Id : Present)
		{
			if (N->FindRuntime(Id)->LOD != EMTASimLOD::Far) { bDetailed = true; break; }
		}

		if (bDetailed)
		{
			// Sichtbar: jedes Paar einzeln, jede Figur hoechstens einmal pro Stunde.
			TSet<FName> Used;
			for (int32 I = 0; I < Present.Num(); ++I)
			{
				for (int32 J = I + 1; J < Present.Num(); ++J)
				{
					if (Used.Contains(Present[I]) || Used.Contains(Present[J])) { continue; }
					const FMTANPCProfile* ProfI = N->FindProfile(Present[I]);
					const FMTANPCProfile* ProfJ = N->FindProfile(Present[J]);
					const float P = PairInteractionRate * Social * (0.5f + (ProfI->Personality.Talk + ProfJ->Personality.Talk) * 0.5f)
						* (FindEdge(Present[I], Present[J]) ? 1.5f : 1.f);
					if (Rng.FRand() >= P) { continue; }
					FMTANPCInteractionEvent Ev;
					if (!ChooseInteraction(Present[I], Present[J], Present, Rng, Ev)) { continue; }
					Ev.bVisible = true;
					if (Dlg)
					{
						FMTABarkContext Ctx;
						Ctx.Category = Ev.Type == EMTAInteractionType::Argue ? FName(TEXT("argue"))
							: (Ev.Type == EMTAInteractionType::Mediate ? FName(TEXT("mediate")) : FName(TEXT("ambient_chat")));
						Ctx.District = N->GetLocationDistrict(Ev.Location);
						Ctx.bIgnoreCooldown = true;
						Ev.Line = Dlg->GetBark(Ctx);
					}
					ApplyInteraction(Ev, Rng);
					Used.Add(Ev.A); Used.Add(Ev.B);
					N->SetBusy(Ev.A, 0.25f); N->SetBusy(Ev.B, 0.25f);
					OutEvents.Add(Ev);
				}
			}
		}
		else
		{
			// Statistisch: erwartete Anzahl je Ort, Paare zufaellig gezogen, keine Delegates.
			const int32 Pairs = Present.Num() * (Present.Num() - 1) / 2;
			const float Expected = (float)Pairs * PairInteractionRate * Social * 0.8f;
			int32 Count = FMath::FloorToInt32(Expected);
			if (Rng.FRand() < Expected - (float)Count) { ++Count; }
			Count = FMath::Min(Count, Present.Num());
			for (int32 K = 0; K < Count; ++K)
			{
				const int32 I = Rng.RandRange(0, Present.Num() - 1);
				int32 J = Rng.RandRange(0, Present.Num() - 2);
				if (J >= I) { ++J; }
				FMTANPCInteractionEvent Ev;
				if (!ChooseInteraction(Present[I], Present[J], Present, Rng, Ev)) { continue; }
				Ev.bVisible = false;
				ApplyInteraction(Ev, Rng);
				OutEvents.Add(Ev);
			}
		}
	}
}

void UMTARelationshipSubsystem::MarkDriftBaseline()
{
	for (FMTARelationship& E : Edges)
	{
		E.InitialFriendship = E.Friendship;
		E.InitialRivalry = E.Rivalry;
	}
	LoadedEdgeCount = Edges.Num();
}

void UMTARelationshipSubsystem::ComputeDrift(float& OutFriendship, float& OutRivalry, int32& OutNewEdges) const
{
	OutFriendship = 0.f; OutRivalry = 0.f;
	OutNewEdges = FMath::Max(0, Edges.Num() - LoadedEdgeCount);
	if (Edges.Num() == 0) { return; }
	for (const FMTARelationship& E : Edges)
	{
		OutFriendship += FMath::Abs(E.Friendship - E.InitialFriendship);
		OutRivalry += FMath::Abs(E.Rivalry - E.InitialRivalry);
	}
	OutFriendship /= Edges.Num();
	OutRivalry /= Edges.Num();
}

// ------------------------------------------------------------------------------------------------
// Persistenz
// ------------------------------------------------------------------------------------------------

FString UMTARelationshipSubsystem::ToJson() const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FMTARelationship& E : Edges)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("a"), E.A.ToString());
		O->SetStringField(TEXT("b"), E.B.ToString());
		O->SetNumberField(TEXT("f"), E.Friendship);
		O->SetNumberField(TEXT("r"), E.Rivalry);
		O->SetNumberField(TEXT("l"), E.Love);
		O->SetNumberField(TEXT("d"), E.Debt);
		O->SetBoolField(TEXT("kin"), E.bKin);
		O->SetBoolField(TEXT("betrayed"), E.bBetrayed);
		Arr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("edges"), Arr);
	TSharedPtr<FJsonObject> T = MakeShared<FJsonObject>();
	for (const TPair<FName, float>& P : PlayerTrust) { T->SetNumberField(P.Key.ToString(), P.Value); }
	Root->SetObjectField(TEXT("playerTrust"), T);
	return MTANPC::JsonToString(Root);
}

bool UMTARelationshipSubsystem::FromJson(const FString& Json)
{
	const TSharedPtr<FJsonObject> Root = MTANPC::JsonFromString(Json);
	if (!Root.IsValid()) { return false; }
	// Notizen aus den Startdaten erhalten.
	TMap<uint64, FString> Notes;
	for (const FMTARelationship& E : Edges) { if (!E.Note.IsEmpty()) { Notes.Add(PairKey(E.A, E.B), E.Note); } }
	Edges.Reset(); EdgeIndex.Reset(); EdgesByNPC.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Root->TryGetArrayField(TEXT("edges"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FString SA, SB;
			O->TryGetStringField(TEXT("a"), SA);
			O->TryGetStringField(TEXT("b"), SB);
			FMTARelationship& E = FindOrAddEdge(FName(*SA), FName(*SB));
			double D = 0.0;
			if (O->TryGetNumberField(TEXT("f"), D)) { E.Friendship = (float)D; }
			if (O->TryGetNumberField(TEXT("r"), D)) { E.Rivalry = (float)D; }
			if (O->TryGetNumberField(TEXT("l"), D)) { E.Love = (float)D; }
			if (O->TryGetNumberField(TEXT("d"), D)) { E.Debt = (E.A == FName(*SA)) ? (float)D : -(float)D; }
			O->TryGetBoolField(TEXT("kin"), E.bKin);
			O->TryGetBoolField(TEXT("betrayed"), E.bBetrayed);
			if (const FString* Note = Notes.Find(PairKey(E.A, E.B))) { E.Note = *Note; }
		}
	}
	PlayerTrust.Reset();
	const TSharedPtr<FJsonObject>* T = nullptr;
	if (Root->TryGetObjectField(TEXT("playerTrust"), T))
	{
		for (const auto& P : (*T)->Values) { PlayerTrust.Add(FName(*P.Key), (float)P.Value->AsNumber()); }
	}
	MarkDriftBaseline();
	return true;
}
