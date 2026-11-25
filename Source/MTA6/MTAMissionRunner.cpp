#include "MTAMissionRunner.h"

#include "MTA6GameMode.h"
#include "MTAChaseTarget.h"
#include "MTAMissionPickup.h"
#include "MTAStorySubsystem.h"
#include "MTATrafficTypes.h"
#include "MTAVehiclePawn.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	// --- JSON-Hilfen (nur TryGet*, nie ueber Values iterieren) ---------------------------------

	double MTAMRNum(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, double Default)
	{
		double V = Default;
		if (Obj.IsValid() && Obj->TryGetNumberField(Key, V))
		{
			return V;
		}
		return Default;
	}

	FString MTAMRStr(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		FString V;
		if (Obj.IsValid() && Obj->HasTypedField(Key, EJson::String) && Obj->TryGetStringField(Key, V))
		{
			return V;
		}
		return FString();
	}

	bool MTAMRBool(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		bool V = false;
		if (Obj.IsValid() && Obj->TryGetBoolField(Key, V))
		{
			return V;
		}
		return false;
	}

	bool MTAMRHas(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		return Obj.IsValid() && Obj->HasField(Key);
	}

	TArray<FString> MTAMRStrArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		TArray<FString> V;
		if (Obj.IsValid() && Obj->HasTypedField(Key, EJson::Array))
		{
			Obj->TryGetStringArrayField(Key, V);
		}
		return V;
	}

	FString MTAMRObjectToString(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		if (Obj.IsValid())
		{
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Obj, Writer);
		}
		return Out;
	}

	FString MTAMRArrayToString(const TArray<TSharedPtr<FJsonValue>>& Arr)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Arr, Writer);
		return Out;
	}

	// --- Texte ---------------------------------------------------------------------------------

	/** Nur ASCII und deutsche Umlaute fuer das HUD (Anfuehrungszeichen, Striche, ss ersetzen). */
	FString MTAMRSanitize(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR C : In)
		{
			if (C < 128)
			{
				Out.AppendChar(C);
				continue;
			}
			switch (C)
			{
			case 0x00C4: case 0x00D6: case 0x00DC: case 0x00E4: case 0x00F6: case 0x00FC:
				Out.AppendChar(C);
				break;
			case 0x00DF: Out += TEXT("ss"); break;
			case 0x00BB: case 0x00AB: case 0x201E: case 0x201C: case 0x201D: Out.AppendChar(TEXT('"')); break;
			case 0x201A: case 0x2018: case 0x2019: Out.AppendChar(TEXT('\'')); break;
			case 0x2013: case 0x2014: Out.AppendChar(TEXT('-')); break;
			case 0x2026: Out += TEXT("..."); break;
			case 0x00A0: Out.AppendChar(TEXT(' ')); break;
			case 0x00E9: case 0x00E8: case 0x00EA: Out.AppendChar(TEXT('e')); break;
			case 0x00E1: case 0x00E0: case 0x00E2: Out.AppendChar(TEXT('a')); break;
			case 0x00F3: case 0x00F2: case 0x00F4: Out.AppendChar(TEXT('o')); break;
			case 0x00F1: Out.AppendChar(TEXT('n')); break;
			default: break;
			}
		}
		return Out;
	}

	/** "saltmarket_pension" -> "Saltmarket Pension". */
	FString MTAMRPrettyName(const FString& Id)
	{
		TArray<FString> Parts;
		MTAMRSanitize(Id).ParseIntoArray(Parts, TEXT("_"), true);
		FString Out;
		for (FString& Part : Parts)
		{
			if (Part.Len() > 0)
			{
				Part[0] = FChar::ToUpper(Part[0]);
			}
			Out += (Out.IsEmpty() ? TEXT("") : TEXT(" ")) + Part;
		}
		return Out;
	}

	float MTAMRDistrictFactor(const FString& District)
	{
		if (District == TEXT("core")) return 1.3f;
		if (District == TEXT("greenmoor")) return 1.2f;
		if (District == TEXT("sunstrip")) return 1.1f;
		if (District == TEXT("lantern")) return 0.9f;
		if (District == TEXT("saltmarket")) return 0.8f;
		if (District == TEXT("rustline")) return 0.7f;
		return 1.f;
	}

	/** Kamera-Fahndung je Bezirk (MISSIONSTYPEN.md, Rammen im Bezirk mit Kameras). */
	float MTAMRCameraHeat(const FString& District)
	{
		if (District == TEXT("core")) return 1.f;
		if (District == TEXT("greenmoor")) return 0.8f;
		if (District == TEXT("sunstrip")) return 0.6f;
		if (District == TEXT("saltmarket")) return 0.4f;
		if (District == TEXT("lantern")) return 0.2f;
		if (District == TEXT("rustline")) return 0.1f;
		return 0.3f;
	}

	FString MTAMRCargoNoun(const FString& Cargo, int32 Passengers)
	{
		if (Cargo == TEXT("passengers") || (Cargo.IsEmpty() && Passengers > 0)) return TEXT("die Fahrgaeste");
		if (Cargo == TEXT("crate") || Cargo == TEXT("crates")) return TEXT("die Kisten");
		if (Cargo == TEXT("vip_convoy")) return TEXT("den VIP-Konvoi");
		if (Cargo == TEXT("kcg_raid_plans")) return TEXT("die Einsatzplaene");
		if (Cargo == TEXT("jammer")) return TEXT("den Stoersender");
		if (Cargo.IsEmpty()) return TEXT("das Paket");
		return FString::Printf(TEXT("die Ladung (%s)"), *MTAMRPrettyName(Cargo));
	}

	EMTAChaseCaptureMode MTAMRParseCapture(const FString& Mode)
	{
		if (Mode == TEXT("block")) return EMTAChaseCaptureMode::Block;
		if (Mode == TEXT("ram_or_block")) return EMTAChaseCaptureMode::RamOrBlock;
		if (Mode == TEXT("stop")) return EMTAChaseCaptureMode::Stop;
		if (Mode == TEXT("observe")) return EMTAChaseCaptureMode::Observe;
		return EMTAChaseCaptureMode::Ram;
	}

	bool MTAMRCellValid(const FIntPoint& C)
	{
		return C.X >= 0 && C.X <= MTACity::Blocks && C.Y >= 0 && C.Y <= MTACity::Blocks;
	}

	int32 MTAMRManhattan(const FIntPoint& A, const FIntPoint& B)
	{
		return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y);
	}

	FMTAMissionStep MTAMRMakeStep(EMTAMissionStepKind Kind, const FVector& Location, float Radius, float Duration,
		const FString& Label, const FString& Place)
	{
		FMTAMissionStep S;
		S.Kind = Kind;
		S.Location = Location;
		S.Radius = Radius;
		S.Duration = Duration;
		S.Label = Label;
		S.PlaceName = Place;
		return S;
	}

	const TCHAR* MTAMRFailText(EMTAMissionFailReason Reason)
	{
		switch (Reason)
		{
		case EMTAMissionFailReason::TimeUp:        return TEXT("Zeit abgelaufen");
		case EMTAMissionFailReason::Wrecked:       return TEXT("Fahrzeug zerstoert");
		case EMTAMissionFailReason::TargetLost:    return TEXT("Ziel entkommen");
		case EMTAMissionFailReason::Detected:      return TEXT("Entdeckt");
		case EMTAMissionFailReason::Busted:        return TEXT("Festgenommen");
		case EMTAMissionFailReason::Died:          return TEXT("Ausgeschaltet");
		case EMTAMissionFailReason::CargoDamaged:  return TEXT("Fracht beschaedigt");
		case EMTAMissionFailReason::PassengerLeft: return TEXT("Fahrgast verloren");
		case EMTAMissionFailReason::HeatCap:       return TEXT("Zu viel Fahndung, das Ziel ist abgetaucht");
		case EMTAMissionFailReason::LastPlace:     return TEXT("Letzter Platz");
		case EMTAMissionFailReason::Aborted:       return TEXT("Abgebrochen");
		default:                                   return TEXT("");
		}
	}

	const TCHAR* MTAMRStateText(EMTAMissionRunState InState)
	{
		switch (InState)
		{
		case EMTAMissionRunState::Briefing: return TEXT("Briefing");
		case EMTAMissionRunState::Running:  return TEXT("Laeuft");
		case EMTAMissionRunState::Success:  return TEXT("Erfolg");
		case EMTAMissionRunState::Failed:   return TEXT("Fehlschlag");
		default:                            return TEXT("Idle");
		}
	}
}

// =============================================================================================
// Grundgeruest
// =============================================================================================

UMTAMissionRunner::UMTAMissionRunner()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

UMTAMissionRunner* UMTAMissionRunner::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;
	if (!GameMode)
	{
		return nullptr;
	}
	if (UMTAMissionRunner* Found = GameMode->FindComponentByClass<UMTAMissionRunner>())
	{
		return Found;
	}
	UMTAMissionRunner* Created = NewObject<UMTAMissionRunner>(GameMode, TEXT("MissionRunner"));
	GameMode->AddInstanceComponent(Created);
	Created->RegisterComponent();
	return Created;
}

void UMTAMissionRunner::BeginPlay()
{
	Super::BeginPlay();
	EnsureStoryData();
	if (AMTA6GameMode* GM = GetMTAGameMode())
	{
		GM->OnBusted.AddUniqueDynamic(this, &UMTAMissionRunner::HandleBusted);
		bBustedBound = true;
	}
}

void UMTAMissionRunner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bBustedBound)
	{
		if (AMTA6GameMode* GM = GetMTAGameMode())
		{
			GM->OnBusted.RemoveDynamic(this, &UMTAMissionRunner::HandleBusted);
		}
		bBustedBound = false;
	}
	Cleanup();
	Super::EndPlay(EndPlayReason);
}

void UMTAMissionRunner::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bSelfTestActive)
	{
		return;
	}
	// Dialoge, Briefing-Seiten und Kodex pausieren den Auftrag (wie die uebrige Spiellogik)
	if (const AMTA6GameMode* GM = GetMTAGameMode())
	{
		if (GM->IsDialogOpen())
		{
			return;
		}
	}
	StepMission(DeltaTime);
}

AMTA6GameMode* UMTAMissionRunner::GetMTAGameMode() const
{
	if (AMTA6GameMode* GM = Cast<AMTA6GameMode>(GetOwner()))
	{
		return GM;
	}
	const UWorld* World = GetWorld();
	return World ? World->GetAuthGameMode<AMTA6GameMode>() : nullptr;
}

UMTAStorySubsystem* UMTAMissionRunner::GetStory() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMTAStorySubsystem>() : nullptr;
}

FMTAMissionPlayerProbe UMTAMissionRunner::GetPlayerProbe() const
{
	if (bSimulatePlayer)
	{
		return SimProbe;
	}
	FMTAMissionPlayerProbe P;
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		return P;
	}
	P.bValid = true;
	P.Location = Pawn->GetActorLocation();
	P.Velocity = Pawn->GetVelocity();
	P.Actor = Pawn;
	if (const AMTAVehiclePawn* Vehicle = Cast<AMTAVehiclePawn>(Pawn))
	{
		P.bInVehicle = true;
		P.bWrecked = Vehicle->bWrecked;
		P.VehicleHealth = Vehicle->GetHealthFraction();
	}
	return P;
}

// --- Geld, Fahndung, Meldungen -----------------------------------------------------------------

float UMTAMissionRunner::GetHeatValue() const
{
	if (const AMTA6GameMode* GM = GetMTAGameMode())
	{
		return GM->GetHeat();
	}
	return LocalHeat;
}

void UMTAMissionRunner::SetHeatValue(float NewHeat)
{
	if (AMTA6GameMode* GM = GetMTAGameMode())
	{
		GM->SetHeat(NewHeat);
	}
	LocalHeat = FMath::Clamp(NewHeat, 0.f, 5.f);
}

void UMTAMissionRunner::ReportHeat(float Amount)
{
	if (Amount <= 0.f)
	{
		return;
	}
	if (AMTA6GameMode* GM = GetMTAGameMode())
	{
		GM->ReportCrime(Amount);
	}
	LocalHeat = FMath::Clamp(LocalHeat + Amount, 0.f, 5.f);
}

void UMTAMissionRunner::AddMoneyValue(int32 Amount)
{
	if (Amount == 0 || bSelfTestActive)
	{
		return;
	}
	if (AMTA6GameMode* GM = GetMTAGameMode())
	{
		GM->AddMoney(Amount);
	}
}

void UMTAMissionRunner::Toast(const FString& Text) const
{
	if (bSelfTestActive)
	{
		return;
	}
	if (AMTA6GameMode* GM = GetMTAGameMode())
	{
		GM->ShowToast(Text, 4.f);
	}
}

void UMTAMissionRunner::HandleBusted()
{
	if (State == EMTAMissionRunState::Running)
	{
		bBustedPending = true;
	}
}

void UMTAMissionRunner::NotifyPlayerDied()
{
	if (State == EMTAMissionRunState::Running)
	{
		bDiedPending = true;
	}
}

// =============================================================================================
// Story-Daten und Ortsaufloesung
// =============================================================================================

bool UMTAMissionRunner::EnsureStoryData()
{
	if (StoryData.IsValid())
	{
		return true;
	}
	const FString File = FPaths::ProjectContentDir() / TEXT("Data/story.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *File))
	{
		UE_LOG(LogTemp, Warning, TEXT("MTA6 MissionRunner: story.json nicht lesbar: %s"), *File);
		return false;
	}
	TSharedPtr<FJsonObject> Parsed;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Parsed) || !Parsed.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("MTA6 MissionRunner: story.json ist kein gueltiges JSON"));
		return false;
	}
	StoryData = Parsed;

	// Bezirkszellen aus den Auftraggeber-Zellen ableiten (Bezirk -> Kreuzungen)
	DistrictCells.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Missions = nullptr;
	if (StoryData->TryGetArrayField(TEXT("missions"), Missions))
	{
		for (const TSharedPtr<FJsonValue>& V : *Missions)
		{
			const TSharedPtr<FJsonObject> M = V.IsValid() ? V->AsObject() : nullptr;
			if (!M.IsValid())
			{
				continue;
			}
			const FString District = MTAMRStr(M, TEXT("district"));
			const TArray<TSharedPtr<FJsonValue>>* CellArr = nullptr;
			if (District.IsEmpty() || !M->TryGetArrayField(TEXT("cell"), CellArr) || CellArr->Num() != 2)
			{
				continue;
			}
			const FIntPoint C((int32)(*CellArr)[0]->AsNumber(), (int32)(*CellArr)[1]->AsNumber());
			if (MTAMRCellValid(C))
			{
				DistrictCells.FindOrAdd(District).AddUnique(C);
			}
		}
	}
	return true;
}

TSharedPtr<FJsonObject> UMTAMissionRunner::FindStoryMission(const FString& MissionId) const
{
	const TArray<TSharedPtr<FJsonValue>>* Missions = nullptr;
	if (!StoryData.IsValid() || !StoryData->TryGetArrayField(TEXT("missions"), Missions))
	{
		return nullptr;
	}
	for (const TSharedPtr<FJsonValue>& V : *Missions)
	{
		const TSharedPtr<FJsonObject> M = V.IsValid() ? V->AsObject() : nullptr;
		if (M.IsValid() && MTAMRStr(M, TEXT("id")) == MissionId)
		{
			return M;
		}
	}
	return nullptr;
}

FString UMTAMissionRunner::DistrictFromName(const FString& Name) const
{
	// Aliasse fuer Orte ohne Bezirkspraefix
	static const TCHAR* Aliases[][2] = {
		{ TEXT("aurelia"), TEXT("core") }, { TEXT("kcg"), TEXT("core") }, { TEXT("pier"), TEXT("sunstrip") },
		{ TEXT("open_line"), TEXT("sunstrip") }, { TEXT("st_merrin"), TEXT("lantern") }, { TEXT("harbor"), TEXT("saltmarket") },
		{ TEXT("coast"), TEXT("coastroad") }, { TEXT("autokran"), TEXT("saltmarket") }
	};
	for (const TPair<FString, TArray<FIntPoint>>& Pair : DistrictCells)
	{
		if (Name == Pair.Key || Name.StartsWith(Pair.Key + TEXT("_")))
		{
			return Pair.Key;
		}
	}
	for (const auto& Alias : Aliases)
	{
		const FString Prefix(Alias[0]);
		if (Name == Prefix || Name.StartsWith(Prefix + TEXT("_")))
		{
			return FString(Alias[1]);
		}
	}
	return FString();
}

bool UMTAMissionRunner::ResolvePlace(const FString& Name, const FIntPoint& Anchor, const FString& District, int32 Salt,
	const TArray<FIntPoint>& Avoid, FIntPoint& OutCell, FString& OutDisplay) const
{
	const FString Key = Name.TrimStartAndEnd().ToLower();
	if (Key.IsEmpty() || Key == TEXT("any") || Key == TEXT("none"))
	{
		return false;
	}

	// 1) Feste Koordinaten in registry.locations (optional)
	if (StoryData.IsValid())
	{
		const TSharedPtr<FJsonObject>* Registry = nullptr;
		const TSharedPtr<FJsonObject>* Locations = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Coords = nullptr;
		if (StoryData->TryGetObjectField(TEXT("registry"), Registry)
			&& (*Registry)->TryGetObjectField(TEXT("locations"), Locations)
			&& (*Locations)->TryGetArrayField(Key, Coords) && Coords->Num() == 2)
		{
			OutCell = FIntPoint(FMath::Clamp((int32)(*Coords)[0]->AsNumber(), 0, MTACity::Blocks),
				FMath::Clamp((int32)(*Coords)[1]->AsNumber(), 0, MTACity::Blocks));
			OutDisplay = MTAMRPrettyName(Key);
			return true;
		}
	}

	const uint32 Hash = FCrc::StrCrc32(*Key) ^ (uint32)(Salt * 7919);
	TArray<FIntPoint> Candidates;

	auto PickFrom = [&Avoid, Hash](const TArray<FIntPoint>& Cells, FIntPoint& Picked) -> bool
	{
		TArray<FIntPoint> Free;
		for (const FIntPoint& C : Cells)
		{
			if (!Avoid.Contains(C))
			{
				Free.Add(C);
			}
		}
		const TArray<FIntPoint>& Use = Free.Num() > 0 ? Free : Cells;
		if (Use.Num() == 0)
		{
			return false;
		}
		Picked = Use[(int32)(Hash % (uint32)Use.Num())];
		return true;
	};

	// 2) Variable Ziele: weit weg vom Auftraggeber
	if (Key == TEXT("variable") || Key == TEXT("chosen_ally") || Key == TEXT("random"))
	{
		for (int32 X = 0; X <= MTACity::Blocks; ++X)
		{
			for (int32 Y = 0; Y <= MTACity::Blocks; ++Y)
			{
				if (MTAMRManhattan(FIntPoint(X, Y), Anchor) >= 5)
				{
					Candidates.Add(FIntPoint(X, Y));
				}
			}
		}
		if (PickFrom(Candidates, OutCell))
		{
			OutDisplay = Key == TEXT("chosen_ally") ? FString(TEXT("Unterschlupf der Verbuendeten")) : FString(TEXT("Treffpunkt laut Nachricht"));
			return true;
		}
	}

	// 3) Bezirkspraefix: eine Kreuzung im Bezirk oder direkt daneben
	const FString PlaceDistrict = DistrictFromName(Key);
	if (const TArray<FIntPoint>* Cells = PlaceDistrict.IsEmpty() ? nullptr : DistrictCells.Find(PlaceDistrict))
	{
		for (const FIntPoint& C : *Cells)
		{
			Candidates.AddUnique(C);
			const FIntPoint Neighbors[4] = { C + FIntPoint(1, 0), C - FIntPoint(1, 0), C + FIntPoint(0, 1), C - FIntPoint(0, 1) };
			for (const FIntPoint& N : Neighbors)
			{
				if (MTAMRCellValid(N))
				{
					Candidates.AddUnique(N);
				}
			}
		}
		if (PickFrom(Candidates, OutCell))
		{
			OutDisplay = MTAMRPrettyName(Key);
			return true;
		}
	}

	// 4) Sonst: 2..4 Bloecke vom Auftraggeber (bevorzugt im Bezirk der Mission)
	Candidates.Reset();
	for (int32 X = 0; X <= MTACity::Blocks; ++X)
	{
		for (int32 Y = 0; Y <= MTACity::Blocks; ++Y)
		{
			const int32 D = MTAMRManhattan(FIntPoint(X, Y), Anchor);
			if (D >= 2 && D <= 4)
			{
				Candidates.Add(FIntPoint(X, Y));
			}
		}
	}
	if (PickFrom(Candidates, OutCell))
	{
		OutDisplay = MTAMRPrettyName(Key);
		return true;
	}
	OutCell = Anchor;
	OutDisplay = GetDistrictDisplayName(District);
	return true;
}

float UMTAMissionRunner::FindGroundZ(float X, float Y) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.f;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MTAMissionRunnerGround), false);
	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(Pawn);
	}
	if (World->LineTraceSingleByObjectType(Hit, FVector(X, Y, 50000.f), FVector(X, Y, -50000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return (float)Hit.ImpactPoint.Z;
	}
	return 0.f;
}

FVector UMTAMissionRunner::CellToGround(const FIntPoint& Cell) const
{
	const FVector2D P = MTAGrid::CellToWorld(FMath::Clamp(Cell.X, 0, MTACity::Blocks), FMath::Clamp(Cell.Y, 0, MTACity::Blocks));
	return FVector(P.X, P.Y, FindGroundZ((float)P.X, (float)P.Y));
}

// =============================================================================================
// Missionsbau
// =============================================================================================

EMTAMissionType UMTAMissionRunner::ParseType(const FString& TypeName)
{
	const FString T = TypeName.ToLower();
	if (T == TEXT("courier")) return EMTAMissionType::Courier;
	if (T == TEXT("chase")) return EMTAMissionType::Chase;
	if (T == TEXT("escape")) return EMTAMissionType::Escape;
	if (T == TEXT("race")) return EMTAMissionType::Race;
	if (T == TEXT("hunt")) return EMTAMissionType::Hunt;
	if (T == TEXT("hack")) return EMTAMissionType::Hack;
	if (T == TEXT("gig")) return EMTAMissionType::Gig;
	if (T == TEXT("side")) return EMTAMissionType::Side;
	if (T == TEXT("event")) return EMTAMissionType::Event;
	return EMTAMissionType::Unknown;
}

FString UMTAMissionRunner::GetTypeDisplayName(EMTAMissionType InType)
{
	switch (InType)
	{
	case EMTAMissionType::Courier: return TEXT("Kurier");
	case EMTAMissionType::Chase:   return TEXT("Verfolgung");
	case EMTAMissionType::Escape:  return TEXT("Flucht");
	case EMTAMissionType::Race:    return TEXT("Rennen");
	case EMTAMissionType::Hunt:    return TEXT("Auftragsjagd");
	case EMTAMissionType::Hack:    return TEXT("Hack");
	case EMTAMissionType::Gig:     return TEXT("Gig");
	case EMTAMissionType::Side:    return TEXT("Nebenauftrag");
	case EMTAMissionType::Event:   return TEXT("Ereignis");
	default:                       return TEXT("Auftrag");
	}
}

FString UMTAMissionRunner::GetDistrictDisplayName(const FString& DistrictId)
{
	if (DistrictId == TEXT("core")) return TEXT("Kestrel Core");
	if (DistrictId == TEXT("saltmarket")) return TEXT("Saltmarket");
	if (DistrictId == TEXT("lantern")) return TEXT("Lantern Hill");
	if (DistrictId == TEXT("greenmoor")) return TEXT("Greenmoor");
	if (DistrictId == TEXT("rustline")) return TEXT("Rustline");
	if (DistrictId == TEXT("sunstrip")) return TEXT("Sunstrip");
	if (DistrictId == TEXT("route9")) return TEXT("Route 9");
	if (DistrictId == TEXT("ashridge")) return TEXT("Ashridge");
	if (DistrictId == TEXT("coastroad")) return TEXT("Kuestenstrasse");
	if (DistrictId == TEXT("saltflats")) return TEXT("Salzebene");
	return DistrictId.IsEmpty() ? FString(TEXT("Kestrel Bay")) : MTAMRPrettyName(DistrictId);
}

FMTAMissionSpec UMTAMissionRunner::BuildStorySpec(const FString& MissionId)
{
	EnsureStoryData();
	const TSharedPtr<FJsonObject> M = FindStoryMission(MissionId);
	return M.IsValid() ? BuildSpecFromJson(M) : FMTAMissionSpec();
}

FMTAMissionSpec UMTAMissionRunner::BuildSpecFromJson(const TSharedPtr<FJsonObject>& Mission)
{
	FMTAMissionSpec Out;
	if (!Mission.IsValid())
	{
		return Out;
	}
	Out.Id = MTAMRStr(Mission, TEXT("id"));
	Out.Title = MTAMRSanitize(MTAMRStr(Mission, TEXT("title")));
	Out.Text = MTAMRSanitize(MTAMRStr(Mission, TEXT("text")));
	Out.TypeName = MTAMRStr(Mission, TEXT("type"));
	Out.Type = ParseType(Out.TypeName);
	Out.District = MTAMRStr(Mission, TEXT("district"));
	Out.Giver = MTAMRStr(Mission, TEXT("giver"));
	Out.bMain = MTAMRBool(Mission, TEXT("main"));
	Out.Act = (int32)MTAMRNum(Mission, TEXT("act"), 0.0);
	Out.bFromStory = true;

	const TArray<TSharedPtr<FJsonValue>>* CellArr = nullptr;
	if (Mission->TryGetArrayField(TEXT("cell"), CellArr) && CellArr->Num() == 2)
	{
		Out.Cell = FIntPoint(FMath::Clamp((int32)(*CellArr)[0]->AsNumber(), 0, MTACity::Blocks),
			FMath::Clamp((int32)(*CellArr)[1]->AsNumber(), 0, MTACity::Blocks));
	}

	// Basisgeld = Summe der money-Effekte in onSuccess
	const TArray<TSharedPtr<FJsonValue>>* OnSuccess = nullptr;
	if (Mission->TryGetArrayField(TEXT("onSuccess"), OnSuccess))
	{
		for (const TSharedPtr<FJsonValue>& E : *OnSuccess)
		{
			const TSharedPtr<FJsonObject> EO = E.IsValid() ? E->AsObject() : nullptr;
			Out.BaseMoney += (int32)MTAMRNum(EO, TEXT("money"), 0.0);
		}
	}
	Out.RewardText = MTAMRSanitize(MTAMRStr(Mission, TEXT("reward")));
	if (Out.RewardText.IsEmpty())
	{
		Out.RewardText = FString::Printf(TEXT("$%d"), Out.BaseMoney);
	}

	// Optionales Schema-Erweiterungsfeld onFail (Effekt-Array wie onSuccess)
	const TArray<TSharedPtr<FJsonValue>>* OnFail = nullptr;
	if (Mission->TryGetArrayField(TEXT("onFail"), OnFail) && OnFail->Num() > 0)
	{
		Out.OnFailJson = MTAMRArrayToString(*OnFail);
	}

	const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	if (Mission->TryGetObjectField(TEXT("params"), ParamsPtr) && ParamsPtr->IsValid())
	{
		Params = *ParamsPtr;
	}
	Out.ParamsJson = MTAMRObjectToString(Params);
	BuildSteps(Out, Params);
	return Out;
}

FMTAMissionSpec UMTAMissionRunner::BuildSpec(const FString& Id, const FString& Title, const FString& TypeName, const FString& ParamsJson,
	FIntPoint Cell, const FString& District, int32 Money, bool bMain)
{
	EnsureStoryData();
	FMTAMissionSpec Out;
	Out.Id = Id;
	Out.Title = Title;
	Out.TypeName = TypeName;
	Out.Type = ParseType(TypeName);
	Out.District = District;
	Out.bMain = bMain;
	Out.Cell = FIntPoint(FMath::Clamp(Cell.X, 0, MTACity::Blocks), FMath::Clamp(Cell.Y, 0, MTACity::Blocks));
	Out.BaseMoney = Money;
	Out.RewardText = FString::Printf(TEXT("$%d"), Money);
	Out.bFromStory = false;

	TSharedPtr<FJsonObject> Params;
	if (ParamsJson.IsEmpty() || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ParamsJson), Params) || !Params.IsValid())
	{
		Params = MakeShared<FJsonObject>();
	}
	Out.ParamsJson = MTAMRObjectToString(Params);
	BuildSteps(Out, Params);
	return Out;
}

FMTAMissionSpec UMTAMissionRunner::BuildFreeCourierSpec()
{
	const FMTAMissionPlayerProbe Probe = GetPlayerProbe();
	const FIntPoint Start(MTACity::NearestLine((float)Probe.Location.X), MTACity::NearestLine((float)Probe.Location.Y));

	// Zufaellige, aber weit entfernte Kreuzung (wie die alte freie Kurierfahrt)
	FIntPoint Best = Start;
	int32 BestDist = -1;
	for (int32 Try = 0; Try < 30; ++Try)
	{
		const FIntPoint C(FMath::RandRange(0, MTACity::Blocks), FMath::RandRange(0, MTACity::Blocks));
		const int32 D = MTAMRManhattan(C, Start);
		if (D > BestDist)
		{
			Best = C;
			BestDist = D;
		}
		if (D >= 5)
		{
			break;
		}
	}

	FMTAMissionSpec Out;
	Out.Id = TEXT("free_courier");
	Out.Title = TEXT("Freie Kurierfahrt");
	Out.TypeName = TEXT("courier");
	Out.Type = EMTAMissionType::Courier;
	Out.Cell = Start;
	Out.BaseMoney = 300;
	Out.RewardText = TEXT("$300 + Zeitbonus");
	Out.ProgressNoun = TEXT("Stationen");
	const FVector Target = CellToGround(Best);
	Out.TimeLimit = FMath::RoundToFloat(FVector::Dist2D(Probe.Location, Target) / FMath::Max(100.f, ReferenceSpeed)) + 20.f;
	FMTAMissionStep S = MTAMRMakeStep(EMTAMissionStepKind::Deliver, Target, ArrivalRadius, 0.5f,
		TEXT("Liefere das Paket ab: Kreuzung im Raster"), TEXT("Kreuzung"));
	S.bCountsProgress = true;
	S.bStartsTimer = true;
	S.bInteractive = true;
	Out.Steps.Add(S);
	return Out;
}

void UMTAMissionRunner::BuildSteps(FMTAMissionSpec& Out, const TSharedPtr<FJsonObject>& Params)
{
	Out.Steps.Reset();
	Out.DistrictFactor = MTAMRDistrictFactor(Out.District);
	Out.TimeLimit = (float)MTAMRNum(Params, TEXT("time_limit"), 0.0);
	Out.HeatStart = (float)FMath::Max(MTAMRNum(Params, TEXT("heat_start"), 0.0), MTAMRNum(Params, TEXT("heat_increase"), 0.0));
	Out.HeatCap = MTAMRHas(Params, TEXT("heat_cap")) ? (float)MTAMRNum(Params, TEXT("heat_cap"), 5.0) : -1.f;
	Out.bFragile = MTAMRBool(Params, TEXT("fragile"));
	Out.Passengers = (int32)MTAMRNum(Params, TEXT("passengers"), 0.0);
	const FString Cargo = MTAMRStr(Params, TEXT("cargo"));
	if (Cargo == TEXT("passengers"))
	{
		Out.Passengers = FMath::Max(1, Out.Passengers);
	}
	Out.DamageLimit = FMath::Clamp((float)MTAMRNum(Params, TEXT("damage_limit"), Out.bFragile ? 0.35 : 1.0), 0.05f, 1.f);

	const FIntPoint Anchor = Out.Cell;
	const FVector AnchorLoc = CellToGround(Anchor);
	const FString DistrictName = GetDistrictDisplayName(Out.District);
	TArray<FIntPoint> Used;
	Used.Add(Anchor);
	TMap<FString, FIntPoint> NameCache;
	int32 Salt = 0;

	// Ort aufloesen (gleicher Name -> gleiche Kreuzung), false = Ort entfaellt
	auto Resolve = [this, &Out, &Used, &NameCache, &Salt](const FString& Name, FIntPoint& OutCell, FString& OutDisplay) -> bool
	{
		const FString Key = Name.ToLower();
		if (const FIntPoint* Cached = NameCache.Find(Key))
		{
			OutCell = *Cached;
			OutDisplay = MTAMRPrettyName(Key);
			return true;
		}
		if (!ResolvePlace(Name, Out.Cell, Out.District, Salt++, Used, OutCell, OutDisplay))
		{
			return false;
		}
		NameCache.Add(Key, OutCell);
		Used.AddUnique(OutCell);
		return true;
	};

	// Schritt an einer Kreuzung anhaengen
	auto AddAt = [this, &Out](EMTAMissionStepKind Kind, const FIntPoint& Cell, float Radius, float Duration,
		const FString& Label, const FString& Place) -> FMTAMissionStep&
	{
		const int32 Index = Out.Steps.Add(MTAMRMakeStep(Kind, CellToGround(Cell), Radius, Duration, Label, Place));
		return Out.Steps[Index];
	};

	// Treffpunkt beim Auftraggeber
	auto AddMeeting = [&Out, &AnchorLoc, &DistrictName, this](const FString& Label) -> FMTAMissionStep&
	{
		const int32 Index = Out.Steps.Add(MTAMRMakeStep(EMTAMissionStepKind::Reach, AnchorLoc, ArrivalRadius * 1.5f, 0.f,
			FString::Printf(TEXT("%s: %s"), *Label, *DistrictName), DistrictName));
		return Out.Steps[Index];
	};

	// Kurierkette: Abholen -> Kontrollpunkte -> Abliefern
	auto AddCourierChain = [this, &Out, &Resolve, &AddAt, &Anchor](const FString& FromName, const TArray<FString>& Checkpoints,
		const FString& ToName, const FString& Noun, bool bPickupAtAnchorIfMissing)
	{
		FIntPoint Cell;
		FString Display;
		bool bHasPickup = false;
		if (Resolve(FromName, Cell, Display))
		{
			FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Pickup, Cell, ArrivalRadius, 1.f,
				FString::Printf(TEXT("Hole %s ab: %s"), *Noun, *Display), Display);
			S.bLoadsCargo = true;
			S.bCountsProgress = true;
			S.bInteractive = true;
			bHasPickup = true;
		}
		else if (bPickupAtAnchorIfMissing)
		{
			FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Pickup, Anchor, ArrivalRadius, 1.f,
				FString::Printf(TEXT("Hole %s ab: %s"), *Noun, *GetDistrictDisplayName(Out.District)), GetDistrictDisplayName(Out.District));
			S.bLoadsCargo = true;
			S.bCountsProgress = true;
			S.bInteractive = true;
			bHasPickup = true;
		}
		const int32 FirstAfterPickup = Out.Steps.Num();
		for (const FString& CP : Checkpoints)
		{
			if (!Resolve(CP, Cell, Display))
			{
				continue;
			}
			if (Out.Steps.Num() > 0 && FVector::Dist2D(Out.Steps.Last().Location, CellToGround(Cell)) < 100.f)
			{
				continue;
			}
			FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Checkpoint, Cell, CheckpointRadius, 0.f,
				FString::Printf(TEXT("Passiere den Kontrollpunkt: %s"), *Display), Display);
			S.bCountsProgress = true;
		}
		const FString Target = ToName.IsEmpty() || ToName == TEXT("any") ? FString(TEXT("variable")) : ToName;
		if (Resolve(Target, Cell, Display))
		{
			FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Deliver, Cell, ArrivalRadius, 1.f,
				FString::Printf(TEXT("Liefere %s an: %s"), *Noun, *Display), Display);
			S.bUnloadsCargo = true;
			S.bCountsProgress = true;
			S.bInteractive = true;
		}
		// Zeitlimit laeuft ab dem Abholen (ohne Abholpunkt ab Start)
		if (bHasPickup && Out.Steps.IsValidIndex(FirstAfterPickup))
		{
			Out.Steps[FirstAfterPickup].bStartsTimer = true;
		}
	};

	// Verfolgungsschritt
	auto AddChase = [&Out, &Params](const FVector& Where, const FString& TargetId, EMTAChaseCaptureMode Mode, float Skill) -> FMTAMissionStep&
	{
		FMTAMissionStep S = MTAMRMakeStep(EMTAMissionStepKind::Chase, Where, 0.f, 0.f, FString(), FString());
		S.CaptureMode = Mode;
		S.EscapeSkill = FMath::Clamp(Skill, 0.f, 1.f);
		S.Escorts = FMath::Clamp((int32)MTAMRNum(Params, TEXT("reinforcements"), 0.0), 0, 3);
		S.TargetName = TargetId.IsEmpty() ? FString(TEXT("das Zielfahrzeug")) : MTAMRPrettyName(TargetId);
		S.ObserveMinDistance = (float)MTAMRNum(Params, TEXT("min_distance"), 25.0) * 100.f;
		S.ObserveMaxDistance = FMath::Max(S.ObserveMinDistance + 2000.f, (float)MTAMRNum(Params, TEXT("max_distance"), 120.0) * 100.f);
		const float Limit = Out.TimeLimit > 0.f ? Out.TimeLimit * 0.5f : 90.f;
		S.ObserveDuration = (float)MTAMRNum(Params, TEXT("observe_time"), FMath::Min(90.f, Limit));
		switch (Mode)
		{
		case EMTAChaseCaptureMode::Ram:
			S.Label = FString::Printf(TEXT("Ramme %s, bis es liegen bleibt"), *S.TargetName);
			break;
		case EMTAChaseCaptureMode::Block:
			S.Label = FString::Printf(TEXT("Stelle %s: blockiere die Fahrbahn"), *S.TargetName);
			break;
		case EMTAChaseCaptureMode::Observe:
			S.Label = FString::Printf(TEXT("Beschatte %s (Abstand %d-%d m)"), *S.TargetName,
				FMath::RoundToInt(S.ObserveMinDistance / 100.f), FMath::RoundToInt(S.ObserveMaxDistance / 100.f));
			break;
		default:
			S.Label = FString::Printf(TEXT("Stoppe %s (rammen oder blockieren)"), *S.TargetName);
			break;
		}
		S.bStartsTimer = true;
		const int32 Index = Out.Steps.Add(S);
		return Out.Steps[Index];
	};

	// Flucht-/Untertauchschritt
	auto AddLoseHeat = [this, &Out, &Resolve](float Level, const FString& SafeName, int32 HideSpots, const FString& Label) -> FMTAMissionStep&
	{
		FMTAMissionStep S = MTAMRMakeStep(EMTAMissionStepKind::LoseHeat, FVector::ZeroVector, 0.f, 0.f, FString(), FString());
		S.HeatLevel = FMath::Clamp(Level, 0.f, 5.f);
		S.SafeZoneRadius = 1500.f;
		FIntPoint Cell;
		FString Display;
		if (Resolve(SafeName.IsEmpty() ? FString(TEXT("variable")) : SafeName, Cell, Display))
		{
			S.SafeZones.Add(CellToGround(Cell));
			S.PlaceName = Display;
		}
		for (int32 i = 0; i < HideSpots; ++i)
		{
			FString HideDisplay;
			if (Resolve(FString::Printf(TEXT("hideout_%d"), i + 1), Cell, HideDisplay))
			{
				S.SafeZones.Add(CellToGround(Cell));
			}
		}
		S.Location = S.SafeZones.Num() > 0 ? S.SafeZones[0] : FVector::ZeroVector;
		S.Label = FString::Printf(TEXT("%s oder erreiche ein Versteck (%s)"), *Label, S.PlaceName.IsEmpty() ? TEXT("Karte") : *S.PlaceName);
		S.bStartsTimer = true;
		const int32 Index = Out.Steps.Add(S);
		return Out.Steps[Index];
	};

	const FString TypeLower = Out.TypeName.ToLower();
	switch (Out.Type)
	{
	// ---------------------------------------------------------------------------------------
	case EMTAMissionType::Courier:
	{
		Out.ProgressNoun = TEXT("Stationen");
		AddCourierChain(MTAMRStr(Params, TEXT("from")), MTAMRStrArray(Params, TEXT("checkpoints")), MTAMRStr(Params, TEXT("to")),
			MTAMRCargoNoun(Cargo, Out.Passengers), false);
		const int32 Escort = (int32)MTAMRNum(Params, TEXT("escort"), 0.0);
		if (Escort > 0 && Out.Steps.Num() > 0)
		{
			Out.Steps.Last().Label += FString::Printf(TEXT(" (Begleitschutz: %d Wagen)"), Escort);
		}
		break;
	}
	// ---------------------------------------------------------------------------------------
	case EMTAMissionType::Chase:
	{
		AddMeeting(TEXT("Fahre zum Treffpunkt"));
		AddChase(AnchorLoc, MTAMRStr(Params, TEXT("target")), MTAMRParseCapture(MTAMRStr(Params, TEXT("capture_mode"))),
			(float)MTAMRNum(Params, TEXT("escape_skill"), 0.5));
		break;
	}
	// ---------------------------------------------------------------------------------------
	case EMTAMissionType::Escape:
	{
		AddMeeting(TEXT("Fahre zum Ausgangspunkt"));
		const FString Location = MTAMRStr(Params, TEXT("location"));
		const FString Passenger = MTAMRStr(Params, TEXT("passenger"));
		FIntPoint Cell;
		FString Display;
		if (!Location.IsEmpty() && Resolve(Location, Cell, Display))
		{
			FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Pickup, Cell, ArrivalRadius, 1.f,
				Passenger.IsEmpty() ? FString::Printf(TEXT("Fahre zu: %s"), *Display)
					: FString::Printf(TEXT("Hole %s ab: %s"), *MTAMRPrettyName(Passenger), *Display), Display);
			S.bInteractive = true;
			S.bCountsProgress = true;
			if (!Passenger.IsEmpty())
			{
				S.bLoadsCargo = true;
				Out.Passengers = FMath::Max(1, Out.Passengers);
			}
		}
		FString Safe = MTAMRStr(Params, TEXT("safe_zone"));
		if (Safe.IsEmpty())
		{
			Safe = MTAMRStr(Params, TEXT("route"));
		}
		AddLoseHeat((float)MTAMRNum(Params, TEXT("heat_level"), 2.0), Safe, FMath::Clamp((int32)MTAMRNum(Params, TEXT("hide_spots"), 0.0), 0, 4),
			TEXT("Schuettle die Polizei ab"));
		break;
	}
	// ---------------------------------------------------------------------------------------
	case EMTAMissionType::Race:
	{
		Out.ProgressNoun = TEXT("Checkpoints");
		Out.Opponents = FMath::Clamp((int32)MTAMRNum(Params, TEXT("opponents"), 0.0), 0, 5);
		Out.Laps = FMath::Clamp((int32)MTAMRNum(Params, TEXT("laps"), 1.0), 1, 5);
		Out.Stake = FMath::Max(0, (int32)MTAMRNum(Params, TEXT("stake"), 0.0));

		Out.RaceFirstStep = Out.Steps.Num();
		FMTAMissionStep& StartLine = AddMeeting(TEXT("Fahre zur Startlinie"));
		StartLine.Radius = ArrivalRadius;

		// Rundkurs: achsparalleler Zufallsweg ueber das Raster, zurueck ueber eine L-Kurve
		const FString Route = MTAMRStr(Params, TEXT("route"));
		FRandomStream Rng((int32)(FCrc::StrCrc32(*Route) ^ (uint32)(Anchor.X * 31 + Anchor.Y * 977)));
		const int32 PerLap = FMath::Clamp((int32)MTAMRNum(Params, TEXT("checkpoints_per_lap"), 5.0), 3, 10);
		TArray<FIntPoint> Lap;
		FIntPoint Cur = Anchor;
		int32 LastAxis = -1;
		for (int32 k = 0; k < PerLap - 1; ++k)
		{
			bool bAccepted = false;
			for (int32 Try = 0; Try < 12 && !bAccepted; ++Try)
			{
				const int32 AxisSel = LastAxis < 0 ? Rng.RandRange(0, 1) : 1 - LastAxis;
				const int32 Dir = Rng.RandRange(0, 1) == 0 ? -1 : 1;
				const int32 Len = Rng.RandRange(1, 2);
				FIntPoint Next = Cur;
				if (AxisSel == 0) { Next.X += Dir * Len; } else { Next.Y += Dir * Len; }
				if (MTAMRCellValid(Next) && Next != Anchor && !Lap.Contains(Next))
				{
					Lap.Add(Next);
					Cur = Next;
					LastAxis = AxisSel;
					bAccepted = true;
				}
			}
			if (!bAccepted)
			{
				break;
			}
		}
		const FIntPoint Corner(Anchor.X, Cur.Y);
		if (Corner != Cur && Corner != Anchor)
		{
			Lap.Add(Corner);
		}
		Lap.Add(Anchor);

		const int32 Total = Lap.Num() * Out.Laps;
		int32 Number = 0;
		for (int32 L = 0; L < Out.Laps; ++L)
		{
			for (int32 i = 0; i < Lap.Num(); ++i)
			{
				++Number;
				const bool bFinish = Number == Total;
				const FString Label = bFinish ? FString(TEXT("Ins Ziel!"))
					: (i == Lap.Num() - 1 ? FString::Printf(TEXT("Runde %d geschafft - weiter zu Checkpoint %d/%d"), L + 1, Number, Total)
						: FString::Printf(TEXT("Checkpoint %d/%d"), Number, Total));
				FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Checkpoint, Lap[i], CheckpointRadius, 0.f, Label, FString::Printf(TEXT("CP %d"), Number));
				S.bCountsProgress = true;
				if (Number == 1)
				{
					S.bStartsTimer = true;
				}
			}
		}
		Out.RaceLastStep = Out.Steps.Num() - 1;

		if (Out.TimeLimit <= 0.f)
		{
			float PathLen = 0.f;
			for (int32 i = Out.RaceFirstStep + 1; i <= Out.RaceLastStep; ++i)
			{
				PathLen += (float)FVector::Dist2D(Out.Steps[i - 1].Location, Out.Steps[i].Location);
			}
			const float Speed = FMath::Max(100.f, ReferenceSpeed);
			Out.TimeLimit = Out.Opponents > 0 ? FMath::RoundToFloat(PathLen / Speed * 1.6f) + 30.f
				: FMath::RoundToFloat(PathLen / (Speed * 1.15f)) + 15.f;
		}
		break;
	}
	// ---------------------------------------------------------------------------------------
	case EMTAMissionType::Hunt:
	{
		Out.ProgressNoun = TEXT("Spuren");
		const FString Profile = MTAMRStr(Params, TEXT("target_profile"));
		const int32 Nodes = FMath::Clamp((int32)MTAMRNum(Params, TEXT("investigation_nodes"), 2.0), 0, 6);
		const int32 Prep = FMath::Clamp((int32)MTAMRNum(Params, TEXT("prep_slots"), 1.0), 0, 4);
		FIntPoint Cell;
		FString Display;
		for (int32 i = 0; i < Nodes; ++i)
		{
			if (Resolve(FString::Printf(TEXT("%s_spur_%d"), *Out.District, i + 1), Cell, Display))
			{
				FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Pickup, Cell, ArrivalRadius, 2.f,
					FString::Printf(TEXT("Ermittle: Spur %d/%d bei %s"), i + 1, Nodes, *Display), Display);
				S.bCountsProgress = true;
				S.bInteractive = true;
			}
		}
		for (int32 i = 0; i < Prep; ++i)
		{
			if (Resolve(FString::Printf(TEXT("versteck_ausruestung_%d"), i + 1), Cell, Display))
			{
				FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Pickup, Cell, ArrivalRadius, 1.5f,
					FString::Printf(TEXT("Vorbereitung %d/%d: hole Ausruestung"), i + 1, Prep), Display);
				S.bCountsProgress = true;
				S.bInteractive = true;
			}
		}
		FVector Where = AnchorLoc;
		if (Resolve(FString::Printf(TEXT("%s_jagd"), *Out.District), Cell, Display))
		{
			Where = CellToGround(Cell);
		}
		const FString CaptureName = MTAMRStr(Params, TEXT("capture_mode"));
		AddChase(Where, Profile, CaptureName.IsEmpty() ? EMTAChaseCaptureMode::RamOrBlock : MTAMRParseCapture(CaptureName),
			(float)MTAMRNum(Params, TEXT("escape_skill"), 0.6));
		break;
	}
	// ---------------------------------------------------------------------------------------
	case EMTAMissionType::Hack:
	{
		Out.ProgressNoun = TEXT("Terminals");
		FIntPoint Cell;
		FString Display;
		const FString Carry = MTAMRStr(Params, TEXT("carry"));
		const FString From = MTAMRStr(Params, TEXT("from"));
		bool bCarried = false;
		if (!Carry.IsEmpty() || !From.IsEmpty())
		{
			const FString FromName = From.IsEmpty() ? FString::Printf(TEXT("%s_werkstatt"), *Out.District) : From;
			if (Resolve(FromName, Cell, Display))
			{
				FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Pickup, Cell, ArrivalRadius, 1.f,
					FString::Printf(TEXT("Hole %s: %s"), *MTAMRCargoNoun(Carry, 0), *Display), Display);
				S.bInteractive = true;
				bCarried = true;
			}
		}
		const FString TargetName = MTAMRStr(Params, TEXT("target"));
		const int32 Count = TargetName.IsEmpty() ? FMath::Clamp((int32)MTAMRNum(Params, TEXT("targets"), 1.0), 1, 8) : 1;
		float HackTime = (float)MTAMRNum(Params, TEXT("hack_time"), TypeLower == TEXT("gig") ? 10.0 : 30.0);
		HackTime = FMath::Max(2.f, HackTime);
		const FString RouteName = MTAMRStr(Params, TEXT("route"));
		for (int32 i = 0; i < Count; ++i)
		{
			FIntPoint TermCell = Anchor;
			FString TermDisplay = TargetName.IsEmpty() ? GetDistrictDisplayName(Out.District) : MTAMRPrettyName(TargetName);
			if (i > 0 || bCarried)
			{
				const FString Name = !TargetName.IsEmpty() ? TargetName
					: FString::Printf(TEXT("%s_knoten_%d"), RouteName.IsEmpty() ? *Out.District : *RouteName, i + 1);
				FString Resolved;
				if (Resolve(Name, TermCell, Resolved) && TargetName.IsEmpty())
				{
					TermDisplay = Resolved;
				}
			}
			FMTAMissionStep& S = AddAt(EMTAMissionStepKind::HoldZone, TermCell, HoldZoneRadius, HackTime,
				Count > 1 ? FString::Printf(TEXT("Hacke Terminal %d/%d (%s): bleib im Bereich"), i + 1, Count, *TermDisplay)
					: FString::Printf(TEXT("Hacke %s: bleib im Bereich"), *TermDisplay), TermDisplay);
			S.bCountsProgress = true;
			S.bInteractive = true;
		}
		const bool bEscape = MTAMRBool(Params, TEXT("escape_after")) || MTAMRHas(Params, TEXT("heat_after")) || MTAMRNum(Params, TEXT("pursuers"), 0.0) > 0.0;
		if (bEscape)
		{
			const float HeatAfter = (float)MTAMRNum(Params, TEXT("heat_after"), FMath::Clamp(MTAMRNum(Params, TEXT("pursuers"), 2.0), 1.0, 5.0));
			AddLoseHeat(HeatAfter, TEXT("variable"), 0, TEXT("Alarm! Tauch unter"));
		}
		break;
	}
	// ---------------------------------------------------------------------------------------
	case EMTAMissionType::Gig:
	{
		const FString Template = MTAMRStr(Params, TEXT("template"));
		FIntPoint Cell;
		FString Display;
		if (Template == TEXT("sticker_run"))
		{
			Out.ProgressNoun = TEXT("Aufkleber");
			const int32 N = FMath::Clamp((int32)MTAMRNum(Params, TEXT("targets"), 5.0), 1, 10);
			for (int32 i = 0; i < N; ++i)
			{
				if (Resolve(FString::Printf(TEXT("%s_wand_%d"), *Out.District, i + 1), Cell, Display))
				{
					FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Checkpoint, Cell, CheckpointRadius, 0.f,
						FString::Printf(TEXT("Klebe Aufkleber %d/%d: %s"), i + 1, N, *Display), Display);
					S.bCountsProgress = true;
					if (i == 0)
					{
						S.bStartsTimer = true;
					}
				}
			}
		}
		else if (Template == TEXT("traffic_light_hack"))
		{
			Out.ProgressNoun = TEXT("Ampeln");
			const int32 N = FMath::Clamp((int32)MTAMRNum(Params, TEXT("targets"), 3.0), 1, 8);
			const float HackTime = FMath::Max(2.f, (float)MTAMRNum(Params, TEXT("hack_time"), 10.0));
			for (int32 i = 0; i < N; ++i)
			{
				if (Resolve(FString::Printf(TEXT("%s_ampel_%d"), *Out.District, i + 1), Cell, Display))
				{
					FMTAMissionStep& S = AddAt(EMTAMissionStepKind::HoldZone, Cell, HoldZoneRadius, HackTime,
						FString::Printf(TEXT("Hacke Ampel %d/%d: bleib im Bereich"), i + 1, N), Display);
					S.bCountsProgress = true;
					S.bInteractive = true;
				}
			}
		}
		else if (Template == TEXT("tow"))
		{
			Out.ProgressNoun = TEXT("Stationen");
			if (Resolve(FString::Printf(TEXT("%s_wrack"), *Out.District), Cell, Display))
			{
				FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Pickup, Cell, ArrivalRadius, 2.f,
					FString::Printf(TEXT("Haenge das Wrack an: %s"), *Display), Display);
				S.bLoadsCargo = true;
				S.bCountsProgress = true;
				S.bInteractive = true;
			}
			if (Resolve(TEXT("rustline_scrapyard"), Cell, Display))
			{
				FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Deliver, Cell, ArrivalRadius, 1.f,
					FString::Printf(TEXT("Schlepp das Wrack zum Schrottplatz: %s"), *Display), Display);
				S.bUnloadsCargo = true;
				S.bCountsProgress = true;
				S.bStartsTimer = true;
			}
		}
		else if (Template == TEXT("taxi"))
		{
			Out.ProgressNoun = TEXT("Stationen");
			Out.Passengers = FMath::Max(1, Out.Passengers);
			AddCourierChain(FString(), TArray<FString>(), TEXT("variable"), TEXT("den Fahrgast"), true);
		}
		else if (Template == TEXT("smuggle_passengers"))
		{
			Out.ProgressNoun = TEXT("Stationen");
			Out.Passengers = FMath::Max(1, Out.Passengers);
			AddCourierChain(FString(), MTAMRStrArray(Params, TEXT("checkpoints")), TEXT("variable"), TEXT("die Fahrgaeste"), true);
		}
		else
		{
			// delivery, camera_courier und unbekannte Vorlagen: Lager -> Stopps
			Out.ProgressNoun = TEXT("Stopps");
			const int32 Stops = FMath::Clamp((int32)MTAMRNum(Params, TEXT("stops"), 1.0), 1, 6);
			TArray<FString> Via;
			for (int32 i = 0; i + 1 < Stops; ++i)
			{
				Via.Add(FString::Printf(TEXT("%s_stopp_%d"), *Out.District, i + 1));
			}
			AddCourierChain(FString(), Via, FString::Printf(TEXT("%s_stopp_%d"), *Out.District, Stops), MTAMRCargoNoun(Cargo, Out.Passengers), true);
			// Zwischenstopps sind Lieferungen, keine Kontrollpunkte
			for (FMTAMissionStep& S : Out.Steps)
			{
				if (S.Kind == EMTAMissionStepKind::Checkpoint)
				{
					S.Kind = EMTAMissionStepKind::Deliver;
					S.Radius = ArrivalRadius;
					S.Duration = 1.f;
					S.bInteractive = true;
					S.Label = FString::Printf(TEXT("Liefere ab: %s"), *S.PlaceName);
				}
			}
		}
		break;
	}
	// ---------------------------------------------------------------------------------------
	case EMTAMissionType::Side:
	{
		Out.ProgressNoun = TEXT("Etappen");
		const int32 Beats = FMath::Clamp((int32)MTAMRNum(Params, TEXT("beats"), 2.0), 1, 6);
		FIntPoint Cell = Anchor;
		FString Display = GetDistrictDisplayName(Out.District);
		const FString Location = MTAMRStr(Params, TEXT("location"));
		if (!Location.IsEmpty())
		{
			Resolve(Location, Cell, Display);
		}
		const FString GiverName = Out.Giver.IsEmpty() ? FString(TEXT("deinem Kontakt")) : MTAMRPrettyName(Out.Giver);
		{
			FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Pickup, Cell, ArrivalRadius, 2.f,
				FString::Printf(TEXT("Triff dich mit %s: %s"), *GiverName, *Display), Display);
			S.bCountsProgress = true;
			S.bInteractive = true;
		}
		const FString From = MTAMRStr(Params, TEXT("from"));
		const FString To = MTAMRStr(Params, TEXT("to"));
		const FString Passenger = MTAMRStr(Params, TEXT("passenger"));
		const FString TargetName = MTAMRStr(Params, TEXT("target"));
		if (!From.IsEmpty() || !To.IsEmpty())
		{
			AddCourierChain(From, TArray<FString>(), To, TEXT("die Lieferung"), false);
		}
		else if (!Passenger.IsEmpty() || Out.Passengers > 0)
		{
			Out.Passengers = FMath::Max(1, Out.Passengers);
			const FString Who = Passenger.IsEmpty() ? FString(TEXT("die Fahrgaeste")) : MTAMRPrettyName(Passenger);
			AddCourierChain(FString(), TArray<FString>(), TEXT("variable"), Who, true);
		}
		if (!TargetName.IsEmpty() && Resolve(TargetName, Cell, Display))
		{
			FMTAMissionStep& S = AddAt(EMTAMissionStepKind::HoldZone, Cell, HoldZoneRadius * 0.6f, 10.f,
				FString::Printf(TEXT("Mach dich an %s zu schaffen: bleib in der Naehe"), *Display), Display);
			S.bCountsProgress = true;
			S.bInteractive = true;
		}
		int32 Counted = 0;
		for (const FMTAMissionStep& S : Out.Steps)
		{
			Counted += S.bCountsProgress ? 1 : 0;
		}
		for (int32 i = Counted; i < Beats; ++i)
		{
			if (Resolve(FString::Printf(TEXT("%s_etappe_%d"), *Out.District, i + 1), Cell, Display))
			{
				FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Pickup, Cell, ArrivalRadius, 2.f,
					FString::Printf(TEXT("Geh der Spur nach (%d/%d): %s"), i + 1, Beats, *Display), Display);
				S.bCountsProgress = true;
				S.bInteractive = true;
			}
		}
		break;
	}
	// ---------------------------------------------------------------------------------------
	case EMTAMissionType::Event:
	{
		Out.ProgressNoun = TEXT("Stationen");
		FIntPoint Cell = Anchor;
		FString Display = GetDistrictDisplayName(Out.District);
		const FString Location = MTAMRStr(Params, TEXT("location"));
		if (!Location.IsEmpty())
		{
			Resolve(Location, Cell, Display);
		}
		const FIntPoint EventCell = Cell;
		const FString EventDisplay = Display;
		{
			FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Reach, EventCell, ArrivalRadius * 1.5f, 0.f,
				FString::Printf(TEXT("Fahre zum Ereignis: %s"), *EventDisplay), EventDisplay);
			S.bCountsProgress = true;
		}
		int32 Extra = 0;
		FString ExtraVerb;
		if (MTAMRHas(Params, TEXT("supply_runs"))) { Extra = (int32)MTAMRNum(Params, TEXT("supply_runs"), 0.0); ExtraVerb = TEXT("Versorgungsfahrt"); }
		else if (MTAMRHas(Params, TEXT("photo_targets"))) { Extra = (int32)MTAMRNum(Params, TEXT("photo_targets"), 0.0); ExtraVerb = TEXT("Fotografiere Motiv"); }
		else if (MTAMRHas(Params, TEXT("gate_count"))) { Extra = (int32)MTAMRNum(Params, TEXT("gate_count"), 0.0); ExtraVerb = TEXT("Pruefe Tor"); }
		Extra = FMath::Clamp(Extra, 0, 6);
		for (int32 i = 0; i < Extra; ++i)
		{
			if (Resolve(FString::Printf(TEXT("%s_station_%d"), *Out.District, i + 1), Cell, Display))
			{
				FMTAMissionStep& S = AddAt(EMTAMissionStepKind::Pickup, Cell, ArrivalRadius, 1.5f,
					FString::Printf(TEXT("%s %d/%d: %s"), *ExtraVerb, i + 1, Extra, *Display), Display);
				S.bCountsProgress = true;
				S.bInteractive = true;
			}
		}
		float Hold = 45.f;
		if (MTAMRHas(Params, TEXT("hold_time")))
		{
			Hold = (float)MTAMRNum(Params, TEXT("hold_time"), 30.0);
		}
		else if (MTAMRHas(Params, TEXT("duration_min")))
		{
			Hold = FMath::Clamp((float)MTAMRNum(Params, TEXT("duration_min"), 6.0) * 5.f, 15.f, 60.f);
		}
		{
			FMTAMissionStep& S = AddAt(EMTAMissionStepKind::HoldZone, EventCell, HoldZoneRadius, FMath::Max(3.f, Hold),
				FString::Printf(TEXT("Bleib vor Ort, bis sich die Lage klaert: %s"), *EventDisplay), EventDisplay);
			S.bCountsProgress = true;
		}
		if (MTAMRNum(Params, TEXT("heat_increase"), 0.0) > 0.0)
		{
			AddLoseHeat((float)MTAMRNum(Params, TEXT("heat_increase"), 2.0), TEXT("variable"), 0, TEXT("Razzia! Entkomme der Kontrolle"));
		}
		break;
	}
	// ---------------------------------------------------------------------------------------
	default:
		AddMeeting(TEXT("Fahre zum Auftraggeber"));
		break;
	}

	if (Out.Steps.Num() == 0)
	{
		AddMeeting(TEXT("Fahre zum Auftraggeber"));
	}

	// Kurier-/Gig-Fahrten ohne Zeitangabe: Zeitlimit aus der Streckenlaenge
	if (Out.TimeLimit <= 0.f && (Out.Type == EMTAMissionType::Courier || Out.Type == EMTAMissionType::Gig))
	{
		float PathLen = 0.f;
		FVector Prev = AnchorLoc;
		for (const FMTAMissionStep& S : Out.Steps)
		{
			PathLen += (float)FVector::Dist2D(Prev, S.Location);
			Prev = S.Location;
		}
		Out.TimeLimit = FMath::RoundToFloat(PathLen / FMath::Max(100.f, ReferenceSpeed) * 1.3f) + 30.f;
	}
}

// =============================================================================================
// Auswahl und Steuerung
// =============================================================================================

TArray<FMTAMissionOffer> UMTAMissionRunner::GetAvailable()
{
	TArray<FMTAMissionOffer> Result;
	EnsureStoryData();
	const FMTAMissionPlayerProbe Probe = GetPlayerProbe();
	const int32 MaxStory = bOfferFreeCourier ? 8 : 9;

	if (UMTAStorySubsystem* Story = GetStory())
	{
		TArray<FMTAMission> Missions = Story->GetAvailableMissions();
		Missions.StableSort([](const FMTAMission& A, const FMTAMission& B)
		{
			if (A.bMain != B.bMain)
			{
				return A.bMain;
			}
			return A.Act < B.Act;
		});
		for (const FMTAMission& M : Missions)
		{
			if (Result.Num() >= MaxStory)
			{
				break;
			}
			const TSharedPtr<FJsonObject> J = FindStoryMission(M.Id);
			FMTAMissionOffer Offer;
			Offer.Id = M.Id;
			Offer.Title = MTAMRSanitize(M.Title);
			Offer.bMain = M.bMain;
			Offer.Act = M.Act;
			Offer.Type = ParseType(MTAMRStr(J, TEXT("type")));
			Offer.TypeDisplay = GetTypeDisplayName(Offer.Type);
			Offer.DistrictDisplay = GetDistrictDisplayName(MTAMRStr(J, TEXT("district")));
			const TArray<TSharedPtr<FJsonValue>>* OnSuccess = nullptr;
			if (J.IsValid() && J->TryGetArrayField(TEXT("onSuccess"), OnSuccess))
			{
				for (const TSharedPtr<FJsonValue>& E : *OnSuccess)
				{
					Offer.BaseMoney += (int32)MTAMRNum(E.IsValid() ? E->AsObject() : nullptr, TEXT("money"), 0.0);
				}
			}
			Offer.RewardText = MTAMRSanitize(MTAMRStr(J, TEXT("reward")));
			if (Offer.RewardText.IsEmpty())
			{
				Offer.RewardText = FString::Printf(TEXT("$%d"), Offer.BaseMoney);
			}
			if (Probe.bValid)
			{
				const FVector2D P = MTAGrid::CellToWorld(M.CellX, M.CellY);
				Offer.DistanceMeters = FMath::RoundToInt(FVector2D::Distance(P, FVector2D(Probe.Location.X, Probe.Location.Y)) / 100.f);
			}
			Result.Add(Offer);
		}
	}

	if (bOfferFreeCourier && Result.Num() < 9)
	{
		FMTAMissionOffer Free;
		Free.Id = TEXT("free_courier");
		Free.Title = TEXT("Freie Kurierfahrt");
		Free.Type = EMTAMissionType::Courier;
		Free.TypeDisplay = GetTypeDisplayName(Free.Type);
		Free.DistrictDisplay = TEXT("Kestrel Bay");
		Free.BaseMoney = 300;
		Free.RewardText = TEXT("$300 + Zeitbonus");
		Result.Add(Free);
	}

	for (int32 i = 0; i < Result.Num(); ++i)
	{
		FMTAMissionOffer& O = Result[i];
		const FString Dist = O.DistanceMeters >= 0 ? FString::Printf(TEXT(", %d m"), O.DistanceMeters) : FString();
		O.DisplayLine = FString::Printf(TEXT("[%d] %s%s  -  %s, %s, %s%s"), i + 1, O.bMain ? TEXT("HAUPT: ") : TEXT(""), *O.Title,
			*O.TypeDisplay, *O.DistrictDisplay, *O.RewardText, *Dist);
	}
	return Result;
}

void UMTAMissionRunner::OpenSelection()
{
	SelectionOffers = GetAvailable();
	bSelectionOpen = true;
	OnObjectiveChanged.Broadcast();
}

void UMTAMissionRunner::CloseSelection()
{
	if (bSelectionOpen)
	{
		bSelectionOpen = false;
		OnObjectiveChanged.Broadcast();
	}
}

void UMTAMissionRunner::ToggleSelection()
{
	if (bSelectionOpen)
	{
		CloseSelection();
	}
	else
	{
		OpenSelection();
	}
}

bool UMTAMissionRunner::SelectByIndex(int32 Index)
{
	if (HasActiveMission())
	{
		if (Index < 0)
		{
			Abort();
			CloseSelection();
			return true;
		}
		return false;
	}
	if (!SelectionOffers.IsValidIndex(Index))
	{
		return false;
	}
	return Select(SelectionOffers[Index].Id, true);
}

bool UMTAMissionRunner::Select(const FString& MissionId, bool bStartImmediately)
{
	if (HasActiveMission())
	{
		return false;
	}
	const FMTAMissionSpec NewSpec = MissionId == TEXT("free_courier") ? BuildFreeCourierSpec() : BuildStorySpec(MissionId);
	return StartSpec(NewSpec, bStartImmediately);
}

bool UMTAMissionRunner::StartSpec(const FMTAMissionSpec& InSpec, bool bStartImmediately)
{
	if (!InSpec.IsValid() || HasActiveMission())
	{
		return false;
	}
	Cleanup();
	Spec = InSpec;
	StepIndex = 0;
	bAutoStart = bStartImmediately;
	TimeLeft = Spec.TimeLimit;
	bTimerRunning = false;
	Elapsed = 0.f;
	StepTimer = 0.f;
	HoldProgress = 0.f;
	bCargoLoaded = false;
	FailReason = EMTAMissionFailReason::None;
	RacePlace = 0;
	bSelectionOpen = false;

	SpawnMarkers();
	ChangeState(EMTAMissionRunState::Briefing);
	RefreshMarkers();
	OnObjectiveChanged.Broadcast();
	return true;
}

void UMTAMissionRunner::ConfirmBriefing()
{
	if (State == EMTAMissionRunState::Briefing)
	{
		BeginRun();
	}
}

void UMTAMissionRunner::Abort()
{
	if (!HasActiveMission())
	{
		return;
	}
	// Einsatz zurueck, wenn das Rennen noch nicht gestartet war
	if (State == EMTAMissionRunState::Running && Spec.Stake > 0 && StepIndex <= Spec.RaceFirstStep)
	{
		AddMoneyValue(Spec.Stake);
	}
	FailReason = EMTAMissionFailReason::Aborted;
	LastFinishedId = Spec.Id;
	bLastFinishedFromStory = Spec.bFromStory;
	LastResultMoney = 0;
	Cleanup();
	ChangeState(EMTAMissionRunState::Idle);
	Toast(FString::Printf(TEXT("Auftrag abgebrochen: %s"), *Spec.Title));
	OnObjectiveChanged.Broadcast();
	OnMissionFinished.Broadcast(false);
}

bool UMTAMissionRunner::Retry()
{
	if (HasActiveMission() || !LastFailedSpec.IsValid())
	{
		return false;
	}
	return StartSpec(LastFailedSpec, true);
}

// =============================================================================================
// Ablauf
// =============================================================================================

void UMTAMissionRunner::ChangeState(EMTAMissionRunState NewState)
{
	if (State != NewState)
	{
		State = NewState;
		OnRunStateChanged.Broadcast(NewState);
	}
}

void UMTAMissionRunner::BeginRun()
{
	if (State != EMTAMissionRunState::Briefing)
	{
		return;
	}
	const FMTAMissionPlayerProbe Probe = GetPlayerProbe();
	ChangeState(EMTAMissionRunState::Running);
	Elapsed = 0.f;
	bBustedPending = false;
	bDiedPending = false;
	NoPawnTimer = 0.f;
	OutOfVehicleTimer = 0.f;

	// Zeitlimit ab Start, wenn kein Schritt es spaeter startet
	bool bLaterTimer = false;
	bool bAnyLoads = false;
	for (const FMTAMissionStep& S : Spec.Steps)
	{
		bLaterTimer |= S.bStartsTimer;
		bAnyLoads |= S.bLoadsCargo;
	}
	if (Spec.TimeLimit > 0.f && !bLaterTimer)
	{
		bTimerRunning = true;
		TimeLeft = Spec.TimeLimit;
	}

	// Fracht/Fahrgast von Anfang an an Bord (Kurier ohne Abholpunkt)
	if (!bAnyLoads && (Spec.bFragile || Spec.Passengers > 0) && Spec.Type != EMTAMissionType::Escape)
	{
		bCargoLoaded = true;
		CargoHealthBaseline = Probe.VehicleHealth;
	}

	if (Spec.HeatStart > 0.f)
	{
		const float Heat = GetHeatValue();
		if (Heat < Spec.HeatStart)
		{
			ReportHeat(Spec.HeatStart - Heat);
		}
	}

	// Rennen: Strecke und Gegner
	RacePath.Reset();
	RaceCumulative.Reset();
	if (Spec.RaceFirstStep >= 0 && Spec.RaceLastStep > Spec.RaceFirstStep)
	{
		float Sum = 0.f;
		for (int32 i = Spec.RaceFirstStep; i <= Spec.RaceLastStep; ++i)
		{
			if (RacePath.Num() > 0)
			{
				Sum += (float)FVector::Dist2D(RacePath.Last(), Spec.Steps[i].Location);
			}
			RacePath.Add(Spec.Steps[i].Location);
			RaceCumulative.Add(Sum);
		}
		UWorld* World = GetWorld();
		for (int32 o = 0; o < Spec.Opponents && World; ++o)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParams.ObjectFlags |= RF_Transient;
			AMTAChaseTarget* Opp = World->SpawnActor<AMTAChaseTarget>(AMTAChaseTarget::StaticClass(), FTransform(RacePath[0]), SpawnParams);
			if (!Opp)
			{
				continue;
			}
			static const FLinearColor OppColors[5] = {
				FLinearColor(0.1f, 0.3f, 0.9f), FLinearColor(0.9f, 0.85f, 0.1f), FLinearColor(0.1f, 0.8f, 0.3f),
				FLinearColor(0.9f, 0.4f, 0.05f), FLinearColor(0.8f, 0.8f, 0.85f) };
			Opp->SetBodyColor(OppColors[o % 5]);
			// Renngegner sind Geister (nur Kontakt per Abstand), damit sie den Spieler nicht vom Kurs schieben
			Opp->bPhysicalCollision = false;
			Opp->BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			const float Lateral = (o % 2 == 0 ? -1.f : 1.f) * (250.f + 250.f * (float)(o / 2));
			const float Speed = 1750.f * (0.92f + 0.06f * (float)o) * (1.f + 0.1f * (float)FMath::Max(0, Spec.Act - 1));
			Opp->InitRoute(RacePath, Speed, Lateral);
			Opponents.Add(Opp);
		}
		if (Spec.Stake > 0)
		{
			AddMoneyValue(-Spec.Stake);
			Toast(FString::Printf(TEXT("Einsatz gezahlt: $%d"), Spec.Stake));
		}
	}

	EnterStep(0);
}

void UMTAMissionRunner::SpawnMarkers()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;

	StepMarkers.SetNum(Spec.Steps.Num());
	for (int32 i = 0; i < Spec.Steps.Num(); ++i)
	{
		const FMTAMissionStep& S = Spec.Steps[i];
		EMTAPickupKind Kind = EMTAPickupKind::Meeting;
		FString Prompt;
		switch (S.Kind)
		{
		case EMTAMissionStepKind::Reach:
			Kind = EMTAPickupKind::Meeting;
			break;
		case EMTAMissionStepKind::Pickup:
			Kind = EMTAPickupKind::Pickup;
			Prompt = S.bInteractive ? TEXT("Aufnehmen / Sprechen") : TEXT("");
			break;
		case EMTAMissionStepKind::Deliver:
			Kind = EMTAPickupKind::Dropoff;
			Prompt = S.bInteractive ? TEXT("Abliefern") : TEXT("");
			break;
		case EMTAMissionStepKind::Checkpoint:
			Kind = EMTAPickupKind::Checkpoint;
			break;
		case EMTAMissionStepKind::HoldZone:
			Kind = S.bInteractive ? EMTAPickupKind::Terminal : EMTAPickupKind::Zone;
			Prompt = S.bInteractive ? TEXT("Direktzugriff (doppelt so schnell)") : TEXT("");
			break;
		default:
			continue;
		}
		AMTAMissionPickup* Marker = World->SpawnActor<AMTAMissionPickup>(AMTAMissionPickup::StaticClass(), FTransform(S.Location), SpawnParams);
		if (Marker)
		{
			Marker->Setup(Kind, S.Radius, Prompt);
			Marker->SetMarkerState(false, false);
			StepMarkers[i] = Marker;
		}
	}

	// Verstecke aller Fluchtschritte
	for (const FMTAMissionStep& S : Spec.Steps)
	{
		if (S.Kind != EMTAMissionStepKind::LoseHeat)
		{
			continue;
		}
		for (const FVector& Z : S.SafeZones)
		{
			AMTAMissionPickup* Marker = World->SpawnActor<AMTAMissionPickup>(AMTAMissionPickup::StaticClass(), FTransform(Z), SpawnParams);
			if (Marker)
			{
				Marker->Setup(EMTAPickupKind::SafeZone, S.SafeZoneRadius, FString());
				Marker->SetMarkerState(false, false);
				ZoneMarkers.Add(Marker);
			}
		}
	}
}

void UMTAMissionRunner::RefreshMarkers()
{
	const bool bActive = HasActiveMission();
	for (int32 i = 0; i < StepMarkers.Num(); ++i)
	{
		if (AMTAMissionPickup* Marker = StepMarkers[i])
		{
			const bool bCurrent = bActive && i == StepIndex;
			const bool bPreview = bActive && i == StepIndex + 1 && Spec.Steps.IsValidIndex(i) && Spec.Steps[i].Kind == EMTAMissionStepKind::Checkpoint;
			Marker->SetMarkerState(bCurrent, bPreview);
		}
	}
	const bool bZones = bActive && Spec.Steps.IsValidIndex(StepIndex) && Spec.Steps[StepIndex].Kind == EMTAMissionStepKind::LoseHeat
		&& State == EMTAMissionRunState::Running;
	for (AMTAMissionPickup* Marker : ZoneMarkers)
	{
		if (Marker)
		{
			Marker->SetMarkerState(bZones, false);
		}
	}
}

void UMTAMissionRunner::Cleanup()
{
	for (AMTAMissionPickup* Marker : StepMarkers)
	{
		if (Marker)
		{
			Marker->Destroy();
		}
	}
	for (AMTAMissionPickup* Marker : ZoneMarkers)
	{
		if (Marker)
		{
			Marker->Destroy();
		}
	}
	StepMarkers.Reset();
	ZoneMarkers.Reset();
	if (ChaseTarget)
	{
		ChaseTarget->Destroy();
		ChaseTarget = nullptr;
	}
	for (AMTAChaseTarget* Escort : EscortTargets)
	{
		if (Escort)
		{
			Escort->Destroy();
		}
	}
	EscortTargets.Reset();
	for (AMTAChaseTarget* Opp : Opponents)
	{
		if (Opp)
		{
			Opp->Destroy();
		}
	}
	Opponents.Reset();
	RacePath.Reset();
	RaceCumulative.Reset();
	bBustedPending = false;
	bDiedPending = false;
	bTimerRunning = false;
}

void UMTAMissionRunner::EnterStep(int32 Index)
{
	StepIndex = Index;
	StepTimer = 0.f;
	HoldProgress = 0.f;
	bDirectAccess = false;
	ChaseLostTimer = 0.f;
	ChaseCloseTimer = 0.f;
	ChaseObserveTimer = 0.f;
	StepEnterTime = Elapsed;

	if (!Spec.Steps.IsValidIndex(Index))
	{
		return;
	}
	const FMTAMissionStep& S = Spec.Steps[Index];
	if (S.bStartsTimer && Spec.TimeLimit > 0.f && !bTimerRunning)
	{
		bTimerRunning = true;
		TimeLeft = Spec.TimeLimit;
	}

	const FMTAMissionPlayerProbe Probe = GetPlayerProbe();
	if (S.Kind == EMTAMissionStepKind::Chase)
	{
		UWorld* World = GetWorld();
		if (ChaseTarget)
		{
			ChaseTarget->Destroy();
			ChaseTarget = nullptr;
		}
		if (World)
		{
			// Start etwas vor dem Treffpunkt, vom Spieler weg
			FVector Away = (S.Location - Probe.Location).GetSafeNormal2D();
			if (Away.IsNearlyZero())
			{
				Away = FVector::ForwardVector;
			}
			const FVector SpawnLoc = S.Location + Away * 2500.f;
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParams.ObjectFlags |= RF_Transient;
			ChaseTarget = World->SpawnActor<AMTAChaseTarget>(AMTAChaseTarget::StaticClass(), FTransform(SpawnLoc), SpawnParams);
			if (ChaseTarget)
			{
				ChaseTarget->EscapeSkill = S.EscapeSkill;
				if (S.CaptureMode == EMTAChaseCaptureMode::Observe)
				{
					// Beschatten: ruhige Fahrt, wartet nicht auf Abstand unter dem Hoechstwert
					ChaseTarget->CruiseSpeed = 1100.f;
					ChaseTarget->MaxSpeed = 1500.f;
					ChaseTarget->PanicDistance = 0.f;
					ChaseTarget->CatchUpDistance = S.ObserveMaxDistance * 0.9f;
					ChaseTarget->EscapeSkill = 0.2f;
				}
				else
				{
					ChaseTarget->CruiseSpeed = 1300.f + 700.f * S.EscapeSkill;
					ChaseTarget->MaxSpeed = 2200.f + 900.f * S.EscapeSkill;
				}
				ChaseTarget->InitFlee(SpawnLoc, Probe.Location, (int32)FCrc::StrCrc32(*Spec.Id));
				for (int32 e = 0; e < S.Escorts; ++e)
				{
					AMTAChaseTarget* Escort = World->SpawnActor<AMTAChaseTarget>(AMTAChaseTarget::StaticClass(), FTransform(SpawnLoc), SpawnParams);
					if (Escort)
					{
						Escort->SetBodyColor(FLinearColor(0.08f, 0.08f, 0.1f));
						Escort->InitTrail(ChaseTarget, 1.2f * (float)(e + 1));
						EscortTargets.Add(Escort);
					}
				}
			}
		}
	}
	else if (S.Kind == EMTAMissionStepKind::LoseHeat)
	{
		const float Heat = GetHeatValue();
		if (S.HeatLevel > Heat)
		{
			ReportHeat(S.HeatLevel - Heat);
		}
	}

	RefreshMarkers();
	OnObjectiveChanged.Broadcast();
}

void UMTAMissionRunner::AdvanceStep(const FMTAMissionPlayerProbe& Probe)
{
	if (!Spec.Steps.IsValidIndex(StepIndex))
	{
		Succeed();
		return;
	}
	const FMTAMissionStep& S = Spec.Steps[StepIndex];
	if (S.bLoadsCargo)
	{
		bCargoLoaded = true;
		CargoHealthBaseline = Probe.VehicleHealth;
		bPassengerInVehicle = Probe.bInVehicle;
		OutOfVehicleTimer = 0.f;
	}
	if (S.bUnloadsCargo)
	{
		bCargoLoaded = false;
	}
	if (S.Kind == EMTAMissionStepKind::Chase)
	{
		if (ChaseTarget)
		{
			ChaseTarget->SetDisabled(true);
		}
		for (AMTAChaseTarget* Escort : EscortTargets)
		{
			if (Escort)
			{
				Escort->SetDisabled(true);
			}
		}
		Toast(S.CaptureMode == EMTAChaseCaptureMode::Observe ? FString(TEXT("Ziel ist angekommen - Beschattung erfolgreich"))
			: FString::Printf(TEXT("%s ist gestellt!"), *S.TargetName));
	}

	if (StepIndex == Spec.RaceLastStep && Spec.RaceLastStep >= 0)
	{
		int32 Finished = 0;
		for (const AMTAChaseTarget* Opp : Opponents)
		{
			Finished += (Opp && Opp->IsRouteFinished()) ? 1 : 0;
		}
		RacePlace = 1 + Finished;
		if (Spec.Opponents > 0 && RacePlace > Spec.Opponents)
		{
			Fail(EMTAMissionFailReason::LastPlace);
			return;
		}
	}

	if (StepIndex + 1 >= Spec.Steps.Num())
	{
		Succeed();
		return;
	}
	EnterStep(StepIndex + 1);
}

void UMTAMissionRunner::StepMission(float DeltaSeconds)
{
	const float Dt = FMath::Clamp(DeltaSeconds, 0.f, 0.25f);

	if (State == EMTAMissionRunState::Success || State == EMTAMissionRunState::Failed)
	{
		ResultTimer -= Dt;
		if (ResultTimer <= 0.f)
		{
			ChangeState(EMTAMissionRunState::Idle);
			OnObjectiveChanged.Broadcast();
		}
		return;
	}
	if (State == EMTAMissionRunState::Briefing)
	{
		if (bAutoStart)
		{
			BeginRun();
		}
		return;
	}
	if (State != EMTAMissionRunState::Running)
	{
		return;
	}

	Elapsed += Dt;
	const FMTAMissionPlayerProbe Probe = GetPlayerProbe();

	// Tod / Festnahme / Zerstoerung
	if (!Probe.bValid)
	{
		NoPawnTimer += Dt;
		if (NoPawnTimer > NoPawnFailTime)
		{
			Fail(EMTAMissionFailReason::Died);
		}
		return;
	}
	NoPawnTimer = 0.f;
	if (bDiedPending)
	{
		Fail(EMTAMissionFailReason::Died);
		return;
	}
	if (bBustedPending)
	{
		Fail(EMTAMissionFailReason::Busted);
		return;
	}
	if (Probe.bInVehicle && Probe.bWrecked)
	{
		Fail(EMTAMissionFailReason::Wrecked);
		return;
	}

	// Zeitlimit
	if (bTimerRunning && Spec.TimeLimit > 0.f)
	{
		TimeLeft -= Dt;
		if (TimeLeft <= 0.f)
		{
			TimeLeft = 0.f;
			Fail(EMTAMissionFailReason::TimeUp);
			return;
		}
	}

	// Fracht und Fahrgaeste
	if (bCargoLoaded)
	{
		if (Probe.bInVehicle && Spec.DamageLimit < 1.f && CargoHealthBaseline - Probe.VehicleHealth > Spec.DamageLimit)
		{
			Fail(EMTAMissionFailReason::CargoDamaged);
			return;
		}
		if (Spec.Passengers > 0 && bPassengerInVehicle)
		{
			OutOfVehicleTimer = Probe.bInVehicle ? 0.f : OutOfVehicleTimer + Dt;
			if (OutOfVehicleTimer > PassengerLeaveTime)
			{
				Fail(EMTAMissionFailReason::PassengerLeft);
				return;
			}
		}
	}

	// Fahndungsgrenze (Auftragsjagd)
	if (Spec.HeatCap >= 0.f && GetHeatValue() > Spec.HeatCap + 0.05f)
	{
		Fail(EMTAMissionFailReason::HeatCap);
		return;
	}

	StepRace(Dt, Probe);
	if (State != EMTAMissionRunState::Running)
	{
		return;
	}
	StepCurrent(Dt, Probe);
}

void UMTAMissionRunner::StepCurrent(float Dt, const FMTAMissionPlayerProbe& Probe)
{
	if (!Spec.Steps.IsValidIndex(StepIndex))
	{
		Succeed();
		return;
	}
	const FMTAMissionStep S = Spec.Steps[StepIndex];
	AMTAMissionPickup* Marker = StepMarkers.IsValidIndex(StepIndex) ? StepMarkers[StepIndex].Get() : nullptr;
	const bool bInteracted = Marker && Marker->ConsumeInteraction();
	const float Dist = (float)FVector::Dist2D(Probe.Location, S.Location);
	const float PlayerSpeed = (float)Probe.Velocity.Size2D();

	switch (S.Kind)
	{
	case EMTAMissionStepKind::Reach:
	case EMTAMissionStepKind::Checkpoint:
		if (Dist <= S.Radius || bInteracted)
		{
			AdvanceStep(Probe);
		}
		break;

	case EMTAMissionStepKind::Pickup:
	case EMTAMissionStepKind::Deliver:
		if (bInteracted)
		{
			AdvanceStep(Probe);
			break;
		}
		if (Dist <= S.Radius)
		{
			if (PlayerSpeed <= StopSpeed)
			{
				StepTimer += Dt;
				if (StepTimer >= FMath::Max(0.3f, S.Duration))
				{
					AdvanceStep(Probe);
				}
			}
		}
		else
		{
			StepTimer = 0.f;
		}
		break;

	case EMTAMissionStepKind::HoldZone:
		if (bInteracted)
		{
			bDirectAccess = true;
		}
		if (Dist <= S.Radius)
		{
			HoldProgress += Dt * (bDirectAccess ? 2.f : 1.f);
			if (Marker)
			{
				Marker->SetProgress(S.Duration > 0.f ? HoldProgress / S.Duration : 1.f);
			}
			if (HoldProgress >= S.Duration)
			{
				AdvanceStep(Probe);
			}
		}
		break;

	case EMTAMissionStepKind::Chase:
		StepChase(Dt, Probe, S);
		break;

	case EMTAMissionStepKind::LoseHeat:
	{
		for (const FVector& Z : S.SafeZones)
		{
			if (FVector::Dist2D(Probe.Location, Z) <= S.SafeZoneRadius)
			{
				SetHeatValue(0.f);
				Toast(TEXT("Untergetaucht - die Polizei hat deine Spur verloren"));
				AdvanceStep(Probe);
				return;
			}
		}
		if (Elapsed - StepEnterTime > 1.f && GetHeatValue() <= 0.01f)
		{
			AdvanceStep(Probe);
		}
		break;
	}
	default:
		AdvanceStep(Probe);
		break;
	}
}

void UMTAMissionRunner::StepChase(float Dt, const FMTAMissionPlayerProbe& Probe, const FMTAMissionStep& Step)
{
	if (!ChaseTarget)
	{
		// Ohne Zielfahrzeug ist der Schritt nicht spielbar: ueberspringen statt festhaengen
		UE_LOG(LogTemp, Warning, TEXT("MTA6 MissionRunner: kein Zielfahrzeug fuer %s"), *Spec.Id);
		AdvanceStep(Probe);
		return;
	}
	FMTAChasePlayerContext Ctx;
	Ctx.Location = Probe.Location;
	Ctx.Velocity = Probe.Velocity;
	Ctx.bValid = Probe.bValid;
	Ctx.PlayerActor = Probe.Actor;

	ChaseTarget->StepTarget(Dt, Ctx);
	for (AMTAChaseTarget* Escort : EscortTargets)
	{
		if (Escort)
		{
			Escort->StepTarget(Dt, Ctx);
		}
	}

	const bool bHit = ChaseTarget->WasHitLastStep();
	const float Dist = (float)FVector::Dist2D(Probe.Location, ChaseTarget->GetSimLocation());

	if (Step.CaptureMode == EMTAChaseCaptureMode::Observe)
	{
		if (bHit)
		{
			Fail(EMTAMissionFailReason::Detected);
			return;
		}
		ChaseCloseTimer = Dist < Step.ObserveMinDistance ? ChaseCloseTimer + Dt : FMath::Max(0.f, ChaseCloseTimer - Dt);
		if (ChaseCloseTimer > ObserveDetectTime)
		{
			Fail(EMTAMissionFailReason::Detected);
			return;
		}
		ChaseLostTimer = Dist > Step.ObserveMaxDistance ? ChaseLostTimer + Dt : 0.f;
		if (ChaseLostTimer > ChaseLoseTime)
		{
			Fail(EMTAMissionFailReason::TargetLost);
			return;
		}
		ChaseObserveTimer += Dt;
		if (ChaseObserveTimer >= Step.ObserveDuration)
		{
			AdvanceStep(Probe);
		}
		return;
	}

	if (bHit && !bSelfTestActive)
	{
		// Rammen im Bezirk mit Kameras faellt auf
		ReportHeat(MTAMRCameraHeat(Spec.District) * 0.5f);
	}

	ChaseLostTimer = Dist > ChaseLoseDistance ? ChaseLostTimer + Dt : 0.f;
	if (ChaseLostTimer > ChaseLoseTime)
	{
		Fail(EMTAMissionFailReason::TargetLost);
		return;
	}

	const bool bRammed = ChaseTarget->GetHealthFraction() <= 0.f;
	const bool bBlockMode = Step.CaptureMode == EMTAChaseCaptureMode::Block || Step.CaptureMode == EMTAChaseCaptureMode::RamOrBlock
		|| Step.CaptureMode == EMTAChaseCaptureMode::Stop;
	const bool bBlocked = bBlockMode && ChaseTarget->GetBlockedTime() >= BlockCaptureTime && Dist < 2000.f;
	if (bRammed || bBlocked)
	{
		AdvanceStep(Probe);
	}
}

float UMTAMissionRunner::GetPlayerRaceProgress(const FMTAMissionPlayerProbe& Probe) const
{
	if (Spec.RaceFirstStep < 0 || RacePath.Num() < 2 || StepIndex <= Spec.RaceFirstStep)
	{
		return 0.f;
	}
	const int32 K = FMath::Clamp(StepIndex - Spec.RaceFirstStep, 1, RacePath.Num() - 1);
	const float ToNext = (float)FVector::Dist2D(Probe.Location, RacePath[K]);
	return FMath::Clamp(RaceCumulative[K] - ToNext, RaceCumulative[K - 1], RaceCumulative[K]);
}

int32 UMTAMissionRunner::ComputeRacePlace(const FMTAMissionPlayerProbe& Probe) const
{
	const float Mine = GetPlayerRaceProgress(Probe);
	int32 Place = 1;
	for (const AMTAChaseTarget* Opp : Opponents)
	{
		if (Opp && Opp->GetRouteProgress() > Mine)
		{
			++Place;
		}
	}
	return Place;
}

void UMTAMissionRunner::StepRace(float Dt, const FMTAMissionPlayerProbe& Probe)
{
	if (Spec.RaceFirstStep < 0 || Opponents.Num() == 0)
	{
		return;
	}
	const bool bStarted = StepIndex > Spec.RaceFirstStep;
	const float Mine = GetPlayerRaceProgress(Probe);
	FMTAChasePlayerContext Ctx;
	Ctx.Location = Probe.Location;
	Ctx.Velocity = Probe.Velocity;
	Ctx.bValid = Probe.bValid;
	Ctx.PlayerActor = Probe.Actor;
	for (AMTAChaseTarget* Opp : Opponents)
	{
		if (!Opp)
		{
			continue;
		}
		if (bStarted)
		{
			Opp->StartRoute();
		}
		// Gummiband: weit vorn etwas langsamer, weit hinten etwas schneller
		const float Diff = Opp->GetRouteProgress() - Mine;
		Opp->SetSpeedScale(Diff > 6000.f ? 0.85f : (Diff < -6000.f ? 1.12f : 1.f));
		Opp->StepTarget(Dt, Ctx);
	}
	RacePlace = ComputeRacePlace(Probe);
}

void UMTAMissionRunner::Succeed()
{
	if (State != EMTAMissionRunState::Running)
	{
		return;
	}
	const FMTAMissionPlayerProbe Probe = GetPlayerProbe();

	// Zuschlaege laut MISSIONSTYPEN.md (Basisgeld zahlt bei Story-Missionen die Story)
	const float Base = (float)Spec.BaseMoney;
	float Mult = Spec.DistrictFactor - 1.f;
	const bool bTimeBonus = Spec.Type == EMTAMissionType::Courier || Spec.Type == EMTAMissionType::Gig
		|| Spec.Type == EMTAMissionType::Race || Spec.Type == EMTAMissionType::Hack;
	if (bTimeBonus && Spec.TimeLimit > 0.f && bTimerRunning)
	{
		Mult += 0.3f * FMath::Clamp(TimeLeft / Spec.TimeLimit, 0.f, 1.f);
	}
	if (Spec.RaceLastStep >= 0 && Spec.Opponents > 0)
	{
		static const float PlaceFactor[3] = { 1.f, 0.5f, 0.25f };
		Mult += PlaceFactor[FMath::Clamp(RacePlace - 1, 0, 2)] - 1.f;
	}
	int32 Extra = FMath::RoundToInt(Base * Mult);
	if (Spec.bFragile)
	{
		const float Loss = FMath::Max(0.f, CargoHealthBaseline - Probe.VehicleHealth);
		Extra -= FMath::RoundToInt(Base * 0.1f * FMath::FloorToFloat(Loss / 0.1f));
	}
	if (Spec.Stake > 0)
	{
		Extra += RacePlace == 1 ? Spec.Stake * 2 : (RacePlace == 2 ? Spec.Stake : 0);
	}
	if (!Spec.bFromStory)
	{
		Extra += Spec.BaseMoney;
	}
	AddMoneyValue(Extra);

	if (bCompleteStoryOnSuccess && Spec.bFromStory && !bSelfTestActive)
	{
		if (UMTAStorySubsystem* Story = GetStory())
		{
			Story->CompleteMission(Spec.Id);
			const int32 StoryMoney = Story->ConsumeMoney();
			AddMoneyValue(StoryMoney);
			Extra += StoryMoney;
			ReportHeat(Story->ConsumeHeat());
		}
	}

	LastResultMoney = Extra;
	LastFinishedId = Spec.Id;
	bLastFinishedFromStory = Spec.bFromStory;
	FailReason = EMTAMissionFailReason::None;
	const FString PlaceText = (Spec.RaceLastStep >= 0 && Spec.Opponents > 0) ? FString::Printf(TEXT("  Platz %d"), RacePlace) : FString();
	Toast(FString::Printf(TEXT("Auftrag erfuellt: %s%s  %s$%d"), *Spec.Title, *PlaceText, Extra >= 0 ? TEXT("+") : TEXT("-"), FMath::Abs(Extra)));

	StepIndex = Spec.Steps.Num();
	Cleanup();
	ResultTimer = ResultDisplayTime;
	ChangeState(EMTAMissionRunState::Success);
	OnObjectiveChanged.Broadcast();
	OnMissionFinished.Broadcast(true);
}

void UMTAMissionRunner::Fail(EMTAMissionFailReason Reason)
{
	if (!HasActiveMission())
	{
		return;
	}
	FailReason = Reason;
	const int32 Cost = Reason == EMTAMissionFailReason::Aborted ? 0 : FMath::RoundToInt((float)Spec.BaseMoney * FailCostFraction);
	AddMoneyValue(-Cost);

	if (!bSelfTestActive)
	{
		if (UMTAStorySubsystem* Story = GetStory())
		{
			bool bApplied = false;
			if (Reason == EMTAMissionFailReason::Busted)
			{
				// Festnahme: Aegis-Profil +10 (MISSIONSTYPEN.md, escape)
				Story->ApplyEffectsJson(TEXT("[{\"aegis\":10}]"));
				bApplied = true;
			}
			if (!Spec.OnFailJson.IsEmpty())
			{
				Story->ApplyEffectsJson(Spec.OnFailJson);
				bApplied = true;
			}
			if (bApplied)
			{
				AddMoneyValue(Story->ConsumeMoney());
				ReportHeat(Story->ConsumeHeat());
			}
		}
	}

	LastResultMoney = -Cost;
	LastFinishedId = Spec.Id;
	bLastFinishedFromStory = Spec.bFromStory;
	LastFailedSpec = Spec;
	Toast(FString::Printf(TEXT("Auftrag gescheitert: %s%s"), MTAMRFailText(Reason),
		Cost > 0 ? *FString::Printf(TEXT("  -$%d"), Cost) : TEXT("")));

	Cleanup();
	ResultTimer = ResultDisplayTime;
	ChangeState(EMTAMissionRunState::Failed);
	OnObjectiveChanged.Broadcast();
	OnMissionFinished.Broadcast(false);
}

// =============================================================================================
// Anzeige
// =============================================================================================

FString UMTAMissionRunner::GetTitleText() const
{
	if (State == EMTAMissionRunState::Idle)
	{
		return FString();
	}
	return FString::Printf(TEXT("%s: %s"), *GetTypeDisplayName(Spec.Type), *Spec.Title);
}

FString UMTAMissionRunner::GetObjectiveText() const
{
	switch (State)
	{
	case EMTAMissionRunState::Briefing:
		return FString::Printf(TEXT("Briefing: %s"), *Spec.Title);
	case EMTAMissionRunState::Success:
		return TEXT("AUFTRAG ERFUELLT");
	case EMTAMissionRunState::Failed:
		return FString::Printf(TEXT("AUFTRAG GESCHEITERT: %s"), MTAMRFailText(FailReason));
	case EMTAMissionRunState::Running:
		return Spec.Steps.IsValidIndex(StepIndex) ? Spec.Steps[StepIndex].Label : FString();
	default:
		return bSelectionOpen ? FString(TEXT("Waehle einen Auftrag (Ziffern)")) : FString(TEXT("Kein Auftrag aktiv"));
	}
}

FString UMTAMissionRunner::GetStatusText() const
{
	if (State != EMTAMissionRunState::Running || !Spec.Steps.IsValidIndex(StepIndex))
	{
		return FString();
	}
	const FMTAMissionStep& S = Spec.Steps[StepIndex];
	const FMTAMissionPlayerProbe Probe = GetPlayerProbe();
	const float Dist = (float)FVector::Dist2D(Probe.Location, S.Location);
	FString Out;
	switch (S.Kind)
	{
	case EMTAMissionStepKind::Reach:
	case EMTAMissionStepKind::Checkpoint:
		Out = FString::Printf(TEXT("Entfernung: %d m"), FMath::RoundToInt(Dist / 100.f));
		break;
	case EMTAMissionStepKind::Pickup:
	case EMTAMissionStepKind::Deliver:
		Out = Dist <= S.Radius ? FString(TEXT("Anhalten!")) : FString::Printf(TEXT("Entfernung: %d m"), FMath::RoundToInt(Dist / 100.f));
		break;
	case EMTAMissionStepKind::HoldZone:
	{
		const int32 Pct = S.Duration > 0.f ? FMath::Clamp(FMath::FloorToInt(HoldProgress / S.Duration * 100.f), 0, 100) : 100;
		const TCHAR* What = S.bInteractive ? TEXT("Upload") : TEXT("Halten");
		Out = Dist <= S.Radius ? FString::Printf(TEXT("%s %d %%"), What, Pct)
			: FString::Printf(TEXT("%s %d %% - zurueck in den Bereich (%d m)"), What, Pct, FMath::RoundToInt(Dist / 100.f));
		break;
	}
	case EMTAMissionStepKind::Chase:
		if (ChaseTarget)
		{
			const float TD = (float)FVector::Dist2D(Probe.Location, ChaseTarget->GetSimLocation());
			if (S.CaptureMode == EMTAChaseCaptureMode::Observe)
			{
				const TCHAR* Warn = TD < S.ObserveMinDistance ? TEXT("  ZU NAH!") : (TD > S.ObserveMaxDistance ? TEXT("  ZU WEIT!") : TEXT(""));
				Out = FString::Printf(TEXT("Abstand: %d m%s  Ankunft in %d s"), FMath::RoundToInt(TD / 100.f), Warn,
					FMath::Max(0, FMath::CeilToInt(S.ObserveDuration - ChaseObserveTimer)));
			}
			else
			{
				Out = FString::Printf(TEXT("Ziel: %d m  Zustand %d %%"), FMath::RoundToInt(TD / 100.f),
					FMath::RoundToInt(ChaseTarget->GetHealthFraction() * 100.f));
				if (ChaseTarget->GetBlockedTime() > 0.2f)
				{
					Out += FString::Printf(TEXT("  Blockiert %.1f s"), ChaseTarget->GetBlockedTime());
				}
				if (ChaseLostTimer > 0.f)
				{
					Out += FString::Printf(TEXT("  Ziel entkommt in %d s!"), FMath::Max(0, FMath::CeilToInt(ChaseLoseTime - ChaseLostTimer)));
				}
			}
		}
		break;
	case EMTAMissionStepKind::LoseHeat:
	{
		float Nearest = -1.f;
		for (const FVector& Z : S.SafeZones)
		{
			const float D = (float)FVector::Dist2D(Probe.Location, Z);
			Nearest = Nearest < 0.f ? D : FMath::Min(Nearest, D);
		}
		const int32 Stars = FMath::Clamp(FMath::CeilToInt(GetHeatValue() - 0.001f), 0, 5);
		Out = Nearest >= 0.f ? FString::Printf(TEXT("Fahndung: %d Sterne  Versteck: %d m"), Stars, FMath::RoundToInt(Nearest / 100.f))
			: FString::Printf(TEXT("Fahndung: %d Sterne"), Stars);
		break;
	}
	default:
		break;
	}
	if (Spec.RaceLastStep >= 0 && Spec.Opponents > 0)
	{
		Out += FString::Printf(TEXT("  Platz %d/%d"), FMath::Max(1, RacePlace), Spec.Opponents + 1);
	}
	if (bCargoLoaded && Spec.bFragile && Probe.bInVehicle)
	{
		Out += FString::Printf(TEXT("  Fracht %d %%"), FMath::RoundToInt(FMath::Clamp(1.f - (CargoHealthBaseline - Probe.VehicleHealth) / FMath::Max(0.01f, Spec.DamageLimit), 0.f, 1.f) * 100.f));
	}
	return Out;
}

float UMTAMissionRunner::GetTimeLeft() const
{
	return (State == EMTAMissionRunState::Running && bTimerRunning && Spec.TimeLimit > 0.f) ? FMath::Max(0.f, TimeLeft) : -1.f;
}

int32 UMTAMissionRunner::GetProgressTotal() const
{
	int32 Total = 0;
	for (const FMTAMissionStep& S : Spec.Steps)
	{
		Total += S.bCountsProgress ? 1 : 0;
	}
	return Total;
}

int32 UMTAMissionRunner::GetProgressCurrent() const
{
	int32 Done = 0;
	for (int32 i = 0; i < Spec.Steps.Num() && i < StepIndex; ++i)
	{
		Done += Spec.Steps[i].bCountsProgress ? 1 : 0;
	}
	return Done;
}

FString UMTAMissionRunner::GetProgressText() const
{
	const int32 Total = GetProgressTotal();
	if (State == EMTAMissionRunState::Idle || Total <= 1)
	{
		return FString();
	}
	return FString::Printf(TEXT("%d/%d %s"), GetProgressCurrent(), Total, *Spec.ProgressNoun);
}

TArray<FMTAMissionMarker> UMTAMissionRunner::GetObjectiveMarkers() const
{
	TArray<FMTAMissionMarker> Out;
	if (!HasActiveMission() || !Spec.Steps.IsValidIndex(StepIndex))
	{
		return Out;
	}
	const FMTAMissionStep& S = Spec.Steps[StepIndex];
	auto Add = [&Out](const FVector& Loc, EMTAMissionMarkerKind Kind, float Radius)
	{
		FMTAMissionMarker M;
		M.Location = Loc;
		M.Kind = Kind;
		M.Radius = Radius;
		Out.Add(M);
	};
	if (S.Kind == EMTAMissionStepKind::Chase)
	{
		if (ChaseTarget)
		{
			Add(ChaseTarget->GetActorLocation(), EMTAMissionMarkerKind::Target, 0.f);
		}
		else
		{
			Add(S.Location, EMTAMissionMarkerKind::Objective, 0.f);
		}
		for (const AMTAChaseTarget* Escort : EscortTargets)
		{
			if (Escort)
			{
				Add(Escort->GetActorLocation(), EMTAMissionMarkerKind::Escort, 0.f);
			}
		}
	}
	else if (S.Kind == EMTAMissionStepKind::LoseHeat)
	{
		for (const FVector& Z : S.SafeZones)
		{
			Add(Z, EMTAMissionMarkerKind::SafeZone, S.SafeZoneRadius);
		}
	}
	else
	{
		Add(S.Location, EMTAMissionMarkerKind::Objective, S.Radius);
		if (Spec.Steps.IsValidIndex(StepIndex + 1) && Spec.Steps[StepIndex + 1].Kind == EMTAMissionStepKind::Checkpoint)
		{
			Add(Spec.Steps[StepIndex + 1].Location, EMTAMissionMarkerKind::NextObjective, Spec.Steps[StepIndex + 1].Radius);
		}
	}
	for (const AMTAChaseTarget* Opp : Opponents)
	{
		if (Opp)
		{
			Add(Opp->GetActorLocation(), EMTAMissionMarkerKind::Opponent, 0.f);
		}
	}
	return Out;
}

TArray<FVector> UMTAMissionRunner::GetObjectiveLocations() const
{
	TArray<FVector> Out;
	for (const FMTAMissionMarker& M : GetObjectiveMarkers())
	{
		Out.Add(M.Location);
	}
	return Out;
}

bool UMTAMissionRunner::GetPrimaryObjectiveLocation(FVector& OutLocation) const
{
	const TArray<FMTAMissionMarker> Markers = GetObjectiveMarkers();
	if (Markers.Num() == 0)
	{
		return false;
	}
	// Bei mehreren Verstecken das naechstgelegene
	const FMTAMissionPlayerProbe Probe = GetPlayerProbe();
	float Best = -1.f;
	for (const FMTAMissionMarker& M : Markers)
	{
		if (M.Kind == EMTAMissionMarkerKind::Objective || M.Kind == EMTAMissionMarkerKind::Target || M.Kind == EMTAMissionMarkerKind::SafeZone)
		{
			const float D = (float)FVector::Dist2D(Probe.Location, M.Location);
			if (Best < 0.f || (M.Kind == EMTAMissionMarkerKind::SafeZone && D < Best))
			{
				Best = D;
				OutLocation = M.Location;
			}
		}
	}
	return Best >= 0.f;
}

float UMTAMissionRunner::GetDistanceToObjective() const
{
	FVector Loc;
	const FMTAMissionPlayerProbe Probe = GetPlayerProbe();
	if (!Probe.bValid || !GetPrimaryObjectiveLocation(Loc))
	{
		return -1.f;
	}
	return (float)FVector::Dist2D(Probe.Location, Loc);
}

FString UMTAMissionRunner::GetFailReasonText() const
{
	return FString(MTAMRFailText(FailReason));
}

void UMTAMissionRunner::GatherMinimapBlips(TArray<FMTAMinimapBlip>& OutBlips) const
{
	for (const FMTAMissionMarker& M : GetObjectiveMarkers())
	{
		OutBlips.Add(FMTAMinimapBlip(FVector2D(M.Location.X, M.Location.Y), EMTAMinimapBlipKind::Other));
	}
}

// =============================================================================================
// Selbsttest
// =============================================================================================

void UMTAMissionRunner::SetSimPlayer(const FVector& Location, const FVector& Velocity)
{
	SimProbe.Location = Location;
	SimProbe.Velocity = Velocity;
	SimProbe.bValid = true;
	// Echte Spielfigur mitteleportieren (Kamera/Streaming folgen), Pruefung laeuft ueber SimProbe
	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Pawn->SetActorLocation(Location + FVector(0.f, 0.f, 150.f), false, nullptr, ETeleportType::TeleportPhysics);
		SimProbe.Actor = Pawn;
	}
}

EMTAMissionRunState UMTAMissionRunner::DriveSelfTest(float MaxSeconds, EMTAMissionSelfTestDrive Drive, int32& OutSteps, float& OutSeconds)
{
	const float Dt = 0.1f;
	float T = 0.f;
	int32 Guard = 0;
	while (T < MaxSeconds && HasActiveMission() && Guard++ < 100000)
	{
		if (State == EMTAMissionRunState::Briefing)
		{
			StepMission(Dt);
			continue;
		}
		if (!Spec.Steps.IsValidIndex(StepIndex))
		{
			break;
		}
		const FMTAMissionStep S = Spec.Steps[StepIndex];
		FVector Loc = SimProbe.Location;
		FVector Vel = FVector::ZeroVector;

		if (Drive == EMTAMissionSelfTestDrive::Idle)
		{
			// stehen bleiben
		}
		else if (S.Kind == EMTAMissionStepKind::Chase)
		{
			if (ChaseTarget)
			{
				const FVector TL = ChaseTarget->GetSimLocation();
				const FVector TF = ChaseTarget->GetSimForward();
				const float TS = ChaseTarget->GetSpeed();
				if (Drive == EMTAMissionSelfTestDrive::StayFar)
				{
					Loc = TL - TF * 30000.f;
				}
				else if (S.CaptureMode == EMTAChaseCaptureMode::Observe)
				{
					Loc = TL - TF * ((S.ObserveMinDistance + S.ObserveMaxDistance) * 0.5f);
					Vel = TF * TS;
				}
				else if (S.CaptureMode == EMTAChaseCaptureMode::Ram)
				{
					// dicht auffahren: das Ziel bewegt sich im selben Schritt noch weiter
					Loc = TL - TF * 200.f;
					Vel = TF * (TS + 1500.f);
				}
				else
				{
					// Blockieren: vor das Ziel in dessen Fahrspur stellen
					Loc = TL + TF * 700.f;
				}
			}
		}
		else if (S.Kind == EMTAMissionStepKind::LoseHeat)
		{
			if (Drive == EMTAMissionSelfTestDrive::LoseByCooling || S.SafeZones.Num() == 0)
			{
				SetHeatValue(0.f);
				Loc = SimProbe.Location;
			}
			else
			{
				Loc = S.SafeZones[0];
			}
		}
		else
		{
			Loc = S.Location;
		}
		SetSimPlayer(Loc, Vel);
		StepMission(Dt);
		T += Dt;
	}
	OutSteps = StepIndex;
	OutSeconds = T;
	return State;
}

FString UMTAMissionRunner::RunMissionSelfTest()
{
	TArray<FString> Lines;
	int32 NumOk = 0;
	int32 NumTotal = 0;
	auto Check = [&Lines, &NumOk, &NumTotal](const FString& Name, bool bPass, const FString& Detail)
	{
		++NumTotal;
		NumOk += bPass ? 1 : 0;
		const FString Line = FString::Printf(TEXT("MISSIONTEST %s: %s  %s"), *Name, bPass ? TEXT("OK") : TEXT("FEHLER"), *Detail);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		Lines.Add(Line);
	};

	// --- Zustand sichern -------------------------------------------------------------------
	if (HasActiveMission())
	{
		Abort();
	}
	Cleanup();
	ChangeState(EMTAMissionRunState::Idle);
	AMTA6GameMode* GM = GetMTAGameMode();
	const int32 MoneyBefore = GM ? GM->GetMoney() : 0;
	const float HeatBefore = GetHeatValue();
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	const FTransform PawnXf = Pawn ? Pawn->GetActorTransform() : FTransform::Identity;
	const bool bPrevComplete = bCompleteStoryOnSuccess;
	const float PrevResultTime = ResultDisplayTime;
	bCompleteStoryOnSuccess = false;
	ResultDisplayTime = 0.f;
	bSelfTestActive = true;
	bSimulatePlayer = true;
	SimProbe = FMTAMissionPlayerProbe();
	SimProbe.bValid = true;
	SimProbe.bInVehicle = true;
	SimProbe.VehicleHealth = 1.f;

	const FVector StartLoc = CellToGround(FIntPoint(1, 1));
	auto ResetRun = [this, &StartLoc]()
	{
		if (HasActiveMission())
		{
			Abort();
		}
		Cleanup();
		ChangeState(EMTAMissionRunState::Idle);
		SimProbe.bWrecked = false;
		SimProbe.bInVehicle = true;
		SimProbe.VehicleHealth = 1.f;
		SetHeatValue(0.f);
		SetSimPlayer(StartLoc, FVector::ZeroVector);
	};

	// --- 1) Story-Missionen: baubar und Ziele in der Stadt ---------------------------------
	const bool bData = EnsureStoryData();
	Check(TEXT("story.json geladen"), bData, FString::Printf(TEXT("%d Bezirke mit Zellen"), DistrictCells.Num()));
	TArray<FString> StoryIds;
	const TArray<TSharedPtr<FJsonValue>>* Missions = nullptr;
	if (bData && StoryData->TryGetArrayField(TEXT("missions"), Missions))
	{
		const float Bound = MTACity::Half + 1000.f;
		int32 Good = 0;
		FString Bad;
		for (const TSharedPtr<FJsonValue>& V : *Missions)
		{
			const TSharedPtr<FJsonObject> M = V.IsValid() ? V->AsObject() : nullptr;
			const FMTAMissionSpec Built = BuildSpecFromJson(M);
			bool bOk = Built.IsValid() && Built.Type != EMTAMissionType::Unknown;
			for (const FMTAMissionStep& St : Built.Steps)
			{
				bOk &= FMath::Abs(St.Location.X) <= Bound && FMath::Abs(St.Location.Y) <= Bound && !St.Label.IsEmpty();
			}
			if (bOk)
			{
				++Good;
				StoryIds.Add(Built.Id);
			}
			else
			{
				Bad += TEXT(" ") + Built.Id;
			}
		}
		Check(TEXT("Story-Missionen baubar"), Good == Missions->Num(), FString::Printf(TEXT("%d/%d%s"), Good, Missions->Num(), *Bad));
	}

	// --- 2) Jeder Typ mit simulierter Mission ----------------------------------------------
	struct FMTASelfCase
	{
		const TCHAR* Name;
		const TCHAR* Type;
		const TCHAR* Params;
		EMTAMissionSelfTestDrive Drive;
		EMTAMissionRunState Expect;
		EMTAMissionFailReason ExpectReason;
	};
	static const FMTASelfCase Cases[] = {
		{ TEXT("courier"), TEXT("courier"), TEXT("{\"from\":\"saltmarket_dock\",\"to\":\"core_plaza\",\"checkpoints\":[\"lantern_gate\"],\"time_limit\":300,\"fragile\":true}"), EMTAMissionSelfTestDrive::Normal, EMTAMissionRunState::Success, EMTAMissionFailReason::None },
		{ TEXT("chase_ram"), TEXT("chase"), TEXT("{\"target\":\"testwagen\",\"capture_mode\":\"ram\",\"escape_skill\":0.5,\"reinforcements\":1}"), EMTAMissionSelfTestDrive::Normal, EMTAMissionRunState::Success, EMTAMissionFailReason::None },
		{ TEXT("chase_block"), TEXT("chase"), TEXT("{\"target\":\"testwagen\",\"capture_mode\":\"block\",\"escape_skill\":0.8}"), EMTAMissionSelfTestDrive::Normal, EMTAMissionRunState::Success, EMTAMissionFailReason::None },
		{ TEXT("chase_observe"), TEXT("chase"), TEXT("{\"target\":\"beschattung\",\"capture_mode\":\"observe\",\"min_distance\":25,\"max_distance\":120,\"time_limit\":40}"), EMTAMissionSelfTestDrive::Normal, EMTAMissionRunState::Success, EMTAMissionFailReason::None },
		{ TEXT("escape_versteck"), TEXT("escape"), TEXT("{\"heat_level\":3,\"pursuers\":2,\"safe_zone\":\"rustline\",\"hide_spots\":1}"), EMTAMissionSelfTestDrive::Normal, EMTAMissionRunState::Success, EMTAMissionFailReason::None },
		{ TEXT("escape_abkuehlen"), TEXT("escape"), TEXT("{\"heat_level\":2,\"pursuers\":2,\"safe_zone\":\"rustline\",\"time_limit\":300}"), EMTAMissionSelfTestDrive::LoseByCooling, EMTAMissionRunState::Success, EMTAMissionFailReason::None },
		{ TEXT("race"), TEXT("race"), TEXT("{\"route\":\"testkurs\",\"laps\":2,\"opponents\":2,\"stake\":100}"), EMTAMissionSelfTestDrive::Normal, EMTAMissionRunState::Success, EMTAMissionFailReason::None },
		{ TEXT("hunt"), TEXT("hunt"), TEXT("{\"target_profile\":\"testziel\",\"investigation_nodes\":2,\"prep_slots\":1,\"heat_cap\":3}"), EMTAMissionSelfTestDrive::Normal, EMTAMissionRunState::Success, EMTAMissionFailReason::None },
		{ TEXT("hack"), TEXT("hack"), TEXT("{\"targets\":2,\"hack_time\":6,\"escape_after\":true,\"heat_after\":2}"), EMTAMissionSelfTestDrive::Normal, EMTAMissionRunState::Success, EMTAMissionFailReason::None },
		{ TEXT("gig_delivery"), TEXT("gig"), TEXT("{\"template\":\"delivery\",\"tier\":1,\"stops\":2,\"time_limit\":300}"), EMTAMissionSelfTestDrive::Normal, EMTAMissionRunState::Success, EMTAMissionFailReason::None },
		{ TEXT("gig_sticker"), TEXT("gig"), TEXT("{\"template\":\"sticker_run\",\"tier\":1,\"targets\":3,\"time_limit\":200}"), EMTAMissionSelfTestDrive::Normal, EMTAMissionRunState::Success, EMTAMissionFailReason::None },
		{ TEXT("side"), TEXT("side"), TEXT("{\"beats\":3,\"location\":\"lantern_tower\"}"), EMTAMissionSelfTestDrive::Normal, EMTAMissionRunState::Success, EMTAMissionFailReason::None },
		{ TEXT("event"), TEXT("event"), TEXT("{\"trigger\":\"test\",\"duration_min\":1,\"photo_targets\":1}"), EMTAMissionSelfTestDrive::Normal, EMTAMissionRunState::Success, EMTAMissionFailReason::None },
		{ TEXT("fail_zeit"), TEXT("courier"), TEXT("{\"from\":\"any\",\"to\":\"core_ziel\",\"time_limit\":3}"), EMTAMissionSelfTestDrive::Idle, EMTAMissionRunState::Failed, EMTAMissionFailReason::TimeUp },
		{ TEXT("fail_zielverlust"), TEXT("chase"), TEXT("{\"target\":\"testwagen\",\"capture_mode\":\"ram\"}"), EMTAMissionSelfTestDrive::StayFar, EMTAMissionRunState::Failed, EMTAMissionFailReason::TargetLost },
	};

	for (const FMTASelfCase& C : Cases)
	{
		ResetRun();
		const FMTAMissionSpec TestSpec = BuildSpec(FString::Printf(TEXT("selftest_%s"), C.Name), FString::Printf(TEXT("Test %s"), C.Name),
			C.Type, C.Params, FIntPoint(4, 4), TEXT("core"), 500, false);
		const bool bStarted = StartSpec(TestSpec, true);
		int32 Steps = 0;
		float Secs = 0.f;
		const EMTAMissionRunState Result = bStarted ? DriveSelfTest(600.f, C.Drive, Steps, Secs) : EMTAMissionRunState::Idle;
		const bool bPass = bStarted && Result == C.Expect && (C.Expect != EMTAMissionRunState::Failed || FailReason == C.ExpectReason);
		Check(FString(C.Name), bPass, FString::Printf(TEXT("%s nach %.1f s, %d/%d Schritte%s%s"), MTAMRStateText(Result), Secs, Steps,
			TestSpec.Steps.Num(), FailReason != EMTAMissionFailReason::None ? TEXT(", Grund: ") : TEXT(""), MTAMRFailText(FailReason)));
	}

	// --- 3) Festnahme, Zerstoerung, Tod ----------------------------------------------------
	{
		ResetRun();
		StartSpec(BuildSpec(TEXT("selftest_busted"), TEXT("Test Festnahme"), TEXT("courier"), TEXT("{\"to\":\"core_ziel\"}"), FIntPoint(4, 4), TEXT("core"), 100, false), true);
		StepMission(0.1f);
		HandleBusted();
		StepMission(0.1f);
		Check(TEXT("fail_festnahme"), State == EMTAMissionRunState::Failed && FailReason == EMTAMissionFailReason::Busted, MTAMRFailText(FailReason));

		ResetRun();
		StartSpec(BuildSpec(TEXT("selftest_wreck"), TEXT("Test Wrack"), TEXT("courier"), TEXT("{\"to\":\"core_ziel\"}"), FIntPoint(4, 4), TEXT("core"), 100, false), true);
		StepMission(0.1f);
		SimProbe.bWrecked = true;
		StepMission(0.1f);
		Check(TEXT("fail_zerstoert"), State == EMTAMissionRunState::Failed && FailReason == EMTAMissionFailReason::Wrecked, MTAMRFailText(FailReason));

		ResetRun();
		StartSpec(BuildSpec(TEXT("selftest_died"), TEXT("Test Tod"), TEXT("courier"), TEXT("{\"to\":\"core_ziel\"}"), FIntPoint(4, 4), TEXT("core"), 100, false), true);
		StepMission(0.1f);
		NotifyPlayerDied();
		StepMission(0.1f);
		Check(TEXT("fail_tod"), State == EMTAMissionRunState::Failed && FailReason == EMTAMissionFailReason::Died, MTAMRFailText(FailReason));

		ResetRun();
		StartSpec(BuildSpec(TEXT("selftest_fragile"), TEXT("Test Fracht"), TEXT("courier"), TEXT("{\"from\":\"core_lager\",\"to\":\"core_ziel\",\"fragile\":true}"), FIntPoint(4, 4), TEXT("core"), 100, false), true);
		int32 Dummy = 0;
		float DummySecs = 0.f;
		// bis nach dem Abholen fahren, dann Schaden
		for (int32 i = 0; i < 400 && HasActiveMission() && StepIndex == 0; ++i)
		{
			SetSimPlayer(Spec.Steps[0].Location, FVector::ZeroVector);
			StepMission(0.1f);
		}
		SimProbe.bInVehicle = true;
		SimProbe.VehicleHealth = 0.4f;
		DriveSelfTest(1.f, EMTAMissionSelfTestDrive::Idle, Dummy, DummySecs);
		Check(TEXT("fail_fracht"), State == EMTAMissionRunState::Failed && FailReason == EMTAMissionFailReason::CargoDamaged,
			FString::Printf(TEXT("%s state=%d step=%d cargo=%d limit=%.2f base=%.2f"), *FString(MTAMRFailText(FailReason)), (int32)State, StepIndex, bCargoLoaded ? 1 : 0, Spec.DamageLimit, CargoHealthBaseline));
	}

	// --- 4) Auswahl-API ------------------------------------------------------------------
	{
		ResetRun();
		const TArray<FMTAMissionOffer> Offers = GetAvailable();
		bool bOrdered = true;
		bool bSeenSide = false;
		for (const FMTAMissionOffer& O : Offers)
		{
			if (O.Id == TEXT("free_courier"))
			{
				continue;
			}
			bOrdered &= !(O.bMain && bSeenSide);
			bSeenSide |= !O.bMain;
		}
		Check(TEXT("Auftragsliste"), Offers.Num() > 0 && bOrdered, FString::Printf(TEXT("%d Eintraege, Haupt vor Neben: %s, erster: %s"),
			Offers.Num(), bOrdered ? TEXT("ja") : TEXT("nein"), Offers.Num() > 0 ? *Offers[0].DisplayLine : TEXT("-")));
		if (Offers.Num() > 0)
		{
			OpenSelection();
			const bool bSel = SelectByIndex(0);
			const bool bActive = HasActiveMission();
			SelectByIndex(-1);
			Check(TEXT("Auswahl und Abbruch"), bSel && bActive && State == EMTAMissionRunState::Idle && FailReason == EMTAMissionFailReason::Aborted,
				FString::Printf(TEXT("gewaehlt: %s"), *LastFinishedId));
		}

		ResetRun();
		const bool bFree = Select(TEXT("free_courier"), true);
		int32 FreeSteps = 0;
		float FreeSecs = 0.f;
		const EMTAMissionRunState FreeResult = bFree ? DriveSelfTest(600.f, EMTAMissionSelfTestDrive::Normal, FreeSteps, FreeSecs) : EMTAMissionRunState::Idle;
		Check(TEXT("freie_kurierfahrt"), FreeResult == EMTAMissionRunState::Success, MTAMRStateText(FreeResult));
	}

	// --- 5) Alle Story-Missionen ohne Eingabe durchspielen ---------------------------------
	{
		int32 Done = 0;
		FString Bad;
		for (const FString& Id : StoryIds)
		{
			ResetRun();
			int32 Steps = 0;
			float Secs = 0.f;
			const bool bStarted = StartSpec(BuildStorySpec(Id), true);
			const EMTAMissionRunState Result = bStarted ? DriveSelfTest(900.f, EMTAMissionSelfTestDrive::Normal, Steps, Secs) : EMTAMissionRunState::Idle;
			if (Result == EMTAMissionRunState::Success)
			{
				++Done;
			}
			else
			{
				Bad += FString::Printf(TEXT(" %s(%s/%s)"), *Id, MTAMRStateText(Result), MTAMRFailText(FailReason));
			}
		}
		Check(TEXT("Story-Missionen durchspielbar"), Done == StoryIds.Num(), FString::Printf(TEXT("%d/%d%s"), Done, StoryIds.Num(), *Bad));
	}

	// --- Zustand wiederherstellen ---------------------------------------------------------
	ResetRun();
	bSimulatePlayer = false;
	bSelfTestActive = false;
	bCompleteStoryOnSuccess = bPrevComplete;
	ResultDisplayTime = PrevResultTime;
	FailReason = EMTAMissionFailReason::None;
	LastFailedSpec = FMTAMissionSpec();
	Spec = FMTAMissionSpec();
	StepIndex = 0;
	SetHeatValue(HeatBefore);
	if (GM)
	{
		GM->AddMoney(MoneyBefore - GM->GetMoney());
	}
	if (Pawn)
	{
		Pawn->SetActorTransform(PawnXf, false, nullptr, ETeleportType::TeleportPhysics);
	}

	const FString Summary = FString::Printf(TEXT("MISSIONTEST ERGEBNIS: %d/%d OK"), NumOk, NumTotal);
	UE_LOG(LogTemp, Display, TEXT("%s"), *Summary);
	Lines.Add(Summary);
	return FString::Join(Lines, TEXT("\n"));
}
