#include "MTAHeatSubsystem.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------------------------
// Hilfen
// ---------------------------------------------------------------------------------------------

int32 UMTAHeatSubsystem::HeatToStars(float Heat)
{
	// Godot: int(ceil(heat - 0.001))
	return FMath::Clamp(FMath::CeilToInt(Heat - 0.001f), 0, 5);
}

float UMTAHeatSubsystem::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.f;
}

FMTAPlayerHeat& UMTAHeatSubsystem::FindOrAddPlayer(int32 PlayerIndex)
{
	return Players.FindOrAdd(FMath::Max(0, PlayerIndex));
}

const FMTAPlayerHeat* UMTAHeatSubsystem::FindPlayer(int32 PlayerIndex) const
{
	return Players.Find(FMath::Max(0, PlayerIndex));
}

void UMTAHeatSubsystem::BroadcastIfNeeded(int32 PlayerIndex, FMTAPlayerHeat& State, bool bForce)
{
	// Delegates beziehen sich auf den lokalen Spieler (Index 0).
	if (PlayerIndex != 0)
	{
		return;
	}

	const int32 Stars = HeatToStars(State.GlobalHeat);
	const bool bStarsChanged = Stars != State.LastBroadcastStars;
	const bool bReachedZero = State.GlobalHeat <= 0.f && State.LastBroadcastHeat > 0.f;
	const bool bBigStep = FMath::Abs(State.GlobalHeat - State.LastBroadcastHeat) >= 0.05f;

	if (bForce || bStarsChanged || bReachedZero || bBigStep)
	{
		State.LastBroadcastHeat = State.GlobalHeat;
		OnHeatChanged.Broadcast(State.GlobalHeat);
	}
	if (bStarsChanged)
	{
		State.LastBroadcastStars = Stars;
		OnWantedLevelChanged.Broadcast(Stars);
	}
}

// ---------------------------------------------------------------------------------------------
// Alt-API (Spieler 0)
// ---------------------------------------------------------------------------------------------

void UMTAHeatSubsystem::AddHeat(FName DistrictId, float Amount)
{
	AddPlayerHeat(0, Amount, DistrictId);
}

float UMTAHeatSubsystem::GetHeat(FName DistrictId) const
{
	// Kompatibilitaet zu AMTA6GameMode::GetHeat/SetHeat: liefert die globale Fahndung von Spieler 0,
	// damit SetHeat (AddHeat mit Differenz) konsistent bleibt. Bezirks-Ruf: GetDistrictHeat.
	(void)DistrictId;
	return GetGlobalHeat(0);
}

// ---------------------------------------------------------------------------------------------
// Pro Spieler
// ---------------------------------------------------------------------------------------------

void UMTAHeatSubsystem::AddPlayerHeat(int32 PlayerIndex, float Amount, FName DistrictId)
{
	FMTAPlayerHeat& State = FindOrAddPlayer(PlayerIndex);
	const FName District = DistrictId.IsNone() ? State.CurrentDistrict : DistrictId;

	State.GlobalHeat = FMath::Clamp(State.GlobalHeat + Amount, 0.f, MaxHeat);
	if (!District.IsNone())
	{
		float& Value = State.DistrictHeat.FindOrAdd(District);
		Value = FMath::Clamp(Value + Amount, 0.f, MaxHeat);
	}
	if (Amount > 0.f)
	{
		State.LastCrimeTime = Now();
	}
	BroadcastIfNeeded(PlayerIndex, State, /*bForce*/ true);
}

void UMTAHeatSubsystem::SetGlobalHeat(int32 PlayerIndex, float NewHeat)
{
	FMTAPlayerHeat& State = FindOrAddPlayer(PlayerIndex);
	State.GlobalHeat = FMath::Clamp(NewHeat, 0.f, MaxHeat);
	BroadcastIfNeeded(PlayerIndex, State, /*bForce*/ true);
}

float UMTAHeatSubsystem::GetGlobalHeat(int32 PlayerIndex) const
{
	const FMTAPlayerHeat* State = FindPlayer(PlayerIndex);
	return State ? State->GlobalHeat : 0.f;
}

float UMTAHeatSubsystem::GetDistrictHeat(int32 PlayerIndex, FName DistrictId) const
{
	const FMTAPlayerHeat* State = FindPlayer(PlayerIndex);
	if (!State)
	{
		return 0.f;
	}
	const float* Value = State->DistrictHeat.Find(DistrictId);
	return Value ? *Value : 0.f;
}

int32 UMTAHeatSubsystem::GetWantedLevel(int32 PlayerIndex) const
{
	return HeatToStars(GetGlobalHeat(PlayerIndex));
}

float UMTAHeatSubsystem::GetSecondsSinceLastCrime(int32 PlayerIndex) const
{
	const FMTAPlayerHeat* State = FindPlayer(PlayerIndex);
	return State ? Now() - State->LastCrimeTime : TNumericLimits<float>::Max();
}

FName UMTAHeatSubsystem::GetCurrentDistrict(int32 PlayerIndex) const
{
	const FMTAPlayerHeat* State = FindPlayer(PlayerIndex);
	return State ? State->CurrentDistrict : NAME_None;
}

void UMTAHeatSubsystem::TickCooldown(float DeltaSeconds, bool bPoliceNearby, int32 PlayerIndex)
{
	FMTAPlayerHeat& State = FindOrAddPlayer(PlayerIndex);
	const bool bQuietLongEnough = (Now() - State.LastCrimeTime) > CooldownDelay;
	if (!bQuietLongEnough)
	{
		return;
	}

	if (State.GlobalHeat > 0.f && !bPoliceNearby)
	{
		State.GlobalHeat = FMath::Max(0.f, State.GlobalHeat - CooldownRate * DeltaSeconds);
		BroadcastIfNeeded(PlayerIndex, State, /*bForce*/ false);
	}

	// Bezirks-Ruf kuehlt immer, aber langsam. Leere Eintraege entfernen.
	for (auto It = State.DistrictHeat.CreateIterator(); It; ++It)
	{
		It.Value() = FMath::Max(0.f, It.Value() - DistrictCooldownRate * DeltaSeconds);
		if (It.Value() <= 0.f)
		{
			It.RemoveCurrent();
		}
	}
}

void UMTAHeatSubsystem::NotifyPlayerDistrict(int32 PlayerIndex, FName DistrictId)
{
	FMTAPlayerHeat& State = FindOrAddPlayer(PlayerIndex);
	if (State.CurrentDistrict == DistrictId)
	{
		return;
	}
	State.CurrentDistrict = DistrictId;

	// Wiedererkennung: wer hier beruechtigt ist, wird beim Betreten erneut gesucht.
	const float* Known = State.DistrictHeat.Find(DistrictId);
	if (Known && *Known >= RecognitionThreshold && State.GlobalHeat <= 0.f)
	{
		State.GlobalHeat = FMath::Min(RecognitionHeat, MaxHeat);
		State.LastCrimeTime = Now();
		BroadcastIfNeeded(PlayerIndex, State, /*bForce*/ true);
	}
}

void UMTAHeatSubsystem::ClearHeat(int32 PlayerIndex)
{
	FMTAPlayerHeat& State = FindOrAddPlayer(PlayerIndex);
	State.GlobalHeat = 0.f;
	for (auto& Pair : State.DistrictHeat)
	{
		Pair.Value *= DistrictKeepOnArrest;
	}
	BroadcastIfNeeded(PlayerIndex, State, /*bForce*/ true);
}

FMTAPlayerHeat UMTAHeatSubsystem::GetPlayerHeatState(int32 PlayerIndex) const
{
	const FMTAPlayerHeat* State = FindPlayer(PlayerIndex);
	return State ? *State : FMTAPlayerHeat();
}
