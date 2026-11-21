#include "MTAProgressionSubsystem.h"
#include "MTAEconomySubsystem.h"
#include "MTAInventorySubsystem.h"
#include "MTAGarageSubsystem.h"
#include "MTACharacterOriginSubsystem.h"
#include "Engine/GameInstance.h"

namespace MTAP = MTAProgression;

void UMTAProgressionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadSkills();
}

bool UMTAProgressionSubsystem::LoadSkills(const FString& RelativePath)
{
	TSharedPtr<FJsonObject> Parsed;
	if (!MTAP::LoadJsonFile(RelativePath, Parsed)) return false;
	Data = Parsed;
	Trees.Reset();
	Skills.Reset();
	Rewards.Reset();
	MasteryThresholds.Reset();

	if (const TArray<TSharedPtr<FJsonValue>>* M = MTAP::Arr(MTAP::Obj(Data, TEXT("xp")), TEXT("masteryThresholds")))
	{
		for (const TSharedPtr<FJsonValue>& V : *M) { MasteryThresholds.Add((float)V->AsNumber()); }
	}

	if (const TArray<TSharedPtr<FJsonValue>>* TreeArr = MTAP::Arr(Data, TEXT("trees")))
	{
		for (const TSharedPtr<FJsonValue>& TV : *TreeArr)
		{
			const TSharedPtr<FJsonObject> T = TV->AsObject();
			FMTASkillTreeDef Tree;
			Tree.Id = FName(*MTAP::Str(T, TEXT("id")));
			Tree.Name = MTAP::Str(T, TEXT("name"));
			Tree.Description = MTAP::Str(T, TEXT("description"));
			if (const TArray<TSharedPtr<FJsonValue>>* SkillArr = MTAP::Arr(T, TEXT("skills")))
			{
				for (const TSharedPtr<FJsonValue>& SV : *SkillArr)
				{
					const TSharedPtr<FJsonObject> S = SV->AsObject();
					FMTASkillDef Def;
					Def.Id = FName(*MTAP::Str(S, TEXT("id")));
					Def.TreeId = Tree.Id;
					Def.Name = MTAP::Str(S, TEXT("name"));
					Def.Description = MTAP::Str(S, TEXT("desc"));
					Def.Tier = (int32)MTAP::Num(S, TEXT("tier"), 1.0);
					Def.MaxRank = FMath::Max(1, (int32)MTAP::Num(S, TEXT("maxRank"), 1.0));
					Def.RequiredLevel = (int32)MTAP::Num(S, TEXT("level"), 1.0);
					Def.RequiredMastery = (int32)MTAP::Num(S, TEXT("mastery"), 0.0);
					if (const TArray<TSharedPtr<FJsonValue>>* C = MTAP::Arr(S, TEXT("cost")))
					{
						for (const TSharedPtr<FJsonValue>& V : *C) { Def.CostPerRank.Add((int32)V->AsNumber()); }
					}
					if (const TArray<TSharedPtr<FJsonValue>>* R = MTAP::Arr(S, TEXT("requires")))
					{
						for (const TSharedPtr<FJsonValue>& V : *R)
						{
							// Format "skill_id" oder "skill_id:rang"
							const FString Raw = V->AsString();
							FString Id = Raw, RankStr;
							int32 Rank = 1;
							if (Raw.Split(TEXT(":"), &Id, &RankStr)) { Rank = FMath::Max(1, FCString::Atoi(*RankStr)); }
							Def.Requires.Add(FName(*Id));
							Def.RequiredRanks.Add(Rank);
						}
					}
					Def.Effects = MTAP::ReadModifiers(MTAP::Arr(S, TEXT("effects")));
					Def.Tags = MTAP::ReadNameArray(MTAP::Arr(S, TEXT("tags")));
					Tree.SkillIds.Add(Def.Id);
					Skills.Add(Def.Id, Def);
				}
			}
			Trees.Add(Tree);
		}
	}

	auto ReadRewards = [this](const TSharedPtr<FJsonObject>& Section, bool bPenalty)
	{
		if (!Section.IsValid()) return;
		for (const auto& P : Section->Values)
		{
			const FName Faction(*P.Key);
			for (const TSharedPtr<FJsonValue>& V : P.Value->AsArray())
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				FMTAReputationReward R;
				R.Id = FName(*MTAP::Str(O, TEXT("id")));
				R.Faction = Faction;
				R.Name = MTAP::Str(O, TEXT("name"), R.Id.ToString());
				R.Threshold = (float)MTAP::Num(O, TEXT("threshold"), 0.0);
				R.bPenalty = bPenalty;
				R.Money = (int64)MTAP::Num(O, TEXT("money"), 0.0);
				R.PerkPoints = (int32)MTAP::Num(O, TEXT("perkPoints"), 0.0);
				R.Effects = MTAP::ReadModifiers(MTAP::Arr(O, TEXT("effects")));
				R.Tags = MTAP::ReadNameArray(MTAP::Arr(O, TEXT("tags")));
				if (const TArray<TSharedPtr<FJsonValue>>* Items = MTAP::Arr(O, TEXT("items")))
				{
					for (const TSharedPtr<FJsonValue>& IV : *Items)
					{
						const TArray<TSharedPtr<FJsonValue>>& Pair = IV->AsArray();
						if (Pair.Num() == 2)
						{
							R.ItemIds.Add(FName(*Pair[0]->AsString()));
							R.ItemCounts.Add((int32)Pair[1]->AsNumber());
						}
					}
				}
				Rewards.Add(R);
			}
		}
	};
	const TSharedPtr<FJsonObject> Rep = MTAP::Obj(Data, TEXT("reputation"));
	ReadRewards(MTAP::Obj(Rep, TEXT("rewards")), false);
	ReadRewards(MTAP::Obj(Rep, TEXT("penalties")), true);

	ResetProgression();
	UE_LOG(LogTemp, Display, TEXT("MTA6: %d Skillbaeume, %d Skills, %d Ruf-Eintraege geladen"), Trees.Num(), Skills.Num(), Rewards.Num());
	return true;
}

void UMTAProgressionSubsystem::ResetProgression()
{
	Xp = 0;
	Level = 1;
	PerkPoints = 0;
	SkillRanks.Reset();
	TreeXp.Reset();
	ClaimedRewards.Reset();
	ActiveRepEffects.Reset();
	ExternalMods.Reset();
	ExternalTags.Reset();
	MarkDirty();
}

// ---------------------------------------------------------------------------------------------
// Erfahrung
// ---------------------------------------------------------------------------------------------

int64 UMTAProgressionSubsystem::GetXpForLevel(int32 InLevel) const
{
	// Summe base * (l-1)^exp: Level 2 = 900, Level 10 ca. 16.000, Level 30 ca. 120.000 XP
	const TSharedPtr<FJsonObject> X = MTAP::Obj(Data, TEXT("xp"));
	const double Base = MTAP::Num(X, TEXT("levelBase"), 900.0);
	const double Exp = MTAP::Num(X, TEXT("levelExp"), 1.32);
	double Total = 0.0;
	for (int32 L = 1; L < InLevel; ++L) { Total += Base * FMath::Pow((double)L, Exp); }
	return (int64)Total;
}

float UMTAProgressionSubsystem::GetLevelProgress() const
{
	const int64 Cur = GetXpForLevel(Level);
	const int64 Next = GetXpForLevel(Level + 1);
	return Next > Cur ? FMath::Clamp((float)(Xp - Cur) / (float)(Next - Cur), 0.f, 1.f) : 1.f;
}

int32 UMTAProgressionSubsystem::AddXp(int32 Amount)
{
	if (Amount <= 0) return 0;
	Xp += (int64)FMath::RoundToInt(ApplyStat(TEXT("xp.gain"), (float)Amount));
	const TSharedPtr<FJsonObject> X = MTAP::Obj(Data, TEXT("xp"));
	const int32 MaxLevel = (int32)MTAP::Num(X, TEXT("maxLevel"), 50.0);
	const int32 PerLevel = (int32)MTAP::Num(X, TEXT("perkPointsPerLevel"), 1.0);
	const int32 BonusEvery = (int32)MTAP::Num(X, TEXT("bonusPerkEvery"), 5.0);
	int32 Gained = 0;
	while (Level < MaxLevel && Xp >= GetXpForLevel(Level + 1))
	{
		++Level;
		++Gained;
		PerkPoints += PerLevel + ((BonusEvery > 0 && Level % BonusEvery == 0) ? 1 : 0);
		OnLevelUp.Broadcast(Level, PerkPoints);
	}
	return Gained;
}

int32 UMTAProgressionSubsystem::AwardXpFromSource(FName Source, float Count)
{
	const FString Key = Source.ToString();
	const double Base = MTAP::Num(MTAP::Obj(Data, TEXT("xpSources")), Key, 0.0);
	if (const TArray<TSharedPtr<FJsonValue>>* T = MTAP::Arr(MTAP::Obj(Data, TEXT("treeXpSources")), Key))
	{
		if (T->Num() == 2) { AddTreeXp(FName(*(*T)[0]->AsString()), (float)((*T)[1]->AsNumber() * Count)); }
	}
	// Drift-XP hat einen eigenen Multiplikator (Skill "Driftlinie").
	const float Mul = Key.StartsWith(TEXT("drift")) ? ApplyStat(TEXT("xp.drift"), 1.f) : 1.f;
	return AddXp(FMath::RoundToInt((float)Base * Count * Mul));
}

void UMTAProgressionSubsystem::AddTreeXp(FName TreeId, float Amount)
{
	if (Amount <= 0.f) return;
	const int32 Before = GetTreeMastery(TreeId);
	TreeXp.FindOrAdd(TreeId) += Amount;
	if (GetTreeMastery(TreeId) != Before) { MarkDirty(); }
}

float UMTAProgressionSubsystem::GetTreeXp(FName TreeId) const
{
	const float* V = TreeXp.Find(TreeId);
	return V ? *V : 0.f;
}

int32 UMTAProgressionSubsystem::GetTreeMastery(FName TreeId) const
{
	const float V = GetTreeXp(TreeId);
	int32 M = 0;
	for (int32 I = 1; I < MasteryThresholds.Num(); ++I) { if (V >= MasteryThresholds[I]) M = I; }
	return M;
}

void UMTAProgressionSubsystem::AddPerkPoints(int32 Amount)
{
	PerkPoints = FMath::Max(0, PerkPoints + Amount);
}

// ---------------------------------------------------------------------------------------------
// Skills
// ---------------------------------------------------------------------------------------------

bool UMTAProgressionSubsystem::GetSkill(FName SkillId, FMTASkillDef& OutSkill) const
{
	if (const FMTASkillDef* D = Skills.Find(SkillId)) { OutSkill = *D; return true; }
	return false;
}

int32 UMTAProgressionSubsystem::GetSkillRank(FName SkillId) const
{
	const int32* R = SkillRanks.Find(SkillId);
	return R ? *R : 0;
}

bool UMTAProgressionSubsystem::CanUnlockSkill(FName SkillId, FString& OutReason) const
{
	const FMTASkillDef* D = Skills.Find(SkillId);
	if (!D) { OutReason = TEXT("Unbekannter Skill"); return false; }
	const int32 Rank = GetSkillRank(SkillId);
	if (Rank >= D->MaxRank) { OutReason = TEXT("Hoechster Rang erreicht"); return false; }
	if (Level < D->RequiredLevel) { OutReason = FString::Printf(TEXT("Benoetigt Stufe %d"), D->RequiredLevel); return false; }
	if (GetTreeMastery(D->TreeId) < D->RequiredMastery) { OutReason = FString::Printf(TEXT("Benoetigt Meisterschaft %d"), D->RequiredMastery); return false; }
	for (int32 I = 0; I < D->Requires.Num(); ++I)
	{
		const int32 Need = D->RequiredRanks.IsValidIndex(I) ? D->RequiredRanks[I] : 1;
		if (GetSkillRank(D->Requires[I]) < Need)
		{
			const FMTASkillDef* Req = Skills.Find(D->Requires[I]);
			OutReason = FString::Printf(TEXT("Benoetigt %s (Rang %d)"), Req ? *Req->Name : *D->Requires[I].ToString(), Need);
			return false;
		}
	}
	const int32 Cost = D->CostPerRank.IsValidIndex(Rank) ? D->CostPerRank[Rank] : (D->CostPerRank.Num() > 0 ? D->CostPerRank.Last() : 1);
	if (PerkPoints < Cost) { OutReason = FString::Printf(TEXT("Benoetigt %d Perk-Punkte"), Cost); return false; }
	OutReason.Reset();
	return true;
}

bool UMTAProgressionSubsystem::UnlockSkill(FName SkillId, bool bFree)
{
	const FMTASkillDef* D = Skills.Find(SkillId);
	if (!D) return false;
	const int32 Rank = GetSkillRank(SkillId);
	if (Rank >= D->MaxRank) return false;
	if (!bFree)
	{
		FString Reason;
		if (!CanUnlockSkill(SkillId, Reason)) return false;
		PerkPoints -= D->CostPerRank.IsValidIndex(Rank) ? D->CostPerRank[Rank] : (D->CostPerRank.Num() > 0 ? D->CostPerRank.Last() : 1);
	}
	SkillRanks.Add(SkillId, Rank + 1);
	MarkDirty();
	OnSkillUnlocked.Broadcast(SkillId, Rank + 1);
	return true;
}

bool UMTAProgressionSubsystem::RespecTree(FName TreeId)
{
	int32 Refund = 0;
	TArray<FName> ToClear;
	for (const auto& P : SkillRanks)
	{
		const FMTASkillDef* D = Skills.Find(P.Key);
		if (!D || D->TreeId != TreeId) continue;
		for (int32 R = 0; R < P.Value; ++R) { Refund += D->CostPerRank.IsValidIndex(R) ? D->CostPerRank[R] : 1; }
		ToClear.Add(P.Key);
	}
	if (Refund == 0) return false;
	UGameInstance* GI = GetGameInstance();
	UMTAEconomySubsystem* Econ = GI ? GI->GetSubsystem<UMTAEconomySubsystem>() : nullptr;
	if (Econ)
	{
		const int64 Cost = (int64)(Econ->GetPriceParam(TEXT("respecBase"), 750.f) + Econ->GetPriceParam(TEXT("respecPerPoint"), 120.f) * Refund);
		if (!Econ->TrySpend(Cost, EMTATransactionCategory::Purchase, TEXT("Umschulung ") + TreeId.ToString())) return false;
	}
	for (const FName& Id : ToClear) { SkillRanks.Remove(Id); }
	PerkPoints += Refund;
	MarkDirty();
	return true;
}

// ---------------------------------------------------------------------------------------------
// Modifikatoren
// ---------------------------------------------------------------------------------------------

void UMTAProgressionSubsystem::MarkDirty()
{
	bCacheDirty = true;
	OnModifiersChanged.Broadcast();
}

const FMTAReputationReward* UMTAProgressionSubsystem::FindReward(FName Id) const
{
	return Rewards.FindByPredicate([Id](const FMTAReputationReward& R) { return R.Id == Id; });
}

void UMTAProgressionSubsystem::RebuildCache() const
{
	ModCache.Reset();
	TagCache.Reset();
	auto AddMod = [this](const FMTAStatModifier& M, int32 Times)
	{
		FMTAStatModifier& Agg = ModCache.FindOrAdd(M.Stat);
		if (Agg.Stat.IsNone()) { Agg.Stat = M.Stat; }
		Agg.Add += M.Add * Times;
		Agg.Mul *= FMath::Pow(M.Mul, (float)Times);
	};
	for (const auto& P : SkillRanks)
	{
		const FMTASkillDef* D = Skills.Find(P.Key);
		if (!D || P.Value <= 0) continue;
		for (const FMTAStatModifier& M : D->Effects) { AddMod(M, P.Value); }
		for (const FName& T : D->Tags) { TagCache.Add(T); }
	}
	for (const FName& Id : ActiveRepEffects)
	{
		if (const FMTAReputationReward* R = FindReward(Id))
		{
			for (const FMTAStatModifier& M : R->Effects) { AddMod(M, 1); }
			for (const FName& T : R->Tags) { TagCache.Add(T); }
		}
	}
	for (const auto& P : ExternalMods) { for (const FMTAStatModifier& M : P.Value) { AddMod(M, 1); } }
	for (const auto& P : ExternalTags) { for (const FName& T : P.Value) { TagCache.Add(T); } }
	bCacheDirty = false;
}

FMTAStatModifier UMTAProgressionSubsystem::GetStatModifier(FName Stat) const
{
	if (bCacheDirty) { RebuildCache(); }
	if (const FMTAStatModifier* M = ModCache.Find(Stat)) { return *M; }
	FMTAStatModifier Neutral;
	Neutral.Stat = Stat;
	return Neutral;
}

float UMTAProgressionSubsystem::ApplyStat(FName Stat, float Base) const
{
	return GetStatModifier(Stat).Apply(Base);
}

TArray<FMTAStatModifier> UMTAProgressionSubsystem::GetAllModifiers() const
{
	if (bCacheDirty) { RebuildCache(); }
	TArray<FMTAStatModifier> Result;
	ModCache.GenerateValueArray(Result);
	return Result;
}

bool UMTAProgressionSubsystem::HasTag(FName Tag) const
{
	if (bCacheDirty) { RebuildCache(); }
	return TagCache.Contains(Tag);
}

void UMTAProgressionSubsystem::SetExternalModifiers(FName Source, const TArray<FMTAStatModifier>& Mods, const TArray<FName>& InTags)
{
	ExternalMods.Add(Source, Mods);
	ExternalTags.Add(Source, InTags);
	MarkDirty();
}

void UMTAProgressionSubsystem::ClearExternalModifiers(FName Source)
{
	ExternalMods.Remove(Source);
	ExternalTags.Remove(Source);
	MarkDirty();
}

// ---------------------------------------------------------------------------------------------
// Ruf
// ---------------------------------------------------------------------------------------------

FName UMTAProgressionSubsystem::GetReputationTier(FName Faction) const
{
	const float S = MTAP::GetStoryStanding(GetGameInstance(), Faction.ToString());
	FName Best(TEXT("neutral"));
	float BestMin = -1000.f;
	if (const TArray<TSharedPtr<FJsonValue>>* T = MTAP::Arr(MTAP::Obj(Data, TEXT("reputation")), TEXT("tiers")))
	{
		for (const TSharedPtr<FJsonValue>& V : *T)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			const float Min = (float)MTAP::Num(O, TEXT("min"), 0.0);
			if (S >= Min && Min > BestMin) { BestMin = Min; Best = FName(*MTAP::Str(O, TEXT("id"))); }
		}
	}
	return Best;
}

TArray<FMTAReputationReward> UMTAProgressionSubsystem::GetRewardsForFaction(FName Faction) const
{
	return Rewards.FilterByPredicate([Faction](const FMTAReputationReward& R) { return R.Faction == Faction; });
}

TArray<FMTAReputationReward> UMTAProgressionSubsystem::EvaluateReputationRewards()
{
	TArray<FMTAReputationReward> Granted;
	UGameInstance* GI = GetGameInstance();
	UMTAEconomySubsystem* Econ = GI ? GI->GetSubsystem<UMTAEconomySubsystem>() : nullptr;
	UMTAInventorySubsystem* Inv = GI ? GI->GetSubsystem<UMTAInventorySubsystem>() : nullptr;
	TArray<FName> NewActive;
	for (const FMTAReputationReward& R : Rewards)
	{
		const float S = MTAP::GetStoryStanding(GI, R.Faction.ToString());
		const bool bActive = R.bPenalty ? S <= R.Threshold : S >= R.Threshold;
		if (!bActive) continue;
		NewActive.Add(R.Id);
		if (R.bPenalty || ClaimedRewards.Contains(R.Id)) continue;
		ClaimedRewards.Add(R.Id);
		if (Econ && R.Money > 0) { Econ->AddMoney(R.Money, EMTATransactionCategory::Reward, R.Name); }
		PerkPoints += R.PerkPoints;
		if (Inv)
		{
			for (int32 I = 0; I < R.ItemIds.Num(); ++I) { Inv->AddItem(R.ItemIds[I], R.ItemCounts.IsValidIndex(I) ? R.ItemCounts[I] : 1, EMTAInventoryContainer::Personal); }
		}
		Granted.Add(R);
		OnReputationReward.Broadcast(R);
	}
	if (NewActive != ActiveRepEffects)
	{
		ActiveRepEffects = NewActive;
		MarkDirty();
	}
	return Granted;
}

// ---------------------------------------------------------------------------------------------
// Persistenz
// ---------------------------------------------------------------------------------------------

TSharedRef<FJsonObject> UMTAProgressionSubsystem::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema"), SchemaVersion);
	Root->SetNumberField(TEXT("xp"), (double)Xp);
	Root->SetNumberField(TEXT("level"), Level);
	Root->SetNumberField(TEXT("perkPoints"), PerkPoints);
	TMap<FName, float> Ranks;
	for (const auto& P : SkillRanks) { Ranks.Add(P.Key, (float)P.Value); }
	Root->SetObjectField(TEXT("skills"), MTAP::WriteNumberMap(Ranks));
	Root->SetObjectField(TEXT("treeXp"), MTAP::WriteNumberMap(TreeXp));
	Root->SetArrayField(TEXT("claimedRewards"), MTAP::WriteNameArray(ClaimedRewards));
	Root->SetArrayField(TEXT("activeRep"), MTAP::WriteNameArray(ActiveRepEffects));
	TSharedRef<FJsonObject> Ext = MakeShared<FJsonObject>();
	for (const auto& P : ExternalMods)
	{
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetArrayField(TEXT("mods"), MTAP::WriteModifiers(P.Value));
		const TArray<FName>* Tags = ExternalTags.Find(P.Key);
		E->SetArrayField(TEXT("tags"), MTAP::WriteNameArray(Tags ? *Tags : TArray<FName>()));
		Ext->SetObjectField(P.Key.ToString(), E);
	}
	Root->SetObjectField(TEXT("external"), Ext);
	return Root;
}

bool UMTAProgressionSubsystem::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid()) return false;
	const int32 Schema = (int32)MTAP::Num(Json, TEXT("schema"), 0.0);
	if (Schema < 1 || Schema > SchemaVersion) return false;
	ResetProgression();
	Xp = (int64)MTAP::Num(Json, TEXT("xp"), 0.0);
	Level = FMath::Max(1, (int32)MTAP::Num(Json, TEXT("level"), 1.0));
	PerkPoints = (int32)MTAP::Num(Json, TEXT("perkPoints"), 0.0);
	for (const auto& P : MTAP::ReadNumberMap(MTAP::Obj(Json, TEXT("skills"))))
	{
		if (Skills.Contains(P.Key)) { SkillRanks.Add(P.Key, (int32)P.Value); } // entfernte Skills fallen weg
	}
	TreeXp = MTAP::ReadNumberMap(MTAP::Obj(Json, TEXT("treeXp")));
	ClaimedRewards = MTAP::ReadNameArray(MTAP::Arr(Json, TEXT("claimedRewards")));
	ActiveRepEffects = MTAP::ReadNameArray(MTAP::Arr(Json, TEXT("activeRep")));
	if (const TSharedPtr<FJsonObject> Ext = MTAP::Obj(Json, TEXT("external")))
	{
		for (const auto& P : Ext->Values)
		{
			const TSharedPtr<FJsonObject> E = P.Value->AsObject();
			const FName Source(*P.Key);
			ExternalMods.Add(Source, MTAP::ReadModifiers(MTAP::Arr(E, TEXT("mods"))));
			ExternalTags.Add(Source, MTAP::ReadNameArray(MTAP::Arr(E, TEXT("tags"))));
		}
	}
	MarkDirty();
	return true;
}

FString UMTAProgressionSubsystem::SaveStateToString() const { return MTAP::JsonToString(ToJson()); }
bool UMTAProgressionSubsystem::LoadStateFromString(const FString& Json) { return FromJson(MTAP::JsonFromString(Json)); }

TSharedRef<FJsonObject> UMTAProgressionSubsystem::ExportBundleJson() const
{
	TSharedRef<FJsonObject> Bundle = MakeShared<FJsonObject>();
	Bundle->SetNumberField(TEXT("schema"), BundleSchemaVersion);
	const UGameInstance* GI = GetGameInstance();
	if (const UMTACharacterOriginSubsystem* O = GI ? GI->GetSubsystem<UMTACharacterOriginSubsystem>() : nullptr) { Bundle->SetObjectField(TEXT("origin"), O->ToJson()); }
	if (const UMTAEconomySubsystem* E = GI ? GI->GetSubsystem<UMTAEconomySubsystem>() : nullptr) { Bundle->SetObjectField(TEXT("economy"), E->ToJson()); }
	Bundle->SetObjectField(TEXT("progression"), ToJson());
	if (const UMTAGarageSubsystem* G = GI ? GI->GetSubsystem<UMTAGarageSubsystem>() : nullptr) { Bundle->SetObjectField(TEXT("garage"), G->ToJson()); }
	if (const UMTAInventorySubsystem* I = GI ? GI->GetSubsystem<UMTAInventorySubsystem>() : nullptr) { Bundle->SetObjectField(TEXT("inventory"), I->ToJson()); }
	return Bundle;
}

bool UMTAProgressionSubsystem::ImportBundleJson(const TSharedPtr<FJsonObject>& Bundle)
{
	if (!Bundle.IsValid()) return false;
	const int32 Schema = (int32)MTAP::Num(Bundle, TEXT("schema"), 0.0);
	if (Schema < 1 || Schema > BundleSchemaVersion)
	{
		UE_LOG(LogTemp, Error, TEXT("MTA6: Fortschritts-Bundle Schema %d nicht unterstuetzt"), Schema);
		return false;
	}
	UGameInstance* GI = GetGameInstance();
	bool bOk = true;
	// Reihenfolge: Herkunft -> Fortschritt (Modifikatoren) -> Wirtschaft -> Garage -> Inventar
	if (UMTACharacterOriginSubsystem* O = GI ? GI->GetSubsystem<UMTACharacterOriginSubsystem>() : nullptr) { if (const TSharedPtr<FJsonObject> J = MTAP::Obj(Bundle, TEXT("origin"))) bOk &= O->FromJson(J); }
	if (const TSharedPtr<FJsonObject> J = MTAP::Obj(Bundle, TEXT("progression"))) bOk &= FromJson(J);
	if (UMTAEconomySubsystem* E = GI ? GI->GetSubsystem<UMTAEconomySubsystem>() : nullptr) { if (const TSharedPtr<FJsonObject> J = MTAP::Obj(Bundle, TEXT("economy"))) bOk &= E->FromJson(J); }
	if (UMTAGarageSubsystem* G = GI ? GI->GetSubsystem<UMTAGarageSubsystem>() : nullptr) { if (const TSharedPtr<FJsonObject> J = MTAP::Obj(Bundle, TEXT("garage"))) bOk &= G->FromJson(J); }
	if (UMTAInventorySubsystem* I = GI ? GI->GetSubsystem<UMTAInventorySubsystem>() : nullptr) { if (const TSharedPtr<FJsonObject> J = MTAP::Obj(Bundle, TEXT("inventory"))) bOk &= I->FromJson(J); }
	return bOk;
}

FString UMTAProgressionSubsystem::ExportProgressionBundle() const { return MTAP::JsonToString(ExportBundleJson()); }
bool UMTAProgressionSubsystem::ImportProgressionBundle(const FString& Json) { return ImportBundleJson(MTAP::JsonFromString(Json)); }
