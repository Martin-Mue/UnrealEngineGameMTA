#include "MTADialogueSubsystem.h"
#include "MTANPCSubsystem.h"
#include "MTANPCActor.h"
#include "MTARelationshipSubsystem.h"
#include "MTAGossipSubsystem.h"
#include "MTAStorySubsystem.h"
#include "MTAHeatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Dom/JsonValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTADialogue, Log, All);

namespace
{
	const FString NpcRef(TEXT("@npc"));
	const FName PlayerId(TEXT("player"));
}

void UMTADialogueSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMTANPCSubsystem>();
	Collection.InitializeDependency<UMTARelationshipSubsystem>();
	Collection.InitializeDependency<UMTAGossipSubsystem>();
	BarkRng.Initialize(0x4D544136); // "MTA6"
	// Startwerte der Fertigkeiten (Progression-Agent ueberschreibt sie per SetPlayerSkill).
	for (const TCHAR* S : { TEXT("persuade"), TEXT("intimidate"), TEXT("hack"), TEXT("driving") }) { PlayerSkills.Add(FName(S), 2); }
	LoadDialogues();
	LoadBarks();
}

UMTANPCSubsystem* UMTADialogueSubsystem::NPCs() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UMTANPCSubsystem>() : nullptr;
}

UMTARelationshipSubsystem* UMTADialogueSubsystem::Relations() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UMTARelationshipSubsystem>() : nullptr;
}

UMTAGossipSubsystem* UMTADialogueSubsystem::GossipSys() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UMTAGossipSubsystem>() : nullptr;
}

// ------------------------------------------------------------------------------------------------
// Laden
// ------------------------------------------------------------------------------------------------

int32 UMTADialogueSubsystem::LoadDialogues()
{
	Dialogues.Reset();
	DialogueByNPC.Reset();
	const FString Dir = FPaths::ProjectContentDir() / TEXT("Data/dialogues");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *Dir, TEXT("json"));
	Files.Sort();
	for (const FString& File : Files)
	{
		const TSharedPtr<FJsonObject> D = MTANPC::LoadJsonFile(Dir / File);
		if (!D.IsValid()) { continue; }
		FString Id, Npc, Start;
		const TSharedPtr<FJsonObject>* Nodes = nullptr;
		if (!D->TryGetStringField(TEXT("id"), Id) || !D->TryGetStringField(TEXT("start"), Start) || !D->TryGetObjectField(TEXT("nodes"), Nodes))
		{
			UE_LOG(LogMTADialogue, Error, TEXT("Dialog %s: id/start/nodes fehlen"), *File);
			continue;
		}
		if (!(*Nodes)->HasField(Start))
		{
			UE_LOG(LogMTADialogue, Error, TEXT("Dialog %s: Startknoten '%s' fehlt"), *File, *Start);
			continue;
		}
		Dialogues.Add(FName(*Id), D);
		if (D->TryGetStringField(TEXT("npc"), Npc) && !DialogueByNPC.Contains(FName(*Npc)))
		{
			DialogueByNPC.Add(FName(*Npc), FName(*Id));
		}
	}
	UE_LOG(LogMTADialogue, Log, TEXT("Dialoge geladen: %d"), Dialogues.Num());
	return Dialogues.Num();
}

bool UMTADialogueSubsystem::LoadBarks(const FString& Path)
{
	const TSharedPtr<FJsonObject> Root = MTANPC::LoadJsonFile(Path.IsEmpty() ? FString(TEXT("barks.json")) : Path);
	if (!Root.IsValid()) { return false; }
	BarkGroups.Reset();
	BarkCooldowns.Reset();
	const TSharedPtr<FJsonObject>* Cd = nullptr;
	if (Root->TryGetObjectField(TEXT("cooldowns"), Cd))
	{
		for (const auto& P : (*Cd)->Values) { BarkCooldowns.Add(FName(*P.Key), (float)P.Value->AsNumber()); }
	}
	const TArray<TSharedPtr<FJsonValue>>* Groups = nullptr;
	if (!Root->TryGetArrayField(TEXT("groups"), Groups)) { return false; }
	auto Names = [](const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		TArray<FName> Out;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O.IsValid() && O->TryGetArrayField(Key, A)) { for (const TSharedPtr<FJsonValue>& V : *A) { Out.Add(FName(*V->AsString())); } }
		return Out;
	};
	for (const TSharedPtr<FJsonValue>& V : *Groups)
	{
		const TSharedPtr<FJsonObject> G = V->AsObject();
		FBarkGroup B;
		FString Cat;
		G->TryGetStringField(TEXT("cat"), Cat);
		B.Category = FName(*Cat);
		const TSharedPtr<FJsonObject>* When = nullptr;
		if (G->TryGetObjectField(TEXT("when"), When))
		{
			B.Districts = Names(*When, TEXT("district"));
			B.Factions = Names(*When, TEXT("faction"));
			B.Roles = Names(*When, TEXT("role"));
			B.WeatherIds = Names(*When, TEXT("weather"));
			const TArray<TSharedPtr<FJsonValue>>* Hours = nullptr;
			if ((*When)->TryGetArrayField(TEXT("hours"), Hours) && Hours->Num() == 2)
			{
				B.HourFrom = (float)(*Hours)[0]->AsNumber();
				B.HourTo = (float)(*Hours)[1]->AsNumber();
			}
			double D = 0.0;
			if ((*When)->TryGetNumberField(TEXT("minWanted"), D)) { B.MinWanted = (int32)D; }
			if ((*When)->TryGetNumberField(TEXT("maxWanted"), D)) { B.MaxWanted = (int32)D; }
			if ((*When)->TryGetNumberField(TEXT("minSpeed"), D)) { B.MinSpeed = (float)D; }
		}
		const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
		if (G->TryGetArrayField(TEXT("lines"), Lines))
		{
			for (const TSharedPtr<FJsonValue>& L : *Lines) { B.Lines.Add(L->AsString()); }
		}
		BarkGroups.Add(MoveTemp(B));
	}
	UE_LOG(LogMTADialogue, Log, TEXT("Zurufe geladen: %d Zeilen in %d Gruppen"), GetBarkLineCount(), BarkGroups.Num());
	return true;
}

int32 UMTADialogueSubsystem::GetBarkLineCount() const
{
	int32 N = 0;
	for (const FBarkGroup& G : BarkGroups) { N += G.Lines.Num(); }
	return N;
}

FName UMTADialogueSubsystem::GetDialogueForNPC(FName NPCId) const
{
	const FName* D = DialogueByNPC.Find(NPCId);
	return D ? *D : NAME_None;
}

TArray<FName> UMTADialogueSubsystem::GetAllDialogueIds() const
{
	TArray<FName> Out;
	Dialogues.GetKeys(Out);
	return Out;
}

// ------------------------------------------------------------------------------------------------
// Story-Schnappschuss (nur lesend)
// ------------------------------------------------------------------------------------------------

void UMTADialogueSubsystem::RefreshStorySnapshot()
{
	StoryStanding.Reset(); StoryTrust.Reset(); StoryFlags.Reset(); StoryTags.Reset(); StoryCompleted.Reset();
	StoryAegis = 50.f;
	const UGameInstance* GI = GetGameInstance();
	const UMTAStorySubsystem* Story = GI ? GI->GetSubsystem<UMTAStorySubsystem>() : nullptr;
	if (!Story) { return; }
	const TSharedPtr<FJsonObject> Root = MTANPC::JsonFromString(Story->StateToJson());
	if (!Root.IsValid()) { return; }
	double A = 50.0;
	if (Root->TryGetNumberField(TEXT("aegis"), A)) { StoryAegis = (float)A; }
	const TSharedPtr<FJsonObject>* O = nullptr;
	if (Root->TryGetObjectField(TEXT("standing"), O)) { for (const auto& P : (*O)->Values) { StoryStanding.Add(FString(*P.Key), (float)P.Value->AsNumber()); } }
	if (Root->TryGetObjectField(TEXT("npcTrust"), O)) { for (const auto& P : (*O)->Values) { StoryTrust.Add(FString(*P.Key), (float)P.Value->AsNumber()); } }
	if (Root->TryGetObjectField(TEXT("flags"), O)) { for (const auto& P : (*O)->Values) { StoryFlags.Add(FString(*P.Key), P.Value->AsString()); } }
	if (Root->TryGetObjectField(TEXT("tags"), O))
	{
		for (const auto& P : (*O)->Values)
		{
			TArray<FString>& T = StoryTags.Add(FString(*P.Key));
			for (const TSharedPtr<FJsonValue>& V : P.Value->AsArray()) { T.Add(V->AsString()); }
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Done = nullptr;
	if (Root->TryGetArrayField(TEXT("completed"), Done)) { for (const TSharedPtr<FJsonValue>& V : *Done) { StoryCompleted.Add(V->AsString()); } }
}

// ------------------------------------------------------------------------------------------------
// Bedingungen
// ------------------------------------------------------------------------------------------------

bool UMTADialogueSubsystem::CmpNum(const FString& Op, double A, double B)
{
	if (Op == TEXT(">=")) return A >= B;
	if (Op == TEXT(">")) return A > B;
	if (Op == TEXT("<=")) return A <= B;
	if (Op == TEXT("<")) return A < B;
	if (Op == TEXT("==")) return FMath::IsNearlyEqual(A, B);
	if (Op == TEXT("!=")) return !FMath::IsNearlyEqual(A, B);
	return false;
}

FString UMTADialogueSubsystem::ValueToString(const TSharedPtr<FJsonValue>& V)
{
	if (!V.IsValid()) return TEXT("");
	switch (V->Type)
	{
	case EJson::Boolean: return V->AsBool() ? TEXT("true") : TEXT("false");
	case EJson::Number: return FString::SanitizeFloat(V->AsNumber());
	default: return V->AsString();
	}
}

bool UMTADialogueSubsystem::IsLocalFlag(const FString& Name)
{
	return Name.StartsWith(TEXT("dlg.")) || Name.StartsWith(TEXT("npc."));
}

FName UMTADialogueSubsystem::ResolveNPCRef(const FString& Ref) const
{
	return Ref == NpcRef ? ActiveNPC : FName(*Ref);
}

FString UMTADialogueSubsystem::GetFlagValue(const FString& Name) const
{
	if (const FString* L = LocalFlags.Find(Name)) { return *L; }
	if (const FString* S = StoryFlags.Find(Name)) { return *S; }
	return TEXT("false");
}

float UMTADialogueSubsystem::GetTrustValue(FName NPCId) const
{
	const UMTANPCSubsystem* N = NPCs();
	const FMTANPCProfile* P = N ? N->FindProfile(NPCId) : nullptr;
	// Story-Figuren: Story-Wert (Schnappschuss + lokale Ueberlagerung); sonst Beziehungs-Subsystem.
	if (P && !P->StoryKey.IsEmpty())
	{
		if (const float* T = StoryTrust.Find(P->StoryKey)) { return *T; }
	}
	const UMTARelationshipSubsystem* R = Relations();
	return R ? R->GetPlayerTrust(NPCId) : 0.f;
}

bool UMTADialogueSubsystem::CheckAll(const TArray<TSharedPtr<FJsonValue>>* Conds) const
{
	if (!Conds) { return true; }
	for (const TSharedPtr<FJsonValue>& V : *Conds)
	{
		if (!Check(V->AsObject())) { return false; }
	}
	return true;
}

bool UMTADialogueSubsystem::Check(const TSharedPtr<FJsonObject>& C) const
{
	if (!C.IsValid()) { return false; }
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	FString S;

	// --- Schema der Story-Engine ---
	if (C->TryGetArrayField(TEXT("standing"), A) && A->Num() == 3)
	{
		const float* V = StoryStanding.Find((*A)[0]->AsString());
		return CmpNum((*A)[1]->AsString(), V ? *V : 0.f, (*A)[2]->AsNumber());
	}
	if (C->TryGetArrayField(TEXT("npcTrust"), A) && A->Num() == 3)
	{
		return CmpNum((*A)[1]->AsString(), GetTrustValue(ResolveNPCRef((*A)[0]->AsString())), (*A)[2]->AsNumber());
	}
	if (C->TryGetArrayField(TEXT("flag"), A) && A->Num() == 3)
	{
		const FString Cur = GetFlagValue((*A)[0]->AsString());
		const FString Want = ValueToString((*A)[2]);
		return (*A)[1]->AsString() == TEXT("!=") ? Cur != Want : Cur == Want;
	}
	if (C->TryGetArrayField(TEXT("aegis"), A) && A->Num() == 2)
	{
		return CmpNum((*A)[0]->AsString(), StoryAegis, (*A)[1]->AsNumber());
	}
	if (C->TryGetStringField(TEXT("done"), S))
	{
		return StoryCompleted.Contains(S);
	}
	if (C->TryGetArrayField(TEXT("tag"), A) && A->Num() == 3)
	{
		const TArray<FString>* T = StoryTags.Find((*A)[0]->AsString());
		const bool bHas = T && T->Contains((*A)[2]->AsString());
		return (*A)[1]->AsString() == TEXT("not") ? !bHas : bHas;
	}
	if (C->TryGetArrayField(TEXT("any"), A))
	{
		for (const TSharedPtr<FJsonValue>& Sub : *A) { if (Check(Sub->AsObject())) return true; }
		return false;
	}
	if (C->TryGetArrayField(TEXT("all"), A))
	{
		for (const TSharedPtr<FJsonValue>& Sub : *A) { if (!Check(Sub->AsObject())) return false; }
		return true;
	}
	const TSharedPtr<FJsonObject>* Not = nullptr;
	if (C->TryGetObjectField(TEXT("not"), Not))
	{
		return !Check(*Not);
	}

	// --- Erweiterungen des NPC-Systems ---
	if (C->TryGetArrayField(TEXT("skill"), A) && A->Num() == 3)
	{
		return CmpNum((*A)[1]->AsString(), GetPlayerSkill(FName(*(*A)[0]->AsString())), (*A)[2]->AsNumber());
	}
	const UMTANPCSubsystem* N = NPCs();
	if (C->TryGetArrayField(TEXT("hour"), A) && A->Num() == 2)
	{
		return CmpNum((*A)[0]->AsString(), N ? N->GetHour() : 12.f, (*A)[1]->AsNumber());
	}
	if (C->TryGetStringField(TEXT("weather"), S))
	{
		return N && N->GetWeather() == FName(*S);
	}
	if (C->TryGetStringField(TEXT("district"), S))
	{
		return N && N->GetPlayerDistrict() == FName(*S);
	}
	if (C->TryGetArrayField(TEXT("rumor"), A) && A->Num() == 2)
	{
		const UMTAGossipSubsystem* G = GossipSys();
		return G && G->DoesNPCKnow(ActiveNPC, ResolveNPCRef((*A)[0]->AsString()), FName(*(*A)[1]->AsString()));
	}
	if (C->TryGetArrayField(TEXT("relation"), A) && A->Num() == 5)
	{
		const UMTARelationshipSubsystem* R = Relations();
		FMTARelationship E;
		float V = 0.f;
		if (R && R->GetRelationship(ResolveNPCRef((*A)[0]->AsString()), ResolveNPCRef((*A)[1]->AsString()), E))
		{
			const FString Kind = (*A)[2]->AsString();
			V = Kind == TEXT("rivalry") ? E.Rivalry : Kind == TEXT("love") ? E.Love : Kind == TEXT("debt") ? E.Debt : E.Friendship;
		}
		return CmpNum((*A)[3]->AsString(), V, (*A)[4]->AsNumber());
	}
	UE_LOG(LogMTADialogue, Warning, TEXT("Unbekannte Bedingung in %s"), *ActiveDialogue.ToString());
	return false;
}

bool UMTADialogueSubsystem::EvaluateConditionJson(const FString& CondJson, FName NPCId)
{
	const FName Saved = ActiveNPC;
	ActiveNPC = NPCId;
	RefreshStorySnapshot();
	const bool bResult = Check(MTANPC::JsonFromString(CondJson));
	ActiveNPC = Saved;
	return bResult;
}

// ------------------------------------------------------------------------------------------------
// Effekte
// ------------------------------------------------------------------------------------------------

void UMTADialogueSubsystem::RequestStoryEffect(const TSharedPtr<FJsonObject>& Effect)
{
	OnStoryEffectRequested.Broadcast(MTANPC::JsonToString(Effect));
}

void UMTADialogueSubsystem::ApplyAll(const TArray<TSharedPtr<FJsonValue>>* Effects)
{
	if (!Effects) { return; }
	for (const TSharedPtr<FJsonValue>& V : *Effects) { Apply(V->AsObject()); }
}

void UMTADialogueSubsystem::Apply(const TSharedPtr<FJsonObject>& E)
{
	if (!E.IsValid()) { return; }
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	UMTANPCSubsystem* N = NPCs();
	UMTARelationshipSubsystem* Rel = Relations();
	double Num = 0.0;

	if (E->TryGetArrayField(TEXT("standing"), A) && A->Num() == 2)
	{
		float& V = StoryStanding.FindOrAdd((*A)[0]->AsString());
		V = FMath::Clamp(V + (float)(*A)[1]->AsNumber(), -100.f, 100.f);
		RequestStoryEffect(E);
	}
	if (E->TryGetArrayField(TEXT("npcTrust"), A) && A->Num() == 2)
	{
		const FName Target = ResolveNPCRef((*A)[0]->AsString());
		const float Delta = (float)(*A)[1]->AsNumber();
		if (Rel) { Rel->ModifyPlayerTrust(Target, Delta); }
		const FMTANPCProfile* P = N ? N->FindProfile(Target) : nullptr;
		if (P && !P->StoryKey.IsEmpty())
		{
			// Story-Figur: Wert gehoert der Story-Engine -> Anfrage im Story-Schema, lokal ueberlagern.
			float& V = StoryTrust.FindOrAdd(P->StoryKey);
			V = FMath::Clamp(V + Delta, -3.f, 3.f);
			TSharedPtr<FJsonObject> Req = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Arr;
			Arr.Add(MakeShared<FJsonValueString>(P->StoryKey));
			Arr.Add(MakeShared<FJsonValueNumber>(Delta));
			Req->SetArrayField(TEXT("npcTrust"), Arr);
			RequestStoryEffect(Req);
		}
	}
	if (E->TryGetArrayField(TEXT("flag"), A) && A->Num() == 2)
	{
		const FString Name = (*A)[0]->AsString();
		const FString Value = ValueToString((*A)[1]);
		if (IsLocalFlag(Name)) { LocalFlags.Add(Name, Value); }
		else { StoryFlags.Add(Name, Value); RequestStoryEffect(E); }
	}
	if (E->TryGetArrayField(TEXT("tag"), A) && A->Num() == 3)
	{
		TArray<FString>& T = StoryTags.FindOrAdd((*A)[0]->AsString());
		if ((*A)[1]->AsString() == TEXT("add")) { T.AddUnique((*A)[2]->AsString()); } else { T.Remove((*A)[2]->AsString()); }
		RequestStoryEffect(E);
	}
	if (E->TryGetNumberField(TEXT("aegis"), Num))
	{
		StoryAegis = FMath::Clamp(StoryAegis + (float)Num, 0.f, 100.f);
		RequestStoryEffect(E);
	}
	if (E->HasField(TEXT("money")) || E->HasField(TEXT("unlock")) || E->HasField(TEXT("lock")))
	{
		// Geld und Missionsfreischaltung verwalten Wirtschaft/Story; hier nur melden.
		TSharedPtr<FJsonObject> Req = MakeShared<FJsonObject>();
		for (const TCHAR* Key : { TEXT("money"), TEXT("unlock"), TEXT("lock") })
		{
			if (const TSharedPtr<FJsonValue> V = E->TryGetField(Key)) { Req->SetField(Key, V); }
		}
		RequestStoryEffect(Req);
	}
	if (E->TryGetArrayField(TEXT("heat"), A) && A->Num() == 2)
	{
		const UGameInstance* GI = GetGameInstance();
		UWorld* World = GI ? GI->GetWorld() : nullptr;
		if (UMTAHeatSubsystem* Heat = World ? World->GetSubsystem<UMTAHeatSubsystem>() : nullptr)
		{
			Heat->AddHeat(FName(*(*A)[0]->AsString()), (float)(*A)[1]->AsNumber());
		}
	}
	if (E->TryGetArrayField(TEXT("relation"), A) && A->Num() == 4 && Rel)
	{
		EMTARelationKind Kind;
		if (MTANPC::RelationKindFromString((*A)[2]->AsString(), Kind))
		{
			Rel->ModifyRelation(ResolveNPCRef((*A)[0]->AsString()), ResolveNPCRef((*A)[1]->AsString()), Kind, (float)(*A)[3]->AsNumber());
		}
	}
	const TSharedPtr<FJsonObject>* RumorObj = nullptr;
	if (E->TryGetObjectField(TEXT("rumor"), RumorObj))
	{
		if (UMTAGossipSubsystem* G = GossipSys())
		{
			FString Type, Subject, Secret;
			double Sev = 0.5;
			(*RumorObj)->TryGetStringField(TEXT("type"), Type);
			(*RumorObj)->TryGetStringField(TEXT("subject"), Subject);
			(*RumorObj)->TryGetStringField(TEXT("secret"), Secret);
			(*RumorObj)->TryGetNumberField(TEXT("severity"), Sev);
			const FMTANPCRuntime* RT = N ? N->FindRuntime(ActiveNPC) : nullptr;
			const FName District = RT ? RT->Current.District : NAME_None;
			const FName Location = RT ? RT->Current.Location : NAME_None;
			G->CreateRumor(FName(*Type), ResolveNPCRef(Subject), ActiveNPC, District, Location, (float)Sev, Secret.IsEmpty() ? NAME_None : FName(*Secret));
			// Der Spieler hat das Geheimnis gerade erfahren.
			if (!Secret.IsEmpty()) { LocalFlags.Add(TEXT("dlg.clue.") + Secret, TEXT("true")); }
		}
	}
	if (E->TryGetNumberField(TEXT("mood"), Num) && N)
	{
		N->AddMood(ActiveNPC, (float)Num);
	}
}

// ------------------------------------------------------------------------------------------------
// Gespraechsablauf
// ------------------------------------------------------------------------------------------------

bool UMTADialogueSubsystem::StartConversation(FName NPCId)
{
	UMTANPCSubsystem* N = NPCs();
	if (bActive) { EndConversation(); }
	if (!N || !N->FindProfile(NPCId)) { return false; }
	FMTANPCRuntime RT;
	N->GetRuntime(NPCId, RT);
	// Kurze NPC-NPC-Beschaeftigung (BusyUntil) blockiert den Spieler nicht: er unterbricht einfach.
	if (!RT.bActive || !RT.Current.bTalkable)
	{
		const bool bClosedShop = !N->FindProfile(NPCId)->ShopId.IsNone() && RT.Current.Activity != EMTAActivity::Sleep;
		PlayNPCBark(NPCId, bClosedShop ? FName(TEXT("trade_closed")) : FName(TEXT("not_available")));
		return false;
	}
	const FName DialogueId = GetDialogueForNPC(NPCId);
	if (DialogueId.IsNone())
	{
		// Figur ohne eigenen Dialog: Begruessung als Zuruf.
		PlayNPCBark(NPCId, TEXT("greeting"));
		return false;
	}
	return StartDialogue(DialogueId, NPCId);
}

bool UMTADialogueSubsystem::StartDialogue(FName DialogueId, FName NPCId)
{
	const TSharedPtr<FJsonObject>* Graph = Dialogues.Find(DialogueId);
	if (!Graph) { return false; }
	if (bActive) { EndConversation(); }
	FString Npc;
	if (NPCId.IsNone() && (*Graph)->TryGetStringField(TEXT("npc"), Npc)) { NPCId = FName(*Npc); }

	ActiveGraph = *Graph;
	ActiveDialogue = DialogueId;
	ActiveNPC = NPCId;
	bActive = true;
	RefreshStorySnapshot();

	FString StartNode;
	ActiveGraph->TryGetStringField(TEXT("start"), StartNode);
	// Unterbrochenes Gespraech fortsetzen.
	if (const FString* Resume = ResumeNodes.Find(NPCId))
	{
		StartNode = *Resume;
		ResumeNodes.Remove(NPCId);
	}
	else
	{
		// Einstiegspunkte nach Zustand (erste passende gewinnt).
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (ActiveGraph->TryGetArrayField(TEXT("entries"), Entries))
		{
			for (const TSharedPtr<FJsonValue>& V : *Entries)
			{
				const TSharedPtr<FJsonObject> En = V->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* When = nullptr;
				En->TryGetArrayField(TEXT("when"), When);
				FString Node;
				if (CheckAll(When) && En->TryGetStringField(TEXT("node"), Node)) { StartNode = Node; break; }
			}
		}
	}

	if (UMTANPCSubsystem* N = NPCs())
	{
		N->SetBusy(NPCId, 0.5f);
		if (AActor* Actor = N->GetNPCActor(NPCId))
		{
			if (Actor->GetClass()->ImplementsInterface(UMTANPCActor::StaticClass())) { IMTANPCActor::Execute_OnConversationStateChanged(Actor, true); }
		}
	}
	EnterNode(StartNode);
	return bActive;
}

TSharedPtr<FJsonObject> UMTADialogueSubsystem::GetNode(const FString& NodeId) const
{
	const TSharedPtr<FJsonObject>* Nodes = nullptr;
	if (!ActiveGraph.IsValid() || !ActiveGraph->TryGetObjectField(TEXT("nodes"), Nodes)) { return nullptr; }
	const TSharedPtr<FJsonObject>* Node = nullptr;
	return (*Nodes)->TryGetObjectField(NodeId, Node) ? *Node : nullptr;
}

void UMTADialogueSubsystem::EnterNode(const FString& NodeId)
{
	// Automatische Knoten (effect, check, trade) ohne Anzeige durchlaufen; Schutz gegen Endlosschleifen.
	FString Id = NodeId;
	for (int32 Guard = 0; Guard < 64 && bActive; ++Guard)
	{
		const TSharedPtr<FJsonObject> Node = GetNode(Id);
		if (!Node.IsValid())
		{
			UE_LOG(LogMTADialogue, Error, TEXT("Dialog %s: Knoten '%s' fehlt"), *ActiveDialogue.ToString(), *Id);
			EndConversation();
			return;
		}
		CurrentNode = Id;
		FString Type, Next;
		Node->TryGetStringField(TEXT("type"), Type);
		Node->TryGetStringField(TEXT("next"), Next);
		const TArray<TSharedPtr<FJsonValue>>* Effects = nullptr;
		if (Node->TryGetArrayField(TEXT("effects"), Effects)) { ApplyAll(Effects); }

		if (Type == TEXT("end"))
		{
			EndConversation();
			return;
		}
		if (Type == TEXT("effect")) { Id = Next; continue; }
		if (Type == TEXT("check"))
		{
			const TSharedPtr<FJsonObject>* Cond = nullptr;
			Node->TryGetObjectField(TEXT("cond"), Cond);
			FString Yes, No;
			Node->TryGetStringField(TEXT("yes"), Yes);
			Node->TryGetStringField(TEXT("no"), No);
			Id = (Cond && Check(*Cond)) ? Yes : No;
			continue;
		}
		if (Type == TEXT("trade"))
		{
			FString Shop;
			Node->TryGetStringField(TEXT("shop"), Shop);
			const UMTANPCSubsystem* N = NPCs();
			if (N && N->IsTrading(ActiveNPC)) { OnTradeRequested.Broadcast(ActiveNPC, FName(*Shop)); }
			else { PlayNPCBark(ActiveNPC, TEXT("trade_closed")); }
			Id = Next;
			continue;
		}
		BuildView(Node);
		OnDialogueView.Broadcast(View);
		return;
	}
	UE_LOG(LogMTADialogue, Error, TEXT("Dialog %s: Endlosschleife bei '%s'"), *ActiveDialogue.ToString(), *Id);
	EndConversation();
}

FString UMTADialogueSubsystem::ResolveSpeaker(const FString& Who, FName& OutId) const
{
	if (Who.IsEmpty()) { OutId = NAME_None; return FString(); }
	if (Who == TEXT("player")) { OutId = PlayerId; return TEXT("Mira"); }
	OutId = ResolveNPCRef(Who);
	const UMTANPCSubsystem* N = NPCs();
	const FMTANPCProfile* P = N ? N->FindProfile(OutId) : nullptr;
	return P ? P->DisplayName : OutId.ToString();
}

void UMTADialogueSubsystem::BuildView(const TSharedPtr<FJsonObject>& Node)
{
	View = FMTADialogueView();
	View.bActive = true;
	View.DialogueId = ActiveDialogue;
	View.NPCId = ActiveNPC;
	View.NodeId = CurrentNode;
	OptionMap.Reset();

	FString Type, Who, Text;
	Node->TryGetStringField(TEXT("type"), Type);
	Node->TryGetStringField(TEXT("who"), Who);
	Node->TryGetStringField(TEXT("text"), Text);
	if (Who.IsEmpty() && (Type == TEXT("rumor") || Type == TEXT("topics"))) { Who = NpcRef; }
	View.SpeakerName = ResolveSpeaker(Who, View.SpeakerId);
	View.Text = Text;

	if (Type == TEXT("rumor"))
	{
		// Geruecht als Gespraechsthema: der NPC erzaehlt das beste, das er kennt.
		FString About;
		Node->TryGetStringField(TEXT("about"), About);
		UMTAGossipSubsystem* G = GossipSys();
		const int32 RumorId = G ? G->PickRumorForPlayer(ActiveNPC, About.IsEmpty() ? FName(NAME_None) : ResolveNPCRef(About)) : -1;
		if (RumorId >= 0)
		{
			View.Text = Text.IsEmpty() ? G->GetRumorText(RumorId, ActiveNPC) : Text + TEXT(" ") + G->GetRumorText(RumorId, ActiveNPC);
			const FMTARumor* R = G->FindRumor(RumorId);
			if (R && !R->SecretId.IsNone()) { LocalFlags.Add(TEXT("dlg.clue.") + R->SecretId.ToString(), TEXT("true")); }
		}
		else
		{
			FString Fallback;
			Node->TryGetStringField(TEXT("fallback"), Fallback);
			View.Text = Fallback.IsEmpty() ? Text : Fallback;
		}
		return;
	}
	if (Type == TEXT("choice") || Type == TEXT("topics"))
	{
		View.bIsChoice = true;
		BuildOptions(Node);
	}
}

int32 UMTADialogueSubsystem::ComputeCheckValue(const TSharedPtr<FJsonObject>& CheckObj) const
{
	FString Skill;
	CheckObj->TryGetStringField(TEXT("skill"), Skill);
	int32 Value = GetPlayerSkill(FName(*Skill));
	const TArray<TSharedPtr<FJsonValue>>* Bonus = nullptr;
	if (CheckObj->TryGetArrayField(TEXT("bonus"), Bonus))
	{
		for (const TSharedPtr<FJsonValue>& BV : *Bonus)
		{
			const TSharedPtr<FJsonObject> B = BV->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* When = nullptr;
			B->TryGetArrayField(TEXT("when"), When);
			double V = 0.0;
			if (CheckAll(When) && B->TryGetNumberField(TEXT("value"), V)) { Value += (int32)V; }
		}
	}
	return Value;
}

void UMTADialogueSubsystem::BuildOptions(const TSharedPtr<FJsonObject>& Node)
{
	FString Type;
	Node->TryGetStringField(TEXT("type"), Type);
	if (Type == TEXT("topics"))
	{
		// "Fragen zu ..."-Liste aus den Themen des Dialogs, dann "Zurueck".
		const TArray<TSharedPtr<FJsonValue>>* Topics = nullptr;
		if (ActiveGraph->TryGetArrayField(TEXT("topics"), Topics))
		{
			for (int32 I = 0; I < Topics->Num(); ++I)
			{
				const TSharedPtr<FJsonObject> T = (*Topics)[I]->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* When = nullptr;
				T->TryGetArrayField(TEXT("when"), When);
				if (!CheckAll(When)) { continue; }
				FMTADialogueOptionView O;
				O.Index = View.Options.Num();
				T->TryGetStringField(TEXT("label"), O.Text);
				O.bIsTopic = true;
				View.Options.Add(O);
				OptionMap.Add(TPair<int32, int32>(1, I));
			}
		}
		FMTADialogueOptionView Back;
		Back.Index = View.Options.Num();
		Back.Text = TEXT("Das war alles.");
		View.Options.Add(Back);
		OptionMap.Add(TPair<int32, int32>(2, -1));
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* Options = nullptr;
	if (!Node->TryGetArrayField(TEXT("options"), Options)) { return; }
	for (int32 I = 0; I < Options->Num(); ++I)
	{
		const TSharedPtr<FJsonObject> Opt = (*Options)[I]->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* When = nullptr;
		Opt->TryGetArrayField(TEXT("when"), When);
		if (!CheckAll(When)) { continue; }
		FMTADialogueOptionView O;
		O.Index = View.Options.Num();
		Opt->TryGetStringField(TEXT("text"), O.Text);
		const TSharedPtr<FJsonObject>* CheckObj = nullptr;
		if (Opt->TryGetObjectField(TEXT("check"), CheckObj))
		{
			FString Skill;
			double Diff = 0.0;
			(*CheckObj)->TryGetStringField(TEXT("skill"), Skill);
			(*CheckObj)->TryGetNumberField(TEXT("difficulty"), Diff);
			O.bHasCheck = true;
			O.Skill = FName(*Skill);
			O.Difficulty = (int32)Diff;
			O.PlayerValue = ComputeCheckValue(*CheckObj);
			O.bWillSucceed = O.PlayerValue >= O.Difficulty;
		}
		View.Options.Add(O);
		OptionMap.Add(TPair<int32, int32>(0, I));
	}
}

void UMTADialogueSubsystem::Continue()
{
	if (!bActive || View.bIsChoice) { return; }
	const TSharedPtr<FJsonObject> Node = GetNode(CurrentNode);
	FString Next;
	if (!Node.IsValid() || !Node->TryGetStringField(TEXT("next"), Next)) { EndConversation(); return; }
	EnterNode(Next);
}

bool UMTADialogueSubsystem::ChooseOption(int32 Index)
{
	if (!bActive || !View.bIsChoice || !OptionMap.IsValidIndex(Index)) { return false; }
	const TPair<int32, int32> Map = OptionMap[Index];
	const TSharedPtr<FJsonObject> Node = GetNode(CurrentNode);
	if (!Node.IsValid()) { return false; }

	if (Map.Key == 2) // Themenliste verlassen
	{
		FString Next;
		Node->TryGetStringField(TEXT("next"), Next);
		EnterNode(Next);
		return true;
	}
	if (Map.Key == 1) // Thema
	{
		const TArray<TSharedPtr<FJsonValue>>* Topics = nullptr;
		ActiveGraph->TryGetArrayField(TEXT("topics"), Topics);
		FString Target;
		if (Topics && Topics->IsValidIndex(Map.Value)) { (*Topics)[Map.Value]->AsObject()->TryGetStringField(TEXT("node"), Target); }
		EnterNode(Target);
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* Options = nullptr;
	if (!Node->TryGetArrayField(TEXT("options"), Options) || !Options->IsValidIndex(Map.Value)) { return false; }
	const TSharedPtr<FJsonObject> Opt = (*Options)[Map.Value]->AsObject();
	const TArray<TSharedPtr<FJsonValue>>* Effects = nullptr;
	if (Opt->TryGetArrayField(TEXT("effects"), Effects)) { ApplyAll(Effects); }

	FString Next;
	const TSharedPtr<FJsonObject>* CheckObj = nullptr;
	if (Opt->TryGetObjectField(TEXT("check"), CheckObj))
	{
		// Deterministische Pruefung: kein Wuerfel, Ergebnis war in der Anzeige sichtbar.
		FString Skill, Success, Fail;
		double Diff = 0.0;
		(*CheckObj)->TryGetStringField(TEXT("skill"), Skill);
		(*CheckObj)->TryGetNumberField(TEXT("difficulty"), Diff);
		(*CheckObj)->TryGetStringField(TEXT("success"), Success);
		(*CheckObj)->TryGetStringField(TEXT("fail"), Fail);
		const int32 Value = ComputeCheckValue(*CheckObj);
		const bool bOk = Value >= (int32)Diff;
		OnSkillCheck.Broadcast(FName(*Skill), (int32)Diff, Value, bOk);
		Next = bOk ? Success : Fail;
	}
	else
	{
		Opt->TryGetStringField(TEXT("next"), Next);
	}
	if (bActive) { EnterNode(Next); }
	return true;
}

void UMTADialogueSubsystem::InterruptConversation(FName Reason)
{
	if (!bActive) { return; }
	const FName Npc = ActiveNPC;
	// Fortsetzung beim naechsten Ansprechen am selben Knoten.
	ResumeNodes.Add(Npc, CurrentNode);
	UE_LOG(LogMTADialogue, Verbose, TEXT("Gespraech mit %s unterbrochen: %s"), *Npc.ToString(), *Reason.ToString());
	bActive = false;
	View = FMTADialogueView();
	OptionMap.Reset();
	ActiveGraph.Reset();
	PlayNPCBark(Npc, TEXT("conversation_interrupt"));
	if (UMTANPCSubsystem* N = NPCs())
	{
		if (AActor* Actor = N->GetNPCActor(Npc))
		{
			if (Actor->GetClass()->ImplementsInterface(UMTANPCActor::StaticClass())) { IMTANPCActor::Execute_OnConversationStateChanged(Actor, false); }
		}
	}
	OnConversationEnded.Broadcast(Npc, true);
}

void UMTADialogueSubsystem::EndConversation()
{
	if (!bActive) { return; }
	const FName Npc = ActiveNPC;
	bActive = false;
	View = FMTADialogueView();
	OptionMap.Reset();
	ActiveGraph.Reset();
	CurrentNode.Reset();
	if (UMTANPCSubsystem* N = NPCs())
	{
		if (AActor* Actor = N->GetNPCActor(Npc))
		{
			if (Actor->GetClass()->ImplementsInterface(UMTANPCActor::StaticClass())) { IMTANPCActor::Execute_OnConversationStateChanged(Actor, false); }
		}
	}
	OnConversationEnded.Broadcast(Npc, false);
}

// ------------------------------------------------------------------------------------------------
// Fertigkeiten, Flags
// ------------------------------------------------------------------------------------------------

void UMTADialogueSubsystem::SetPlayerSkill(FName Skill, int32 Level)
{
	PlayerSkills.Add(Skill, FMath::Clamp(Level, 0, 10));
}

int32 UMTADialogueSubsystem::GetPlayerSkill(FName Skill) const
{
	const int32* L = PlayerSkills.Find(Skill);
	return L ? *L : 0;
}

FString UMTADialogueSubsystem::GetLocalFlag(const FString& Name) const
{
	const FString* V = LocalFlags.Find(Name);
	return V ? *V : FString(TEXT("false"));
}

void UMTADialogueSubsystem::SetLocalFlag(const FString& Name, const FString& Value)
{
	LocalFlags.Add(Name, Value);
}

// ------------------------------------------------------------------------------------------------
// Zurufe
// ------------------------------------------------------------------------------------------------

FString UMTADialogueSubsystem::GetBark(const FMTABarkContext& Ctx)
{
	const double Now = FPlatformTime::Seconds();
	if (!Ctx.bIgnoreCooldown)
	{
		const float* Cd = BarkCooldowns.Find(Ctx.Category);
		const float Cooldown = Cd ? *Cd : (BarkCooldowns.Contains(TEXT("default")) ? BarkCooldowns[TEXT("default")] : 20.f);
		const double* Last = LastBarkTime.Find(Ctx.Category);
		if (Last && Now - *Last < Cooldown) { return FString(); }
	}
	const UMTANPCSubsystem* N = NPCs();
	const FName Weather = Ctx.Weather.IsNone() ? (N ? N->GetWeather() : NAME_None) : Ctx.Weather;
	const float Hour = Ctx.Hour < 0.f ? (N ? N->GetHour() : 12.f) : Ctx.Hour;

	// Spezifische Gruppen (mit Filter) bevorzugen, allgemeine als Rueckfall.
	TArray<const FString*> Specific, General;
	for (const FBarkGroup& G : BarkGroups)
	{
		if (G.Category != Ctx.Category) { continue; }
		bool bFiltered = false;
		if (G.Districts.Num() > 0) { if (!G.Districts.Contains(Ctx.District)) { continue; } bFiltered = true; }
		if (G.Factions.Num() > 0) { if (!G.Factions.Contains(Ctx.Faction)) { continue; } bFiltered = true; }
		if (G.Roles.Num() > 0) { if (!G.Roles.Contains(Ctx.Role)) { continue; } bFiltered = true; }
		if (G.WeatherIds.Num() > 0) { if (!G.WeatherIds.Contains(Weather)) { continue; } bFiltered = true; }
		if (G.HourFrom >= 0.f)
		{
			const bool bIn = G.HourFrom <= G.HourTo ? (Hour >= G.HourFrom && Hour < G.HourTo) : (Hour >= G.HourFrom || Hour < G.HourTo);
			if (!bIn) { continue; }
			bFiltered = true;
		}
		if (G.MinWanted >= 0 && Ctx.WantedLevel < G.MinWanted) { continue; }
		if (G.MaxWanted >= 0 && Ctx.WantedLevel > G.MaxWanted) { continue; }
		if (G.MinSpeed >= 0.f && Ctx.SpeedKmh < G.MinSpeed) { continue; }
		for (const FString& L : G.Lines) { (bFiltered ? Specific : General).Add(&L); }
	}
	TArray<const FString*>& Pool = Specific.Num() > 0 && (General.Num() == 0 || BarkRng.FRand() < 0.7f) ? Specific : General;
	if (Pool.Num() == 0) { return FString(); }

	// Zuletzt gehoerte Zeilen meiden.
	const FString* Chosen = nullptr;
	for (int32 Try = 0; Try < 6 && !Chosen; ++Try)
	{
		const FString* Cand = Pool[BarkRng.RandRange(0, Pool.Num() - 1)];
		if (!RecentBarkHashes.Contains(GetTypeHash(*Cand)) || Try == 5) { Chosen = Cand; }
	}
	RecentBarkHashes.Add(GetTypeHash(*Chosen));
	if (RecentBarkHashes.Num() > 48) { RecentBarkHashes.RemoveAt(0); }
	if (!Ctx.bIgnoreCooldown) { LastBarkTime.Add(Ctx.Category, Now); }
	return *Chosen;
}

FString UMTADialogueSubsystem::PlayNPCBark(FName NPCId, FName Category, int32 WantedLevel, float SpeedKmh)
{
	UMTANPCSubsystem* N = NPCs();
	const FMTANPCProfile* P = N ? N->FindProfile(NPCId) : nullptr;
	FMTABarkContext Ctx;
	Ctx.Category = Category;
	Ctx.WantedLevel = WantedLevel;
	Ctx.SpeedKmh = SpeedKmh;
	if (P)
	{
		const FMTANPCRuntime* RT = N->FindRuntime(NPCId);
		Ctx.District = RT && !RT->Current.District.IsNone() ? RT->Current.District : P->District;
		Ctx.Faction = P->Faction;
		Ctx.Role = P->Role;
	}
	// Bekannte Spielerin: Geruechte schlagen die Begruessung.
	if (Category == TEXT("greeting") && P)
	{
		const UMTAGossipSubsystem* G = GossipSys();
		if (G && G->GetPlayerNotoriety(Ctx.District) > 0.25f) { Ctx.Category = TEXT("recognize_player"); }
	}
	const FString Text = GetBark(Ctx);
	if (Text.IsEmpty()) { return Text; }
	if (AActor* Actor = N ? N->GetNPCActor(NPCId) : nullptr)
	{
		if (Actor->GetClass()->ImplementsInterface(UMTANPCActor::StaticClass())) { IMTANPCActor::Execute_PlayBark(Actor, Text, Ctx.Category); }
	}
	OnBark.Broadcast(NPCId, Ctx.Category, Text);
	return Text;
}

// ------------------------------------------------------------------------------------------------
// Persistenz
// ------------------------------------------------------------------------------------------------

FString UMTADialogueSubsystem::ToJson() const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& P : LocalFlags) { F->SetStringField(P.Key, P.Value); }
	Root->SetObjectField(TEXT("flags"), F);
	TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
	for (const TPair<FName, int32>& P : PlayerSkills) { S->SetNumberField(P.Key.ToString(), P.Value); }
	Root->SetObjectField(TEXT("skills"), S);
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	for (const TPair<FName, FString>& P : ResumeNodes) { R->SetStringField(P.Key.ToString(), P.Value); }
	Root->SetObjectField(TEXT("resume"), R);
	return MTANPC::JsonToString(Root);
}

bool UMTADialogueSubsystem::FromJson(const FString& Json)
{
	const TSharedPtr<FJsonObject> Root = MTANPC::JsonFromString(Json);
	if (!Root.IsValid()) { return false; }
	LocalFlags.Reset();
	ResumeNodes.Reset();
	const TSharedPtr<FJsonObject>* O = nullptr;
	if (Root->TryGetObjectField(TEXT("flags"), O)) { for (const auto& P : (*O)->Values) { LocalFlags.Add(FString(*P.Key), P.Value->AsString()); } }
	if (Root->TryGetObjectField(TEXT("skills"), O)) { for (const auto& P : (*O)->Values) { PlayerSkills.Add(FName(*P.Key), (int32)P.Value->AsNumber()); } }
	if (Root->TryGetObjectField(TEXT("resume"), O)) { for (const auto& P : (*O)->Values) { ResumeNodes.Add(FName(*P.Key), P.Value->AsString()); } }
	return true;
}
