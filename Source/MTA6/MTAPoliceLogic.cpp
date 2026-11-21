// Polizei-Teil von AMTAWorldAIManager (Modul M6). Verhaltensprotokoll: MTAPoliceTypes.h.
#include "MTAWorldAIManager.h"
#include "MTAHeatSubsystem.h"

bool AMTAWorldAIManager::IsOnRoad(const FVector2f& P)
{
	const float Limit = MTACity::Half + MTACity::RoadHalfWidth;
	if (FMath::Abs(P.X) > Limit || FMath::Abs(P.Y) > Limit)
	{
		return false;
	}
	const float DX = FMath::Abs(P.X - MTACity::RoadCenter(MTACity::NearestLine(P.X)));
	const float DY = FMath::Abs(P.Y - MTACity::RoadCenter(MTACity::NearestLine(P.Y)));
	return DX < MTACity::RoadHalfWidth || DY < MTACity::RoadHalfWidth;
}

bool AMTAWorldAIManager::IsSirenPhaseRed() const
{
	return (FMath::FloorToInt(SimTime * Police.SirenTogglesPerSecond) % 2) == 0;
}

float AMTAWorldAIManager::GetPoliceTargetSpeed(float Heat) const
{
	return Police.ChaseSpeedBase + Heat * Police.ChaseSpeedPerHeat;
}

int32 AMTAWorldAIManager::GetActivePoliceCount() const
{
	int32 Count = 0;
	for (int32 I = FirstPoliceSlot; I < Vehicles.Num(); ++I)
	{
		Count += Vehicles.bActive[I] ? 1 : 0;
	}
	return Count;
}

float AMTAWorldAIManager::GetArrestProgress() const
{
	return Police.ArrestTime > 0.f ? FMath::Clamp(ArrestTimer / Police.ArrestTime, 0.f, 1.f) : 0.f;
}

void AMTAWorldAIManager::DespawnPolice(int32 I)
{
	Vehicles.bActive[I] = 0;
	Vehicles.PoliceState[I] = EMTAPoliceState::Inactive;
	Vehicles.Speed[I] = 0.f;
	Vehicles.Stun[I] = Vehicles.Wait[I] = Vehicles.Ghost[I] = 0.f;
}

void AMTAWorldAIManager::DespawnAllPolice()
{
	for (int32 I = FirstPoliceSlot; I < Vehicles.Num(); ++I)
	{
		DespawnPolice(I);
	}
	ArrestTimer = 0.f;
}

bool AMTAWorldAIManager::TrySpawnPolice()
{
	int32 Slot = INDEX_NONE;
	for (int32 I = FirstPoliceSlot; I < Vehicles.Num(); ++I)
	{
		if (!Vehicles.bActive[I])
		{
			Slot = I;
			break;
		}
	}
	if (Slot == INDEX_NONE || !bHasPlayer)
	{
		return false;
	}

	const float MinSq = FMath::Square(Police.SpawnMinDistance);
	const float MaxSq = FMath::Square(Police.SpawnMaxDistance);
	const int32 Attempts = FMath::Max(1, Police.SpawnAttempts);
	for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
	{
		SpawnVehicleAt(Slot, EMTAAgentKind::Police); // setzt bActive = 1, Chase, Sim-Position
		const float DSq = FVector2f::DistSquared(Vehicles.SimLoc[Slot], PlayerLoc2D);
		// Erste Haelfte der Versuche: nur ausserhalb des Sichtkegels (kein sichtbares Pop-in).
		const bool bViewOk = !Police.bPreferSpawnOutOfView || Attempt >= Attempts / 2 || IsOutOfView(Vehicles.SimLoc[Slot]);
		if (DSq > MinSq && DSq < MaxSq && bViewOk)
		{
			Vehicles.Speed[Slot] = Police.InitialSpeed;
			return true;
		}
	}
	DespawnPolice(Slot);
	return false;
}

void AMTAWorldAIManager::SnapPoliceToLane(int32 I)
{
	const FVector2f L = Vehicles.SimLoc[I];
	const FVector2f D = Vehicles.SimDir[I];
	const float Lim = MTACity::Half - 1.f;
	if (FMath::Abs(D.X) >= FMath::Abs(D.Y))
	{
		Vehicles.Axis[I] = 0;
		Vehicles.Sign[I] = static_cast<int8>(D.X >= 0.f ? 1 : -1);
		Vehicles.Line[I] = MTACity::NearestLine(L.Y);
		Vehicles.Pos[I] = FMath::Clamp(L.X, -Lim, Lim);
	}
	else
	{
		Vehicles.Axis[I] = 1;
		Vehicles.Sign[I] = static_cast<int8>(D.Y >= 0.f ? 1 : -1);
		Vehicles.Line[I] = MTACity::NearestLine(L.X);
		Vehicles.Pos[I] = FMath::Clamp(L.Y, -Lim, Lim);
	}
	Vehicles.Wait[I] = 0.f;
	SetVehicleOffset(I);
	ComputeVehicleSim(I); // Darstellung glaettet den Sprung
}

void AMTAWorldAIManager::UpdatePoliceUnit(int32 I, float Dt, float Heat)
{
	const bool bWanted = Heat > 0.f;
	EMTAPoliceState State = Vehicles.PoliceState[I];
	const float Dist = bHasPlayer ? FVector2f::Distance(Vehicles.SimLoc[I], PlayerLoc2D) : TNumericLimits<float>::Max();

	// --- Uebergaenge -----------------------------------------------------------------------------
	if (!bWanted && State != EMTAPoliceState::Calm)
	{
		if (State == EMTAPoliceState::Ram)
		{
			SnapPoliceToLane(I);
		}
		State = EMTAPoliceState::Calm;
	}
	else if (bWanted && State == EMTAPoliceState::Calm)
	{
		State = EMTAPoliceState::Chase;
	}

	if (State == EMTAPoliceState::Chase && bWanted && Police.bEnableRam && bHasPlayer
		&& Dist < Police.RamEnterDistance && IsOnRoad(PlayerLoc2D) && IsOnRoad(Vehicles.SimLoc[I]))
	{
		State = EMTAPoliceState::Ram; // SimLoc/SimDir werden ab jetzt frei gefuehrt
	}
	else if (State == EMTAPoliceState::Ram
		&& (!bHasPlayer || Dist > Police.RamExitDistance || !IsOnRoad(Vehicles.SimLoc[I])))
	{
		SnapPoliceToLane(I);
		State = EMTAPoliceState::Chase;
	}
	Vehicles.PoliceState[I] = State;

	// --- Bewegung --------------------------------------------------------------------------------
	const float Target = State == EMTAPoliceState::Calm ? Police.CalmSpeed : GetPoliceTargetSpeed(Heat);
	if (State == EMTAPoliceState::Ram)
	{
		if (Vehicles.Stun[I] > 0.f)
		{
			Vehicles.Stun[I] -= Dt;
			Vehicles.Speed[I] = MTAAI::MoveTowards(Vehicles.Speed[I], 0.f, Traffic.StunBraking * Dt);
		}
		else
		{
			const float RamTarget = Target * Police.RamSpeedFactor;
			const float Rate = RamTarget > Vehicles.Speed[I] ? Traffic.Acceleration : Traffic.Braking;
			Vehicles.Speed[I] = MTAAI::MoveTowards(Vehicles.Speed[I], RamTarget, Rate * Dt);
		}

		// Begrenzte Drehrate in Richtung Spieler.
		const FVector2f ToPlayer = (PlayerLoc2D - Vehicles.SimLoc[I]).GetSafeNormal();
		if (!ToPlayer.IsNearlyZero())
		{
			const float CurYaw = MTAAI::YawFromDir(Vehicles.SimDir[I]);
			const float DesYaw = MTAAI::YawFromDir(ToPlayer);
			const float MaxTurn = Police.RamTurnRateDeg * Dt;
			const float Delta = FMath::Clamp(FMath::FindDeltaAngleDegrees(CurYaw, DesYaw), -MaxTurn, MaxTurn);
			Vehicles.SimDir[I] = MTAAI::DirFromYaw(CurYaw + Delta);
		}
		Vehicles.SimLoc[I] += Vehicles.SimDir[I] * (Vehicles.Speed[I] * Dt);
	}
	else
	{
		UpdateLaneSpeed(I, Dt, Target);
		MoveVehicleAlongLane(I, Dt); // gierige Kreuzungswahl im Zustand Chase
	}
}

void AMTAWorldAIManager::UpdatePoliceDispatch(float Dt)
{
	const float Heat = CachedHeat;
	const int32 Stars = FMath::Clamp(FMath::CeilToInt(Heat - 0.001f), 0, 5);
	int32 Active = GetActivePoliceCount();
	const int32 Desired = Heat <= 0.f ? 0 : FMath::Min(NumPoliceSlots, Police.BaseUnits + Stars * Police.UnitsPerStar);

	if (bHasPlayer && Active < Desired && Rng.FRand() < Dt * Police.SpawnRatePerSecond)
	{
		if (TrySpawnPolice())
		{
			++Active;
		}
	}

	bool bNear = false;
	bool bClose = false;
	const float NearSq = FMath::Square(Police.NearPoliceRadius);
	const float CloseSq = FMath::Square(Police.ArrestRadius);
	const float DespawnSq = FMath::Square(Police.DespawnDistanceWhenCalm);
	for (int32 I = FirstPoliceSlot; I < Vehicles.Num(); ++I)
	{
		if (!Vehicles.bActive[I])
		{
			continue;
		}
		if (!bHasPlayer)
		{
			if (Heat <= 0.f)
			{
				DespawnPolice(I);
			}
			continue;
		}
		const float DSq = FVector2f::DistSquared(Vehicles.SimLoc[I], PlayerLoc2D);
		bNear |= DSq < NearSq;
		bClose |= DSq < CloseSq;

		// Abzug: bei Fahndung 0 oder Ueberzahl, aber nur weit weg (kein sichtbares Verschwinden).
		if (DSq > DespawnSq && (Heat <= 0.f || Active > Desired))
		{
			DespawnPolice(I);
			--Active;
		}
	}

	if (HeatSubsystem)
	{
		HeatSubsystem->TickCooldown(Dt, bNear, PlayerIndex);
		CachedHeat = HeatSubsystem->GetGlobalHeat(PlayerIndex);
	}

	// Festnahme: Stillstand nahe einer Einheit ueber ArrestTime.
	if (bHasPlayer && Heat > 0.f && bClose && PlayerSpeed < Police.ArrestMaxPlayerSpeed)
	{
		ArrestTimer += Dt;
		if (ArrestTimer > Police.ArrestTime)
		{
			Arrest();
		}
	}
	else
	{
		ArrestTimer = 0.f;
	}
}

void AMTAWorldAIManager::Arrest()
{
	if (HeatSubsystem)
	{
		HeatSubsystem->ClearHeat(PlayerIndex);
	}
	CachedHeat = 0.f;
	DespawnAllPolice();
	ArrestTimer = 0.f;
	OnArrested.Broadcast();
}
