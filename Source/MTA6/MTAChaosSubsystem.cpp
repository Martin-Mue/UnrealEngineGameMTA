#include "MTAChaosSubsystem.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"
#include "MTAChaosWitness.h"
#include "MTAChaosAegisCamera.h"
#include "MTAHeatSubsystem.h"
#include "MTAStealthComponent.h"

// ---------------------------------------------------------------------------------------
// Lebenszyklus
// ---------------------------------------------------------------------------------------

void UMTAChaosSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Crimes.Reserve(MaxStoredCrimes);
}

void UMTAChaosSubsystem::Deinitialize()
{
	// Gebundene Treffer-Delegates loesen, solange die Fahrzeuge noch leben.
	TArray<FObjectKey> Keys;
	WatchedVehicles.GetKeys(Keys);
	for (const FObjectKey& Key : Keys)
	{
		if (AActor* Vehicle = Cast<AActor>(Key.ResolveObjectPtr()))
		{
			Vehicle->OnActorHit.RemoveDynamic(this, &UMTAChaosSubsystem::HandleWatchedVehicleHit);
		}
	}
	WatchedVehicles.Reset();
	Witnesses.Reset();
	Cameras.Reset();
	PendingReports.Reset();
	HitAndRunWatches.Reset();
	Crimes.Reset();
	Super::Deinitialize();
}

bool UMTAChaosSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMTAChaosSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMTAChaosSubsystem, STATGROUP_Tickables);
}

float UMTAChaosSubsystem::GetNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.f;
}

void UMTAChaosSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const float Now = GetNow();
	ProcessPendingReports(Now);
	ProcessHitAndRun(DeltaTime, Now);

	// Abgelaufene Treffer-Abklingzeiten entfernen (kleine Map, guenstig).
	for (auto It = HitCooldowns.CreateIterator(); It; ++It)
	{
		if (It.Value() < Now)
		{
			It.RemoveCurrent();
		}
	}
}

// ---------------------------------------------------------------------------------------
// Regeln
// ---------------------------------------------------------------------------------------

FMTACrimeRule UMTAChaosSubsystem::MakeDefaultRule(EMTACrimeType Type)
{
	FMTACrimeRule R;
	R.Type = Type;
	// Werte: Heat auf 0..5-Skala, Ruf -100..100, Aegis 0..100. Startwerte fuer Playtests.
	switch (Type)
	{
	case EMTACrimeType::Speeding:
		R.Heat = 0.25f; R.ReputationDelta = -0.5f; R.AegisDelta = 1.f; R.NoticeRadius = 1500.f; R.Loudness = 0.2f;
		R.ReportDelayMin = 20.f; R.ReportDelayMax = 40.f; R.NewsKey = TEXT("news_speeding");
		break;
	case EMTACrimeType::RecklessDriving:
		R.Heat = 0.4f; R.ReputationDelta = -1.f; R.AegisDelta = 1.5f; R.NoticeRadius = 2500.f; R.Loudness = 0.4f;
		R.NewsKey = TEXT("news_reckless");
		break;
	case EMTACrimeType::Collision:
		R.Heat = 0.5f; R.ReputationDelta = -1.f; R.AegisDelta = 2.f; R.NoticeRadius = 3000.f; R.Loudness = 0.8f;
		R.ReportDelayMin = 6.f; R.ReportDelayMax = 18.f; R.NewsKey = TEXT("news_crash");
		break;
	case EMTACrimeType::HitAndRun:
		R.Heat = 1.f; R.ReputationDelta = -4.f; R.AegisDelta = 5.f; R.NoticeRadius = 0.f; R.Loudness = 0.f;
		R.CameraAttributionWindow = 180.f; R.NewsKey = TEXT("news_hit_and_run");
		break;
	case EMTACrimeType::PropertyDamage:
		R.Heat = 0.3f; R.ReputationDelta = -1.f; R.AegisDelta = 1.f; R.NoticeRadius = 2000.f; R.Loudness = 0.6f;
		R.NewsKey = TEXT("news_property");
		break;
	case EMTACrimeType::PedestrianHit:
		R.Heat = 1.5f; R.ReputationDelta = -6.f; R.AegisDelta = 6.f; R.NoticeRadius = 3500.f; R.Loudness = 0.9f;
		R.ReportDelayMin = 3.f; R.ReportDelayMax = 10.f; R.NewsKey = TEXT("news_pedestrian");
		break;
	case EMTACrimeType::VehicleTheft:
		R.Heat = 0.8f; R.ReputationDelta = -2.f; R.AegisDelta = 4.f; R.NoticeRadius = 2000.f; R.Loudness = 0.3f;
		R.ReportDelayMin = 10.f; R.ReportDelayMax = 30.f; R.CameraAttributionWindow = 600.f; R.NewsKey = TEXT("news_theft");
		break;
	case EMTACrimeType::Carjacking:
		R.Heat = 1.5f; R.ReputationDelta = -5.f; R.AegisDelta = 6.f; R.NoticeRadius = 3000.f; R.Loudness = 0.8f;
		R.ReportDelayMin = 4.f; R.ReportDelayMax = 12.f; R.CameraAttributionWindow = 600.f; R.NewsKey = TEXT("news_carjacking");
		break;
	case EMTACrimeType::BreakIn:
		R.Heat = 0.6f; R.ReputationDelta = -2.f; R.AegisDelta = 3.f; R.NoticeRadius = 1500.f; R.Loudness = 0.7f;
		R.NewsKey = TEXT("news_breakin");
		break;
	case EMTACrimeType::Assault:
		R.Heat = 1.2f; R.ReputationDelta = -5.f; R.AegisDelta = 4.f; R.NoticeRadius = 2500.f; R.Loudness = 0.7f;
		R.NewsKey = TEXT("news_assault");
		break;
	case EMTACrimeType::Trespass:
		R.Heat = 0.3f; R.ReputationDelta = -1.f; R.AegisDelta = 2.f; R.NoticeRadius = 1500.f; R.Loudness = 0.1f;
		break;
	case EMTACrimeType::EvidenceTampering:
		R.Heat = 0.5f; R.ReputationDelta = -1.f; R.AegisDelta = 2.f; R.NoticeRadius = 1200.f; R.Loudness = 0.1f;
		break;
	case EMTACrimeType::CameraTampering:
		R.Heat = 0.6f; R.ReputationDelta = 1.f; R.AegisDelta = 5.f; R.NoticeRadius = 1500.f; R.Loudness = 0.4f;
		R.NewsKey = TEXT("news_aegis_sabotage");
		break;
	case EMTACrimeType::PoliceContact:
		R.Heat = 1.f; R.bImmediateHeat = true; R.ReputationDelta = -2.f; R.AegisDelta = 3.f; R.NoticeRadius = 0.f;
		break;
	default:
		break;
	}
	return R;
}

FMTACrimeRule UMTAChaosSubsystem::GetRule(EMTACrimeType Type) const
{
	if (RuleTable)
	{
		const UEnum* Enum = StaticEnum<EMTACrimeType>();
		const FName RowName(*Enum->GetNameStringByValue(static_cast<int64>(Type)));
		if (const FMTACrimeRule* Row = RuleTable->FindRow<FMTACrimeRule>(RowName, TEXT("MTAChaos"), false))
		{
			FMTACrimeRule Copy = *Row;
			Copy.Type = Type;
			return Copy;
		}
	}
	return MakeDefaultRule(Type);
}

// ---------------------------------------------------------------------------------------
// Verbrechen
// ---------------------------------------------------------------------------------------

FName UMTAChaosSubsystem::ResolveDistrict(const FVector& Location) const
{
	if (DistrictResolver)
	{
		const FName Resolved = DistrictResolver(Location);
		if (!Resolved.IsNone())
		{
			return Resolved;
		}
	}
	return DefaultDistrictId;
}

int32 UMTAChaosSubsystem::ReportSimpleCrime(EMTACrimeType Type, AActor* Perpetrator, FVector Location, AActor* Victim, float Severity)
{
	FMTACrimeEvent Crime;
	Crime.Type = Type;
	Crime.Perpetrator = Perpetrator;
	Crime.Victim = Victim;
	Crime.Location = Location;
	Crime.Severity = Severity;
	return ReportCrime(Crime);
}

int32 UMTAChaosSubsystem::ReportCrime(FMTACrimeEvent Crime)
{
	if (Crime.Type == EMTACrimeType::None)
	{
		return INDEX_NONE;
	}

	Crime.CrimeId = NextCrimeId++;
	Crime.WorldTime = GetNow();
	if (Crime.DistrictId.IsNone())
	{
		Crime.DistrictId = ResolveDistrict(Crime.Location);
	}

	// Fahrzeug des Taeters ermitteln: Figur sitzt (angehaengt) in einem registrierten Fahrzeug.
	AActor* Perp = Crime.Perpetrator.Get();
	if (Perp && !Crime.PerpetratorVehicle.IsValid())
	{
		if (HasVehicleRecord(Perp))
		{
			Crime.PerpetratorVehicle = Perp;
		}
		else if (AActor* Parent = Perp->GetAttachParentActor())
		{
			if (HasVehicleRecord(Parent))
			{
				Crime.PerpetratorVehicle = Parent;
			}
		}
	}
	if (AActor* Vehicle = Crime.PerpetratorVehicle.Get())
	{
		Crime.PlateId = GetOrCreateVehicleRecord(Vehicle).PlateId;
	}
	if (const UMTAStealthComponent* Stealth = UMTAStealthComponent::FindStealthComponent(Perp))
	{
		Crime.OutfitId = Stealth->GetOutfitId();
	}

	const FMTACrimeRule Rule = GetRule(Crime.Type);
	const int32 CrimeId = Crime.CrimeId;

	if (Crimes.Num() >= FMath::Max(1, MaxStoredCrimes))
	{
		Crimes.RemoveAt(0, 1, EAllowShrinking::No);
	}
	TSharedRef<FMTACrimeEvent> Stored = MakeShared<FMTACrimeEvent>(Crime);
	Crimes.Add(Stored);

	OnCrimeCommitted.Broadcast(*Stored);

	if (Rule.bImmediateHeat)
	{
		MarkKnown(Stored, EMTACrimeKnowledge::Reported);
	}

	DistributeToWitnesses(CrimeId, Rule);
	return CrimeId;
}

TSharedPtr<FMTACrimeEvent> UMTAChaosSubsystem::FindCrimeMutable(int32 CrimeId) const
{
	for (int32 i = Crimes.Num() - 1; i >= 0; --i)
	{
		if (Crimes[i]->CrimeId == CrimeId)
		{
			return Crimes[i];
		}
	}
	return nullptr;
}

bool UMTAChaosSubsystem::GetCrime(int32 CrimeId, FMTACrimeEvent& OutCrime) const
{
	if (const TSharedPtr<FMTACrimeEvent> Crime = FindCrimeMutable(CrimeId))
	{
		OutCrime = *Crime;
		return true;
	}
	return false;
}

TArray<FMTACrimeEvent> UMTAChaosSubsystem::GetRecentCrimes(float MaxAgeSeconds) const
{
	TArray<FMTACrimeEvent> Result;
	const float Now = GetNow();
	for (int32 i = Crimes.Num() - 1; i >= 0; --i)
	{
		if (Now - Crimes[i]->WorldTime > MaxAgeSeconds)
		{
			break;
		}
		Result.Add(*Crimes[i]);
	}
	return Result;
}

bool UMTAChaosSubsystem::IsSubjectOfCrime(const FMTACrimeEvent& Crime, const AActor* Subject)
{
	if (!Subject)
	{
		return false;
	}
	const AActor* Perp = Crime.Perpetrator.Get();
	const AActor* PerpVehicle = Crime.PerpetratorVehicle.Get();
	if (Subject == Perp || (PerpVehicle && Subject == PerpVehicle))
	{
		return true;
	}
	// Figur sitzt im Fahrzeug bzw. Fahrzeug transportiert die Figur.
	if (Perp && Perp->GetAttachParentActor() == Subject)
	{
		return true;
	}
	const AActor* SubjectParent = Subject->GetAttachParentActor();
	return SubjectParent != nullptr && (SubjectParent == PerpVehicle || SubjectParent == Perp);
}

void UMTAChaosSubsystem::MarkKnown(const TSharedPtr<FMTACrimeEvent>& Crime, EMTACrimeKnowledge NewKnowledge)
{
	if (!Crime.IsValid())
	{
		return;
	}
	const bool bFirst = Crime->Knowledge < EMTACrimeKnowledge::Reported;
	if (NewKnowledge > Crime->Knowledge)
	{
		Crime->Knowledge = NewKnowledge;
	}
	if (!bFirst)
	{
		return;
	}
	const FMTACrimeRule Rule = GetRule(Crime->Type);
	ApplyHeat(*Crime, 1.f);
	ApplyReputation(*Crime, Rule);
	OnCrimeReported.Broadcast(*Crime);
}

void UMTAChaosSubsystem::DistributeToWitnesses(int32 CrimeId, const FMTACrimeRule& Rule)
{
	const float Now = GetNow();

	// Kopie der Liste: Zeugen koennen sich waehrend der Auswertung abmelden.
	const TArray<TWeakObjectPtr<UMTAWitnessComponent>> Snapshot = Witnesses;
	for (const TWeakObjectPtr<UMTAWitnessComponent>& WeakWitness : Snapshot)
	{
		UMTAWitnessComponent* Witness = WeakWitness.Get();
		const TSharedPtr<FMTACrimeEvent> Crime = FindCrimeMutable(CrimeId);
		if (!Crime.IsValid())
		{
			return;
		}
		if (!Witness || !Witness->GetOwner())
		{
			continue;
		}
		const bool bIsVictim = Crime->Victim.IsValid() && Witness->GetOwner() == Crime->Victim.Get();
		if (!bIsVictim && Rule.NoticeRadius <= 0.f)
		{
			continue;
		}

		float Delay = 0.f;
		if (!Witness->ConsiderCrime(*Crime, Rule, bIsVictim, Delay))
		{
			continue;
		}

		Crime->WitnessCount++;
		if (Crime->Knowledge == EMTACrimeKnowledge::Unknown)
		{
			Crime->Knowledge = EMTACrimeKnowledge::Witnessed;
		}
		// Delay < 0: Zeuge hat gesehen, entscheidet sich aber gegen eine Meldung.
		if (Witness->bCanReport && Delay >= 0.f)
		{
			FPendingReport& Pending = PendingReports.AddDefaulted_GetRef();
			Pending.CrimeId = CrimeId;
			Pending.Witness = Witness;
			Pending.ReportTime = Now + Delay;
		}
		OnCrimeWitnessed.Broadcast(*Crime, Witness);
	}
}

void UMTAChaosSubsystem::ProcessPendingReports(float Now)
{
	TArray<FPendingReport> Due;
	for (int32 i = PendingReports.Num() - 1; i >= 0; --i)
	{
		if (PendingReports[i].ReportTime <= Now)
		{
			Due.Add(PendingReports[i]);
			PendingReports.RemoveAtSwap(i, 1, EAllowShrinking::No);
		}
	}
	for (const FPendingReport& Report : Due)
	{
		UMTAWitnessComponent* Witness = Report.Witness.Get();
		if (Witness && Witness->CanReportNow())
		{
			ConfirmReport(Report.CrimeId, Witness);
		}
	}
}

void UMTAChaosSubsystem::ConfirmReport(int32 CrimeId, UMTAWitnessComponent* Witness)
{
	const TSharedPtr<FMTACrimeEvent> Crime = FindCrimeMutable(CrimeId);
	if (!Crime.IsValid() || !Witness)
	{
		return;
	}
	Witness->MarkReported(CrimeId);

	const bool bFirstReport = Crime->Knowledge < EMTACrimeKnowledge::Reported;
	if (bFirstReport)
	{
		// Zeugenbeschreibung: Behoerden kennen jetzt das Outfit.
		if (!Crime->OutfitId.IsNone() && Witness->RemembersOutfit(CrimeId))
		{
			KnownOutfitId = Crime->OutfitId;
		}
		MarkKnown(Crime, EMTACrimeKnowledge::Reported);
	}
	else
	{
		ApplyHeat(*Crime, ExtraWitnessHeatFraction);
	}

	// Gemeldete Fahrzeuge mit erinnertem Kennzeichen werden "heiss".
	if (AActor* Vehicle = Crime->PerpetratorVehicle.Get())
	{
		if (Witness->RemembersPlate(CrimeId))
		{
			SetVehicleHot(Vehicle, true);
		}
	}

	if (AActor* Perp = Crime->Perpetrator.Get())
	{
		NotifySighting(Perp, Crime->Location, EMTASightingSource::Witness);
	}
}

void UMTAChaosSubsystem::CancelPendingReports(UMTAWitnessComponent* Witness)
{
	PendingReports.RemoveAll([Witness](const FPendingReport& R) { return R.Witness.Get() == Witness || !R.Witness.IsValid(); });
}

void UMTAChaosSubsystem::ApplyHeat(FMTACrimeEvent& Crime, float Fraction)
{
	const FMTACrimeRule Rule = GetRule(Crime.Type);
	const float Amount = Rule.Heat * Crime.Severity * Fraction;
	if (Amount <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	Crime.HeatApplied += Amount;
	const FName District = Crime.DistrictId;

	if (UWorld* World = GetWorld())
	{
		if (UMTAHeatSubsystem* Heat = World->GetSubsystem<UMTAHeatSubsystem>())
		{
			Heat->AddHeat(District, Amount);
		}
	}
	OnHeatRequested.Broadcast(District, Amount);
}

void UMTAChaosSubsystem::ApplyReputation(const FMTACrimeEvent& Crime, const FMTACrimeRule& Rule)
{
	if (!FMath::IsNearlyZero(Rule.ReputationDelta))
	{
		AddDistrictReputation(Crime.DistrictId, Rule.ReputationDelta * Crime.Severity);
	}
}

// ---------------------------------------------------------------------------------------
// Sichtungen, Kameras, Zeugen
// ---------------------------------------------------------------------------------------

void UMTAChaosSubsystem::RegisterWitness(UMTAWitnessComponent* Witness)
{
	if (Witness)
	{
		Witnesses.AddUnique(Witness);
	}
}

void UMTAChaosSubsystem::UnregisterWitness(UMTAWitnessComponent* Witness)
{
	Witnesses.Remove(Witness);
	CancelPendingReports(Witness);
}

void UMTAChaosSubsystem::RegisterCamera(UMTAAegisCameraComponent* Camera)
{
	if (Camera)
	{
		Cameras.AddUnique(Camera);
	}
}

void UMTAChaosSubsystem::UnregisterCamera(UMTAAegisCameraComponent* Camera)
{
	Cameras.Remove(Camera);
}

void UMTAChaosSubsystem::NotifySighting(AActor* Subject, FVector Location, EMTASightingSource Source)
{
	OnSubjectSighted.Broadcast(Subject, Location, Source);
}

void UMTAChaosSubsystem::HandleCameraCapture(AActor* Subject, UMTAAegisCameraComponent* Camera, float SpeedKmh, bool bFaceCaptured)
{
	if (!Subject || !Camera)
	{
		return;
	}
	AActor* CameraActor = Camera->GetOwner();
	const float Now = GetNow();
	const bool bIsVehicle = HasVehicleRecord(Subject);
	const FMTAAegisCaptureProfile Profile = Camera->CaptureProfile;

	// 1) Offene Verbrechen dieses Subjekts dem Kennzeichen/Gesicht zuordnen.
	//    Kopie der Zeigerliste, weil Delegates neue Verbrechen melden koennen.
	const TArray<TSharedRef<FMTACrimeEvent>> Snapshot = Crimes;
	for (int32 i = Snapshot.Num() - 1; i >= 0; --i)
	{
		const TSharedPtr<FMTACrimeEvent> Crime = Snapshot[i];
		const FMTACrimeRule Rule = GetRule(Crime->Type);
		if (Now - Crime->WorldTime > Rule.CameraAttributionWindow)
		{
			continue; // Fenster je Typ verschieden, daher kein vorzeitiger Abbruch
		}
		if (!IsSubjectOfCrime(*Crime, Subject))
		{
			continue;
		}
		const bool bNewPlate = bIsVehicle && !Crime->bPlateCaptured;
		const bool bNewFace = bFaceCaptured && !Crime->bFaceCaptured;
		if (!bNewPlate && !bNewFace)
		{
			continue;
		}

		Crime->bPlateCaptured |= bNewPlate;
		Crime->bFaceCaptured |= bNewFace;
		if (bNewFace && !Crime->OutfitId.IsNone())
		{
			KnownOutfitId = Crime->OutfitId;
		}
		AddAegisProfile(Rule.AegisDelta * Crime->Severity);
		if (bNewPlate)
		{
			SetVehicleHot(Subject, true);
		}
		OnPlateCaptured.Broadcast(*Crime, Crime->PlateId, CameraActor);
		MarkKnown(Crime, EMTACrimeKnowledge::Recorded);
	}

	// 2) Tempomessung.
	if (bIsVehicle && Profile.SpeedLimitKmh > 0.f && SpeedKmh > Profile.SpeedLimitKmh)
	{
		FMTACrimeEvent Speeding;
		Speeding.Type = EMTACrimeType::Speeding;
		Speeding.Perpetrator = Subject;
		Speeding.PerpetratorVehicle = Subject;
		Speeding.Location = Subject->GetActorLocation();
		Speeding.Severity = FMath::Clamp((SpeedKmh - Profile.SpeedLimitKmh) / 50.f, 0.3f, 2.f);
		const int32 Id = ReportCrime(Speeding);
		if (const TSharedPtr<FMTACrimeEvent> Stored = FindCrimeMutable(Id))
		{
			Stored->bPlateCaptured = true;
			AddAegisProfile(GetRule(Stored->Type).AegisDelta * Stored->Severity);
			OnPlateCaptured.Broadcast(*Stored, Stored->PlateId, CameraActor);
			MarkKnown(Stored, EMTACrimeKnowledge::Recorded);
		}
	}

	// 3) Heisse Kennzeichen / bekannte Outfits melden jede Sichtung (Polizei bekommt die Position).
	if (bIsVehicle)
	{
		const FMTAVehicleRecord* Record = VehicleRecords.Find(FObjectKey(Subject));
		if (Profile.bReportsHotPlates && Record && Record->bHot)
		{
			AddAegisProfile(HotPlateSightingAegisDelta);
			NotifySighting(Subject, Subject->GetActorLocation(), EMTASightingSource::AegisCamera);
		}
	}
	else if (bFaceCaptured && !KnownOutfitId.IsNone() && KnownOutfitId == CurrentOutfitId)
	{
		NotifySighting(Subject, Subject->GetActorLocation(), EMTASightingSource::AegisCamera);
	}
}

// ---------------------------------------------------------------------------------------
// Spieler-Subjekte
// ---------------------------------------------------------------------------------------

void UMTAChaosSubsystem::RegisterPlayerSubject(AActor* Subject)
{
	if (Subject)
	{
		PlayerSubjects.AddUnique(Subject);
	}
}

void UMTAChaosSubsystem::UnregisterPlayerSubject(AActor* Subject)
{
	PlayerSubjects.Remove(Subject);
}

TArray<AActor*> UMTAChaosSubsystem::GetPlayerSubjects() const
{
	TArray<AActor*> Result;
	for (const TWeakObjectPtr<AActor>& Weak : PlayerSubjects)
	{
		AActor* Actor = Weak.Get();
		// Versteckte Figuren (sitzen im Fahrzeug) werden ueber das Fahrzeug erfasst.
		if (Actor && !Actor->IsHidden())
		{
			Result.Add(Actor);
		}
	}
	return Result;
}

// ---------------------------------------------------------------------------------------
// Fahrzeuge
// ---------------------------------------------------------------------------------------

FName UMTAChaosSubsystem::MakeRandomPlate()
{
	// Kestrel-Bay-Format: "KB-" + 2 Buchstaben + 3 Ziffern (eigene Fiktion).
	const TCHAR A = TCHAR('A' + FMath::RandRange(0, 25));
	const TCHAR B = TCHAR('A' + FMath::RandRange(0, 25));
	const int32 Number = (FMath::RandRange(0, 899) + (++PlateCounter) * 7) % 900 + 100;
	return FName(*FString::Printf(TEXT("KB-%c%c-%03d"), A, B, Number));
}

FMTAVehicleRecord UMTAChaosSubsystem::GetOrCreateVehicleRecord(AActor* Vehicle)
{
	if (!Vehicle)
	{
		return FMTAVehicleRecord();
	}
	FMTAVehicleRecord* Record = VehicleRecords.Find(FObjectKey(Vehicle));
	if (!Record)
	{
		FMTAVehicleRecord NewRecord;
		NewRecord.Vehicle = Vehicle;
		NewRecord.PlateId = MakeRandomPlate();
		NewRecord.bPlayerOwned = Vehicle->ActorHasTag(TEXT("PlayerOwned"));
		Record = &VehicleRecords.Add(FObjectKey(Vehicle), NewRecord);
	}
	return *Record;
}

bool UMTAChaosSubsystem::HasVehicleRecord(const AActor* Vehicle) const
{
	return Vehicle && VehicleRecords.Contains(FObjectKey(Vehicle));
}

void UMTAChaosSubsystem::SetVehiclePlayerOwned(AActor* Vehicle, bool bOwned)
{
	if (!Vehicle)
	{
		return;
	}
	GetOrCreateVehicleRecord(Vehicle);
	FMTAVehicleRecord& Record = VehicleRecords.FindChecked(FObjectKey(Vehicle));
	Record.bPlayerOwned = bOwned;
	if (bOwned)
	{
		Record.bStolen = false;
	}
}

void UMTAChaosSubsystem::MarkVehicleStolen(AActor* Vehicle)
{
	if (!Vehicle)
	{
		return;
	}
	GetOrCreateVehicleRecord(Vehicle);
	VehicleRecords.FindChecked(FObjectKey(Vehicle)).bStolen = true;
}

void UMTAChaosSubsystem::SetVehicleHot(AActor* Vehicle, bool bHot)
{
	if (!Vehicle)
	{
		return;
	}
	GetOrCreateVehicleRecord(Vehicle);
	VehicleRecords.FindChecked(FObjectKey(Vehicle)).bHot = bHot;
}

FName UMTAChaosSubsystem::SwapPlate(AActor* Vehicle, FName NewPlate)
{
	if (!Vehicle)
	{
		return NAME_None;
	}
	GetOrCreateVehicleRecord(Vehicle);
	FMTAVehicleRecord& Record = VehicleRecords.FindChecked(FObjectKey(Vehicle));
	Record.PlateId = NewPlate.IsNone() ? MakeRandomPlate() : NewPlate;
	Record.bHot = false;
	return Record.PlateId;
}

void UMTAChaosSubsystem::WatchVehicle(AActor* Vehicle, AActor* Driver)
{
	if (!Vehicle)
	{
		return;
	}
	GetOrCreateVehicleRecord(Vehicle);
	WatchedVehicles.Add(FObjectKey(Vehicle), Driver);
	// Hinweis: OnActorHit feuert nur, wenn die Karosserie "Simulation Generates Hit Events" hat.
	Vehicle->OnActorHit.AddUniqueDynamic(this, &UMTAChaosSubsystem::HandleWatchedVehicleHit);
}

void UMTAChaosSubsystem::UnwatchVehicle(AActor* Vehicle)
{
	if (!Vehicle)
	{
		return;
	}
	WatchedVehicles.Remove(FObjectKey(Vehicle));
	Vehicle->OnActorHit.RemoveDynamic(this, &UMTAChaosSubsystem::HandleWatchedVehicleHit);
}

void UMTAChaosSubsystem::HandleWatchedVehicleHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!SelfActor || !OtherActor || OtherActor == SelfActor)
	{
		return;
	}
	const float Now = GetNow();
	const FObjectKey OtherKey(OtherActor);
	if (const float* Until = HitCooldowns.Find(OtherKey))
	{
		if (*Until > Now)
		{
			return;
		}
	}

	const FVector RelVel = SelfActor->GetVelocity() - OtherActor->GetVelocity();
	const float ImpactSpeed = RelVel.Size();
	if (ImpactSpeed < MinCrashSpeed)
	{
		return;
	}

	// Einordnung des Gegenstuecks.
	EMTACrimeType Type = EMTACrimeType::None;
	if (OtherActor->IsA<ACharacter>())
	{
		Type = EMTACrimeType::PedestrianHit;
	}
	else if (OtherActor->IsA<APawn>())
	{
		Type = EMTACrimeType::Collision;
	}
	else
	{
		const UPrimitiveComponent* OtherComp = Hit.GetComponent();
		const bool bIsProp = OtherActor->ActorHasTag(TEXT("Prop")) || OtherActor->ActorHasTag(TEXT("Destructible"))
			|| (OtherComp && OtherComp->IsSimulatingPhysics());
		if (bIsProp)
		{
			Type = EMTACrimeType::PropertyDamage;
		}
	}
	if (Type == EMTACrimeType::None)
	{
		return; // Waende, Gebaeude, Boden: kein Verbrechen.
	}

	HitCooldowns.Add(OtherKey, Now + 1.5f);

	const TWeakObjectPtr<AActor>* DriverPtr = WatchedVehicles.Find(FObjectKey(SelfActor));
	AActor* Driver = DriverPtr ? DriverPtr->Get() : nullptr;

	FMTACrimeEvent Crime;
	Crime.Type = Type;
	Crime.Perpetrator = Driver ? Driver : SelfActor;
	Crime.PerpetratorVehicle = SelfActor;
	Crime.Victim = OtherActor;
	Crime.Location = Hit.ImpactPoint;
	Crime.Severity = FMath::Clamp(ImpactSpeed / 2000.f, 0.2f, 2.f);
	const int32 CrimeId = ReportCrime(Crime);

	if (Type == EMTACrimeType::Collision || Type == EMTACrimeType::PedestrianHit)
	{
		FHitAndRunWatch& Watch = HitAndRunWatches.AddDefaulted_GetRef();
		Watch.CrimeId = CrimeId;
		Watch.Vehicle = SelfActor;
		Watch.Location = Hit.ImpactPoint;
		Watch.StartTime = Now;
	}
}

void UMTAChaosSubsystem::ProcessHitAndRun(float DeltaTime, float Now)
{
	TArray<int32> Escaped;
	for (int32 i = HitAndRunWatches.Num() - 1; i >= 0; --i)
	{
		FHitAndRunWatch& Watch = HitAndRunWatches[i];
		AActor* Vehicle = Watch.Vehicle.Get();
		if (!Vehicle)
		{
			HitAndRunWatches.RemoveAtSwap(i, 1, EAllowShrinking::No);
			continue;
		}

		const float Distance = FVector::Dist(Vehicle->GetActorLocation(), Watch.Location);
		if (Vehicle->GetVelocity().Size() < 150.f && Distance < HitAndRunDistance * 0.5f)
		{
			Watch.StoppedFor += DeltaTime;
		}

		if (Watch.StoppedFor >= HitAndRunStopTime || Now - Watch.StartTime > 30.f)
		{
			// Angehalten (oder Fenster abgelaufen): keine Fahrerflucht.
			HitAndRunWatches.RemoveAtSwap(i, 1, EAllowShrinking::No);
			continue;
		}

		if (Distance >= HitAndRunDistance)
		{
			Escaped.Add(Watch.CrimeId);
			HitAndRunWatches.RemoveAtSwap(i, 1, EAllowShrinking::No);
		}
	}

	for (const int32 OriginalId : Escaped)
	{
		const TSharedPtr<FMTACrimeEvent> Original = FindCrimeMutable(OriginalId);
		if (!Original.IsValid())
		{
			continue;
		}
		FMTACrimeEvent Flight;
		Flight.Type = EMTACrimeType::HitAndRun;
		Flight.Perpetrator = Original->Perpetrator;
		Flight.PerpetratorVehicle = Original->PerpetratorVehicle;
		Flight.Victim = Original->Victim;
		Flight.Location = Original->Location;
		Flight.DistrictId = Original->DistrictId;
		Flight.Severity = Original->Severity;
		Flight.Tags.Add(FName(*FString::Printf(TEXT("origin_%d"), OriginalId)));
		const int32 FlightId = ReportCrime(Flight);

		// Wer den Unfall gesehen hat, hat auch die Flucht gesehen: Wissen uebernehmen.
		if (const TSharedPtr<FMTACrimeEvent> Stored = FindCrimeMutable(FlightId))
		{
			Stored->WitnessCount += Original->WitnessCount;
			if (Original->Knowledge >= EMTACrimeKnowledge::Reported)
			{
				MarkKnown(Stored, Original->Knowledge);
			}
			else if (Original->WitnessCount > 0 && Stored->Knowledge == EMTACrimeKnowledge::Unknown)
			{
				Stored->Knowledge = EMTACrimeKnowledge::Witnessed;
				// Zeugen des Unfalls melden auch die Flucht (gleiche Verzoegerung wie offene Meldungen).
				for (FPendingReport& Pending : PendingReports)
				{
					if (Pending.CrimeId == OriginalId)
					{
						FPendingReport Extra = Pending;
						Extra.CrimeId = FlightId;
						PendingReports.Add(Extra);
						break;
					}
				}
			}
			OnHitAndRun.Broadcast(*Stored);
		}
	}
}

// ---------------------------------------------------------------------------------------
// Aegis, Ruf, Erscheinungsbild
// ---------------------------------------------------------------------------------------

void UMTAChaosSubsystem::AddAegisProfile(float Delta)
{
	SetAegisProfile(AegisProfile + Delta);
}

void UMTAChaosSubsystem::SetAegisProfile(float NewValue)
{
	const float Old = AegisProfile;
	AegisProfile = FMath::Clamp(NewValue, 0.f, 100.f);
	if (!FMath::IsNearlyEqual(Old, AegisProfile))
	{
		OnAegisProfileChanged.Broadcast(AegisProfile, AegisProfile - Old);
	}
}

float UMTAChaosSubsystem::GetDistrictReputation(FName DistrictId) const
{
	const float* Value = DistrictReputation.Find(DistrictId);
	return Value ? *Value : 0.f;
}

void UMTAChaosSubsystem::AddDistrictReputation(FName DistrictId, float Delta)
{
	float& Value = DistrictReputation.FindOrAdd(DistrictId);
	Value = FMath::Clamp(Value + Delta, -100.f, 100.f);
	const float NewValue = Value;
	OnReputationChanged.Broadcast(DistrictId, NewValue);
}

void UMTAChaosSubsystem::NotifyAppearanceChanged(AActor* Subject, FName NewOutfitId)
{
	// KnownOutfitId bleibt bestehen: die Behoerden suchen weiter nach dem alten Outfit,
	// das neue ist (noch) unbekannt. Der Abgleich passiert in Zeugen und Kameras.
	CurrentOutfitId = NewOutfitId;
}
