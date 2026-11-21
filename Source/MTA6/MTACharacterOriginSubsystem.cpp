#include "MTACharacterOriginSubsystem.h"
#include "MTAEconomySubsystem.h"
#include "MTAProgressionSubsystem.h"
#include "MTAGarageSubsystem.h"
#include "MTAInventorySubsystem.h"
#include "MTAVehicleCatalog.h"
#include "MTAStorySubsystem.h"
#include "Engine/GameInstance.h"

namespace MTAP = MTAProgression;

void UMTACharacterOriginSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMTAEconomySubsystem>();
	Collection.InitializeDependency<UMTAProgressionSubsystem>();
	Collection.InitializeDependency<UMTAVehicleCatalog>();
	Collection.InitializeDependency<UMTAGarageSubsystem>();
	Collection.InitializeDependency<UMTAInventorySubsystem>();
	LoadOrigins();
}

bool UMTACharacterOriginSubsystem::LoadOrigins(const FString& RelativePath)
{
	TSharedPtr<FJsonObject> Data;
	if (!MTAP::LoadJsonFile(RelativePath, Data)) return false;
	Origins.Reset();
	if (const TArray<TSharedPtr<FJsonValue>>* A = MTAP::Arr(Data, TEXT("origins")))
	{
		for (const TSharedPtr<FJsonValue>& V : *A)
		{
			const TSharedPtr<FJsonObject> J = V->AsObject();
			FMTAOrigin O;
			O.Id = FName(*MTAP::Str(J, TEXT("id")));
			O.Name = MTAP::Str(J, TEXT("name"));
			O.Summary = MTAP::Str(J, TEXT("summary"));
			O.Backstory = MTAP::Str(J, TEXT("backstory"));
			O.PrologVariant = FName(*MTAP::Str(J, TEXT("prologVariant")));
			O.StartMoney = (int64)MTAP::Num(J, TEXT("startMoney"), 1000.0);
			O.StartDebt = (int64)MTAP::Num(J, TEXT("startDebt"), 0.0);
			O.AegisDelta = (float)MTAP::Num(J, TEXT("aegisDelta"), 0.0);
			O.Standing = MTAP::ReadNumberMap(MTAP::Obj(J, TEXT("standing")));
			O.StartVehicle = FName(*MTAP::Str(J, TEXT("startVehicle")));
			O.StartDistrict = FName(*MTAP::Str(J, TEXT("startDistrict"), TEXT("core")));
			if (const TSharedPtr<FJsonObject> G = MTAP::Obj(J, TEXT("garage")))
			{
				O.GarageDistrict = FName(*MTAP::Str(G, TEXT("district")));
				O.GarageLevel = (int32)MTAP::Num(G, TEXT("level"), 0.0);
			}
			O.PerkPoints = (int32)MTAP::Num(J, TEXT("perkPoints"), 0.0);
			O.StartSkills = MTAP::ReadNameArray(MTAP::Arr(J, TEXT("startSkills")));
			O.TreeXp = MTAP::ReadNumberMap(MTAP::Obj(J, TEXT("treeXp")));
			if (const TArray<TSharedPtr<FJsonValue>>* Items = MTAP::Arr(J, TEXT("items")))
			{
				for (const TSharedPtr<FJsonValue>& IV : *Items)
				{
					const TArray<TSharedPtr<FJsonValue>>& Pair = IV->AsArray();
					if (Pair.Num() == 2) { O.ItemIds.Add(FName(*Pair[0]->AsString())); O.ItemCounts.Add((int32)Pair[1]->AsNumber()); }
				}
			}
			if (const TArray<TSharedPtr<FJsonValue>>* Traits = MTAP::Arr(J, TEXT("traits")))
			{
				for (const TSharedPtr<FJsonValue>& TV : *Traits)
				{
					const TSharedPtr<FJsonObject> T = TV->AsObject();
					FMTAOriginTrait Tr;
					Tr.Id = FName(*MTAP::Str(T, TEXT("id")));
					Tr.Name = MTAP::Str(T, TEXT("name"));
					Tr.bPositive = MTAP::Bool(T, TEXT("positive"), true);
					Tr.Effects = MTAP::ReadModifiers(MTAP::Arr(T, TEXT("effects")));
					O.Traits.Add(Tr);
				}
			}
			if (const TSharedPtr<FJsonObject> F = MTAP::Obj(J, TEXT("flags")))
			{
				for (const auto& P : F->Values)
				{
					FString Val;
					bool B = false;
					if (P.Value->TryGetBool(B)) { Val = B ? TEXT("true") : TEXT("false"); }
					else { Val = P.Value->AsString(); }
					O.DialogFlags.Add(FString(*P.Key), Val);
				}
			}
			Origins.Add(O);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("MTA6: %d Herkuenfte geladen"), Origins.Num());
	return true;
}

const FMTAOrigin* UMTACharacterOriginSubsystem::Find(FName Id) const
{
	return Origins.FindByPredicate([Id](const FMTAOrigin& O) { return O.Id == Id; });
}

bool UMTACharacterOriginSubsystem::GetOrigin(FName OriginId, FMTAOrigin& Out) const
{
	if (const FMTAOrigin* O = Find(OriginId)) { Out = *O; return true; }
	return false;
}

void UMTACharacterOriginSubsystem::ApplyTraits(const FMTAOrigin& O) const
{
	const UGameInstance* GI = GetGameInstance();
	UMTAProgressionSubsystem* Prog = GI ? GI->GetSubsystem<UMTAProgressionSubsystem>() : nullptr;
	if (!Prog) return;
	TArray<FMTAStatModifier> Mods;
	TArray<FName> Tags;
	for (const FMTAOriginTrait& T : O.Traits)
	{
		Mods.Append(T.Effects);
		Tags.Add(T.Id);
	}
	Tags.Add(FName(*(TEXT("origin.") + O.Id.ToString())));
	Prog->SetExternalModifiers(TEXT("origin"), Mods, Tags);
}

bool UMTACharacterOriginSubsystem::ApplyOrigin(FName OriginId, const FString& InCharacterName, int32 AppearanceIndex)
{
	const FMTAOrigin* O = Find(OriginId);
	UGameInstance* GI = GetGameInstance();
	if (!O || !GI) return false;
	UMTAEconomySubsystem* Econ = GI->GetSubsystem<UMTAEconomySubsystem>();
	UMTAProgressionSubsystem* Prog = GI->GetSubsystem<UMTAProgressionSubsystem>();
	UMTAGarageSubsystem* Garage = GI->GetSubsystem<UMTAGarageSubsystem>();
	UMTAInventorySubsystem* Inv = GI->GetSubsystem<UMTAInventorySubsystem>();
	UMTAStorySubsystem* Story = GI->GetSubsystem<UMTAStorySubsystem>();

	SelectedOrigin = OriginId;
	CharacterName = InCharacterName;
	Appearance = AppearanceIndex;

	if (Story) { Story->ResetState(); }
	if (Econ)
	{
		Econ->ResetToStart(O->StartMoney);
		Econ->AddExistingDebt(O->StartDebt);
	}
	if (Prog)
	{
		Prog->ResetProgression();
		ApplyTraits(*O);
		Prog->AddPerkPoints(O->PerkPoints);
		for (const auto& P : O->TreeXp) { Prog->AddTreeXp(P.Key, P.Value); }
		for (const FName& S : O->StartSkills) { Prog->UnlockSkill(S, true); }
	}
	if (Garage)
	{
		Garage->ResetGarage();
		if (!O->GarageDistrict.IsNone()) { Garage->GrantGarage(O->GarageDistrict, O->GarageLevel); }
		if (!O->StartVehicle.IsNone())
		{
			const FName Veh = Garage->AddVehicle(O->StartVehicle, O->GarageDistrict.IsNone() ? O->StartDistrict : O->GarageDistrict, false);
			Garage->SetActiveVehicle(Veh);
			Garage->SetInsurance(Veh, 0);
		}
	}
	if (Inv)
	{
		Inv->ResetInventory();
		Inv->SetCurrentDistrict(O->StartDistrict);
		for (int32 I = 0; I < O->ItemIds.Num(); ++I) { Inv->AddItem(O->ItemIds[I], O->ItemCounts.IsValidIndex(I) ? O->ItemCounts[I] : 1, EMTAInventoryContainer::Personal); }
	}
	// Story-Zustand: Staende addieren, Aegis verschieben, Dialogflags setzen
	const FMTAOrigin Copy = *O;
	MTAP::PatchStoryState(GI, [&Copy](const TSharedPtr<FJsonObject>& State)
	{
		State->SetNumberField(TEXT("aegis"), FMath::Clamp(MTAP::Num(State, TEXT("aegis"), 50.0) + Copy.AegisDelta, 0.0, 100.0));
		TSharedPtr<FJsonObject> St = MTAP::Obj(State, TEXT("standing"));
		if (!St.IsValid()) { St = MakeShared<FJsonObject>(); State->SetObjectField(TEXT("standing"), St); }
		for (const auto& P : Copy.Standing)
		{
			St->SetNumberField(P.Key.ToString(), FMath::Clamp(MTAP::Num(St, P.Key.ToString(), 0.0) + P.Value, -100.0, 100.0));
		}
		TSharedPtr<FJsonObject> Fl = MTAP::Obj(State, TEXT("flags"));
		if (!Fl.IsValid()) { Fl = MakeShared<FJsonObject>(); State->SetObjectField(TEXT("flags"), Fl); }
		for (const auto& P : Copy.DialogFlags) { Fl->SetStringField(TEXT("origin.") + P.Key, P.Value); }
		Fl->SetStringField(TEXT("origin"), Copy.Id.ToString());
		Fl->SetStringField(TEXT("prolog"), Copy.PrologVariant.ToString());
	});
	if (Prog) { Prog->EvaluateReputationRewards(); }
	OnOriginApplied.Broadcast(OriginId);
	return true;
}

bool UMTACharacterOriginSubsystem::HasOriginFlag(const FString& Flag) const
{
	const FMTAOrigin* O = Find(SelectedOrigin);
	const FString* V = O ? O->DialogFlags.Find(Flag) : nullptr;
	return V && !V->Equals(TEXT("false"), ESearchCase::IgnoreCase) && !V->IsEmpty();
}

TSharedRef<FJsonObject> UMTACharacterOriginSubsystem::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema"), SchemaVersion);
	Root->SetStringField(TEXT("origin"), SelectedOrigin.ToString());
	Root->SetStringField(TEXT("name"), CharacterName);
	Root->SetNumberField(TEXT("appearance"), Appearance);
	return Root;
}

bool UMTACharacterOriginSubsystem::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid()) return false;
	const int32 Schema = (int32)MTAP::Num(Json, TEXT("schema"), 0.0);
	if (Schema < 1 || Schema > SchemaVersion) return false;
	SelectedOrigin = FName(*MTAP::Str(Json, TEXT("origin"), TEXT("None")));
	CharacterName = MTAP::Str(Json, TEXT("name"));
	Appearance = (int32)MTAP::Num(Json, TEXT("appearance"), 0.0);
	// Eigenschaften werden mit dem Fortschritt ("external.origin") gespeichert; hier nur bei Bedarf neu setzen.
	return true;
}
