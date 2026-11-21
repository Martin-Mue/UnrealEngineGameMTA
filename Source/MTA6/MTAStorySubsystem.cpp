#include "MTAStorySubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Math/UnrealMathUtility.h"

void UMTAStorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadStory();
}

bool UMTAStorySubsystem::LoadStory(const FString& Path)
{
	const FString File = Path.IsEmpty() ? FPaths::ProjectContentDir() / TEXT("Data/story.json") : Path;
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *File))
	{
		UE_LOG(LogTemp, Error, TEXT("MTA6: story.json nicht lesbar: %s"), *File);
		return false;
	}
	TSharedPtr<FJsonObject> Parsed;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Parsed) || !Parsed.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("MTA6: story.json ist kein gueltiges JSON"));
		return false;
	}
	Data = Parsed;
	ResetState();
	return true;
}

void UMTAStorySubsystem::ResetState()
{
	Standing.Reset();
	NpcTrust.Reset();
	Flags.Reset();
	Tags.Reset();
	Completed.Reset();
	OutMoney = 0;
	OutHeat = 0.f;
	Aegis = 50.f;
	if (!Data.IsValid())
	{
		return;
	}
	const TSharedPtr<FJsonObject>* Start = nullptr;
	if (!Data->TryGetObjectField(TEXT("start"), Start))
	{
		return;
	}
	const TSharedPtr<FJsonObject>* S = nullptr;
	if ((*Start)->TryGetObjectField(TEXT("standing"), S))
	{
		for (const auto& Pair : (*S)->Values) { Standing.Add(FString(Pair.Key.ToView()), (float)Pair.Value->AsNumber()); }
	}
	const TSharedPtr<FJsonObject>* T = nullptr;
	if ((*Start)->TryGetObjectField(TEXT("npcTrust"), T))
	{
		for (const auto& Pair : (*T)->Values) { NpcTrust.Add(FString(Pair.Key.ToView()), (float)Pair.Value->AsNumber()); }
	}
	double A = 50.0;
	if ((*Start)->TryGetNumberField(TEXT("aegis"), A)) { Aegis = (float)A; }
}

bool UMTAStorySubsystem::CmpNum(const FString& Op, double A, double B)
{
	if (Op == TEXT(">=")) return A >= B;
	if (Op == TEXT(">")) return A > B;
	if (Op == TEXT("<=")) return A <= B;
	if (Op == TEXT("<")) return A < B;
	if (Op == TEXT("==")) return FMath::IsNearlyEqual(A, B);
	if (Op == TEXT("!=")) return !FMath::IsNearlyEqual(A, B);
	return false;
}

FString UMTAStorySubsystem::ValueToString(const TSharedPtr<FJsonValue>& V)
{
	if (!V.IsValid()) return TEXT("");
	switch (V->Type)
	{
	case EJson::Boolean: return V->AsBool() ? TEXT("true") : TEXT("false");
	case EJson::Number: return FString::SanitizeFloat(V->AsNumber());
	default: return V->AsString();
	}
}

bool UMTAStorySubsystem::Check(const TSharedPtr<FJsonObject>& C) const
{
	if (!C.IsValid()) return false;
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (C->TryGetArrayField(TEXT("standing"), A) && A->Num() == 3)
	{
		const float* V = Standing.Find((*A)[0]->AsString());
		return CmpNum((*A)[1]->AsString(), V ? *V : 0.f, (*A)[2]->AsNumber());
	}
	if (C->TryGetArrayField(TEXT("npcTrust"), A) && A->Num() == 3)
	{
		const float* V = NpcTrust.Find((*A)[0]->AsString());
		return CmpNum((*A)[1]->AsString(), V ? *V : 0.f, (*A)[2]->AsNumber());
	}
	if (C->TryGetArrayField(TEXT("flag"), A) && A->Num() == 3)
	{
		const FString* V = Flags.Find((*A)[0]->AsString());
		const FString Cur = V ? *V : TEXT("false");
		const FString Want = ValueToString((*A)[2]);
		const FString Op = (*A)[1]->AsString();
		return Op == TEXT("!=") ? Cur != Want : Cur == Want;
	}
	if (C->TryGetArrayField(TEXT("aegis"), A) && A->Num() == 2)
	{
		return CmpNum((*A)[0]->AsString(), Aegis, (*A)[1]->AsNumber());
	}
	FString Done;
	if (C->TryGetStringField(TEXT("done"), Done))
	{
		return Completed.Contains(Done);
	}
	if (C->TryGetArrayField(TEXT("tag"), A) && A->Num() == 3)
	{
		const TArray<FString>* T = Tags.Find((*A)[0]->AsString());
		return T && T->Contains((*A)[2]->AsString());
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
	return false;
}

bool UMTAStorySubsystem::CheckAll(const TArray<TSharedPtr<FJsonValue>>* Conds) const
{
	if (!Conds) return true;
	for (const TSharedPtr<FJsonValue>& V : *Conds)
	{
		if (!Check(V->AsObject())) return false;
	}
	return true;
}

void UMTAStorySubsystem::Apply(const TSharedPtr<FJsonObject>& E)
{
	if (!E.IsValid()) return;
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (E->TryGetArrayField(TEXT("standing"), A) && A->Num() == 2)
	{
		float& V = Standing.FindOrAdd((*A)[0]->AsString());
		V = FMath::Clamp(V + (float)(*A)[1]->AsNumber(), -100.f, 100.f);
	}
	if (E->TryGetArrayField(TEXT("npcTrust"), A) && A->Num() == 2)
	{
		float& V = NpcTrust.FindOrAdd((*A)[0]->AsString());
		V = FMath::Clamp(V + (float)(*A)[1]->AsNumber(), -3.f, 3.f);
	}
	if (E->TryGetArrayField(TEXT("flag"), A) && A->Num() == 2)
	{
		Flags.Add((*A)[0]->AsString(), ValueToString((*A)[1]));
	}
	if (E->TryGetArrayField(TEXT("tag"), A) && A->Num() == 3)
	{
		TArray<FString>& T = Tags.FindOrAdd((*A)[0]->AsString());
		const FString Mode = (*A)[1]->AsString();
		if (Mode == TEXT("add")) { T.AddUnique((*A)[2]->AsString()); }
		else if (Mode == TEXT("remove")) { T.Remove((*A)[2]->AsString()); }
	}
	double N = 0.0;
	if (E->TryGetNumberField(TEXT("aegis"), N)) { Aegis = FMath::Clamp(Aegis + (float)N, 0.f, 100.f); }
	if (E->TryGetNumberField(TEXT("money"), N)) { OutMoney += (int32)N; }
	if (E->TryGetNumberField(TEXT("heat"), N)) { OutHeat += (float)N; }
}

void UMTAStorySubsystem::ApplyAll(const TArray<TSharedPtr<FJsonValue>>* Effects)
{
	if (!Effects) return;
	for (const TSharedPtr<FJsonValue>& V : *Effects) { Apply(V->AsObject()); }
}

TSharedPtr<FJsonObject> UMTAStorySubsystem::FindMission(const FString& Id) const
{
	const TArray<TSharedPtr<FJsonValue>>* Missions = nullptr;
	if (Data.IsValid() && Data->TryGetArrayField(TEXT("missions"), Missions))
	{
		for (const TSharedPtr<FJsonValue>& V : *Missions)
		{
			const TSharedPtr<FJsonObject> M = V->AsObject();
			if (M.IsValid() && M->GetStringField(TEXT("id")) == Id) { return M; }
		}
	}
	return nullptr;
}

FMTAMission UMTAStorySubsystem::MissionFromJson(const TSharedPtr<FJsonObject>& M)
{
	FMTAMission R;
	R.Id = M->GetStringField(TEXT("id"));
	R.Title = M->GetStringField(TEXT("title"));
	M->TryGetStringField(TEXT("text"), R.Text);
	M->TryGetBoolField(TEXT("main"), R.bMain);
	M->TryGetBoolField(TEXT("finale"), R.bFinale);
	int32 Act = 0;
	if (M->TryGetNumberField(TEXT("act"), Act)) { R.Act = Act; }
	const TArray<TSharedPtr<FJsonValue>>* Cell = nullptr;
	if (M->TryGetArrayField(TEXT("cell"), Cell) && Cell->Num() == 2)
	{
		R.CellX = (int32)(*Cell)[0]->AsNumber();
		R.CellY = (int32)(*Cell)[1]->AsNumber();
	}
	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (M->TryGetArrayField(TEXT("choices"), Choices)) { R.NumChoices = Choices->Num(); }
	return R;
}

TArray<FMTAMission> UMTAStorySubsystem::GetAvailableMissions() const
{
	TArray<FMTAMission> Result;
	const TArray<TSharedPtr<FJsonValue>>* Missions = nullptr;
	if (!Data.IsValid() || !Data->TryGetArrayField(TEXT("missions"), Missions)) return Result;
	for (const TSharedPtr<FJsonValue>& V : *Missions)
	{
		const TSharedPtr<FJsonObject> M = V->AsObject();
		if (!M.IsValid() || Completed.Contains(M->GetStringField(TEXT("id")))) continue;
		const TArray<TSharedPtr<FJsonValue>>* Req = nullptr;
		M->TryGetArrayField(TEXT("requires"), Req);
		if (!CheckAll(Req)) continue;
		bool bBlocked = false;
		const TArray<TSharedPtr<FJsonValue>>* Exc = nullptr;
		if (M->TryGetArrayField(TEXT("excludes"), Exc))
		{
			for (const TSharedPtr<FJsonValue>& C : *Exc) { if (Check(C->AsObject())) bBlocked = true; }
		}
		if (bBlocked) continue;
		Result.Add(MissionFromJson(M));
	}
	Result.Sort([](const FMTAMission& A, const FMTAMission& B)
	{
		if (A.bMain != B.bMain) return A.bMain;
		return A.Act < B.Act;
	});
	return Result;
}

FString UMTAStorySubsystem::GetChoiceText(const FString& MissionId, int32 ChoiceIndex) const
{
	const TSharedPtr<FJsonObject> M = FindMission(MissionId);
	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (M.IsValid() && M->TryGetArrayField(TEXT("choices"), Choices) && Choices->IsValidIndex(ChoiceIndex))
	{
		return (*Choices)[ChoiceIndex]->AsObject()->GetStringField(TEXT("text"));
	}
	return FString();
}

TArray<FMTAChoiceOption> UMTAStorySubsystem::GetChoiceOptions(const FString& MissionId, int32 ChoiceIndex) const
{
	TArray<FMTAChoiceOption> Result;
	const TSharedPtr<FJsonObject> M = FindMission(MissionId);
	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!M.IsValid() || !M->TryGetArrayField(TEXT("choices"), Choices) || !Choices->IsValidIndex(ChoiceIndex)) return Result;
	const TArray<TSharedPtr<FJsonValue>>* Options = nullptr;
	if (!(*Choices)[ChoiceIndex]->AsObject()->TryGetArrayField(TEXT("options"), Options)) return Result;
	for (const TSharedPtr<FJsonValue>& V : *Options)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* When = nullptr;
		O->TryGetArrayField(TEXT("when"), When);
		if (!CheckAll(When)) continue;
		FMTAChoiceOption Opt;
		Opt.Id = O->GetStringField(TEXT("id"));
		Opt.Text = O->GetStringField(TEXT("text"));
		Result.Add(Opt);
	}
	return Result;
}

bool UMTAStorySubsystem::ChooseOption(const FString& MissionId, int32 ChoiceIndex, const FString& OptionId)
{
	const TSharedPtr<FJsonObject> M = FindMission(MissionId);
	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!M.IsValid() || !M->TryGetArrayField(TEXT("choices"), Choices) || !Choices->IsValidIndex(ChoiceIndex)) return false;
	const TArray<TSharedPtr<FJsonValue>>* Options = nullptr;
	if (!(*Choices)[ChoiceIndex]->AsObject()->TryGetArrayField(TEXT("options"), Options)) return false;
	for (const TSharedPtr<FJsonValue>& V : *Options)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (O->GetStringField(TEXT("id")) == OptionId)
		{
			const TArray<TSharedPtr<FJsonValue>>* Eff = nullptr;
			O->TryGetArrayField(TEXT("effects"), Eff);
			ApplyAll(Eff);
			return true;
		}
	}
	return false;
}

void UMTAStorySubsystem::CompleteMission(const FString& MissionId)
{
	const TSharedPtr<FJsonObject> M = FindMission(MissionId);
	if (!M.IsValid()) return;
	const TArray<TSharedPtr<FJsonValue>>* Eff = nullptr;
	M->TryGetArrayField(TEXT("onSuccess"), Eff);
	ApplyAll(Eff);
	Completed.AddUnique(MissionId);
}

void UMTAStorySubsystem::ApplyEffectsJson(const FString& EffectsJson)
{
	TArray<TSharedPtr<FJsonValue>> Effects;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(EffectsJson), Effects))
	{
		return;
	}
	ApplyAll(&Effects);
}

FMTAEnding UMTAStorySubsystem::EvaluateEnding() const
{
	FMTAEnding Best;
	int32 BestPriority = -9999;
	const TArray<TSharedPtr<FJsonValue>>* Endings = nullptr;
	if (!Data.IsValid() || !Data->TryGetArrayField(TEXT("endings"), Endings)) return Best;
	for (const TSharedPtr<FJsonValue>& V : *Endings)
	{
		const TSharedPtr<FJsonObject> E = V->AsObject();
		const int32 Priority = (int32)E->GetNumberField(TEXT("priority"));
		const TArray<TSharedPtr<FJsonValue>>* When = nullptr;
		E->TryGetArrayField(TEXT("when"), When);
		if (Priority > BestPriority && CheckAll(When))
		{
			BestPriority = Priority;
			Best.Id = E->GetStringField(TEXT("id"));
			Best.Title = E->GetStringField(TEXT("title"));
			Best.Epilogue = E->GetStringField(TEXT("epilogue"));
		}
	}
	return Best;
}

int32 UMTAStorySubsystem::ConsumeMoney() { const int32 V = OutMoney; OutMoney = 0; return V; }
float UMTAStorySubsystem::ConsumeHeat() { const float V = OutHeat; OutHeat = 0.f; return V; }

float UMTAStorySubsystem::GetStanding(const FString& Faction) const
{
	const float* V = Standing.Find(Faction);
	return V ? *V : 0.f;
}

FString UMTAStorySubsystem::StateToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema"), 1);
	Root->SetNumberField(TEXT("aegis"), Aegis);
	TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
	for (const auto& P : Standing) { S->SetNumberField(P.Key, P.Value); }
	Root->SetObjectField(TEXT("standing"), S);
	TSharedRef<FJsonObject> N = MakeShared<FJsonObject>();
	for (const auto& P : NpcTrust) { N->SetNumberField(P.Key, P.Value); }
	Root->SetObjectField(TEXT("npcTrust"), N);
	TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
	for (const auto& P : Flags) { F->SetStringField(P.Key, P.Value); }
	Root->SetObjectField(TEXT("flags"), F);
	TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
	for (const auto& P : Tags)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& Tag : P.Value) { Arr.Add(MakeShared<FJsonValueString>(Tag)); }
		T->SetArrayField(P.Key, Arr);
	}
	Root->SetObjectField(TEXT("tags"), T);
	TArray<TSharedPtr<FJsonValue>> Done;
	for (const FString& Id : Completed) { Done.Add(MakeShared<FJsonValueString>(Id)); }
	Root->SetArrayField(TEXT("completed"), Done);
	FString Out;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Out));
	return Out;
}

bool UMTAStorySubsystem::StateFromJson(const FString& Json)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) return false;
	if ((int32)Root->GetNumberField(TEXT("schema")) != 1) return false;
	Standing.Reset(); NpcTrust.Reset(); Flags.Reset(); Tags.Reset(); Completed.Reset();
	Aegis = (float)Root->GetNumberField(TEXT("aegis"));
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (Root->TryGetObjectField(TEXT("standing"), Obj)) { for (const auto& P : (*Obj)->Values) { Standing.Add(FString(P.Key.ToView()), (float)P.Value->AsNumber()); } }
	if (Root->TryGetObjectField(TEXT("npcTrust"), Obj)) { for (const auto& P : (*Obj)->Values) { NpcTrust.Add(FString(P.Key.ToView()), (float)P.Value->AsNumber()); } }
	if (Root->TryGetObjectField(TEXT("flags"), Obj)) { for (const auto& P : (*Obj)->Values) { Flags.Add(FString(P.Key.ToView()), P.Value->AsString()); } }
	if (Root->TryGetObjectField(TEXT("tags"), Obj))
	{
		for (const auto& P : (*Obj)->Values)
		{
			TArray<FString>& T = Tags.FindOrAdd(FString(P.Key.ToView()));
			for (const TSharedPtr<FJsonValue>& V : P.Value->AsArray()) { T.Add(V->AsString()); }
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Done = nullptr;
	if (Root->TryGetArrayField(TEXT("completed"), Done)) { for (const TSharedPtr<FJsonValue>& V : *Done) { Completed.Add(V->AsString()); } }
	return true;
}

FString UMTAStorySubsystem::RunStorySimulation(int32 Runs)
{
	FRandomStream Rng(7);
	TMap<FString, int32> Counts;
	int32 DeadEnds = 0;
	for (int32 R = 0; R < Runs; ++R)
	{
		ResetState();
		for (int32 Guard = 0; Guard < 100; ++Guard)
		{
			const TArray<FMTAMission> Av = GetAvailableMissions();
			if (Av.IsEmpty()) break;
			const FMTAMission& M = Av[Rng.RandRange(0, Av.Num() - 1)];
			for (int32 C = 0; C < M.NumChoices; ++C)
			{
				const TArray<FMTAChoiceOption> Opts = GetChoiceOptions(M.Id, C);
				if (Opts.IsEmpty()) { ++DeadEnds; break; }
				ChooseOption(M.Id, C, Opts[Rng.RandRange(0, Opts.Num() - 1)].Id);
			}
			CompleteMission(M.Id);
		}
		if (!Completed.Contains(TEXT("w5_finale"))) { ++DeadEnds; }
		Counts.FindOrAdd(EvaluateEnding().Id)++;
	}
	ResetState();
	FString Report = FString::Printf(TEXT("STORYSIM runs=%d sackgassen=%d enden:"), Runs, DeadEnds);
	for (const auto& P : Counts) { Report += FString::Printf(TEXT(" %s=%d"), *P.Key, P.Value); }
	return Report;
}
