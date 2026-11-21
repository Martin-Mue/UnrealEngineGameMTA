#include "MTAInteriorSubsystem.h"
#include "MTAInteriorInstance.h"
#include "MTAInteriorInteractive.h"
#include "MTABuildingEntry.h"
#include "MTAShopComponent.h"
#include "MTATimeOfDay.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTAInterior, Log, All);

UMTAInteriorSubsystem* UMTAInteriorSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UMTAInteriorSubsystem>() : nullptr;
}

void UMTAInteriorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadData();
}

void UMTAInteriorSubsystem::Deinitialize()
{
	Instances.Reset();
	Entries.Reset();
	PawnInside.Reset();
	Super::Deinitialize();
}

bool UMTAInteriorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMTAInteriorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMTAInteriorSubsystem, STATGROUP_Tickables);
}

bool UMTAInteriorSubsystem::LoadData()
{
	FString Error;
	const FString Interiors = FPaths::ProjectContentDir() / InteriorsDataPath;
	const FString Furnishing = FPaths::ProjectContentDir() / FurnishingDataPath;
	if (!Database.Load(Interiors, Furnishing, Error))
	{
		UE_LOG(LogMTAInterior, Error, TEXT("Innenraum-Daten nicht geladen: %s"), *Error);
		return false;
	}
	UE_LOG(LogMTAInterior, Log, TEXT("Innenraeume: %d Vorlagen, %d Raumtypen, %d Moebel/Deko-Objekte"),
		Database.NumTemplates(), Database.NumRoomTypes(), Database.GetFurnishing().NumItems());
	return true;
}

FName UMTAInteriorSubsystem::PickTemplateId(EMTADistrict District, FName RoomType, int32 Seed, FVector2D MaxFootprintCm) const
{
	const FMTAInteriorTemplate* T = Database.PickTemplate(District, RoomType, Seed, MaxFootprintCm);
	return T ? T->Id : NAME_None;
}

TArray<FName> UMTAInteriorSubsystem::GetTemplateIds(EMTADistrict District, FName RoomType) const
{
	TArray<const FMTAInteriorTemplate*> List;
	Database.GetTemplates(District, RoomType, List);
	TArray<FName> Out;
	for (const FMTAInteriorTemplate* T : List) { Out.Add(T->Id); }
	return Out;
}

// ---------------------------------------------------------------------------
// Instanzen
// ---------------------------------------------------------------------------

TSubclassOf<AMTAInteriorInstance> UMTAInteriorSubsystem::ResolveInstanceClass() const
{
	if (!InstanceClass.IsNull())
	{
		if (UClass* C = InstanceClass.LoadSynchronous()) { return C; }
	}
	return AMTAInteriorInstance::StaticClass();
}

int32 UMTAInteriorSubsystem::AllocateLayer(const FBox2D& XYBounds) const
{
	for (int32 Layer = 0; Layer < MaxLayers; ++Layer)
	{
		bool bFree = true;
		for (const TPair<FName, TObjectPtr<AMTAInteriorInstance>>& P : Instances)
		{
			const AMTAInteriorInstance* I = P.Value;
			if (!I || I->GridLayer != Layer) { continue; }
			const FBox B = I->GetWorldBounds();
			if (FBox2D(FVector2D(B.Min), FVector2D(B.Max)).ExpandBy(500.f).Intersect(XYBounds)) { bFree = false; break; }
		}
		if (bFree) { return Layer; }
	}
	return INDEX_NONE;
}

AMTAInteriorInstance* UMTAInteriorSubsystem::FindInterior(FName EntryKey) const
{
	const TObjectPtr<AMTAInteriorInstance>* Found = Instances.Find(EntryKey);
	return Found ? Found->Get() : nullptr;
}

AMTAInteriorInstance* UMTAInteriorSubsystem::AcquireInterior(const FMTAInteriorRequest& Request)
{
	if (AMTAInteriorInstance* Existing = FindInterior(Request.EntryKey)) { return Existing; }
	UWorld* World = GetWorld();
	if (!World || !Database.IsLoaded()) { return nullptr; }

	const FMTAInteriorTemplate* T = Database.FindTemplate(Request.TemplateId);
	if (!T) { T = Database.PickTemplate(Request.District, NAME_None, Request.Seed, FVector2D::ZeroVector); }
	if (!T)
	{
		UE_LOG(LogMTAInterior, Warning, TEXT("Keine Vorlage fuer %s"), *Request.EntryKey.ToString());
		return nullptr;
	}
	const FMTAStyleDef* Style = Database.ResolveStyle(*T, Request.District, Request.StyleOverride);
	if (!Style) { return nullptr; }

	FMTAInteriorBuildSettings Settings;
	Settings.MaxLights = MaxLightsPerInterior;
	Settings.MaxShadowLights = MaxShadowLightsPerInterior;
	Settings.MaxWindowLights = MaxWindowLightsPerInterior;
	FMTAInteriorBuildResult Result;
	const int32 Seed = HashCombine(GetTypeHash(Request.EntryKey), (uint32)Request.Seed);
	if (!FMTAInteriorBuilder::Build(*T, Database, *Style, Seed, Settings, Result)) { return nullptr; }

	// Zelle senkrecht unter dem Gebaeude: World-Partition-Streaming haelt die Umgebung geladen
	const FVector2D Half = T->Footprint * 0.5f;
	const FVector2D Origin = FVector2D(FMath::GridSnap(Request.Anchor.X, 100.0), FMath::GridSnap(Request.Anchor.Y, 100.0)) - Half;
	const int32 Layer = AllocateLayer(FBox2D(Origin - FVector2D(400.f), Origin + T->Footprint + FVector2D(400.f)));
	if (Layer == INDEX_NONE)
	{
		UE_LOG(LogMTAInterior, Warning, TEXT("Interior Grid voll (%d Ebenen)"), MaxLayers);
		return nullptr;
	}
	const FVector Location(Origin, InteriorGridDepth - Layer * LayerSpacing);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Name = MakeUniqueObjectName(World->PersistentLevel, AMTAInteriorInstance::StaticClass(), FName(*FString::Printf(TEXT("Interior_%s"), *Request.EntryKey.ToString())));
	AMTAInteriorInstance* Inst = World->SpawnActor<AMTAInteriorInstance>(ResolveInstanceClass(), Location, FRotator::ZeroRotator, Params);
	if (!Inst) { return nullptr; }
	Inst->GridLayer = Layer;
	Inst->ExteriorAnchor = Request.Anchor;
	const FMTARoomTypeDef* RT = Database.FindRoomType(T->RoomType);
	Inst->BuildFrom(Result, *T, Request.EntryKey, Request.District, RT ? RT->Role : FName(TEXT("resident")));
	Inst->SetDormant(true);
	Inst->ApplyDaylight(CurrentHour, WeatherDim);
	Instances.Add(Request.EntryKey, Inst);
	UE_LOG(LogMTAInterior, Verbose, TEXT("Innenraum %s (%s) gebaut: %d Primitive, %d Lichter, %.0f KB"),
		*Request.EntryKey.ToString(), *T->Id.ToString(), Result.Prims.Num(), Result.Lights.Num(), Inst->CostKB);
	OnInteriorBuilt.Broadcast(Inst);
	return Inst;
}

bool UMTAInteriorSubsystem::ReleaseInterior(FName EntryKey)
{
	if (!Instances.Contains(EntryKey) || IsOccupied(EntryKey)) { return false; }
	DestroyInstance(EntryKey);
	return true;
}

void UMTAInteriorSubsystem::DestroyInstance(FName EntryKey)
{
	TObjectPtr<AMTAInteriorInstance> Inst;
	if (!Instances.RemoveAndCopyValue(EntryKey, Inst)) { return; }
	if (Inst)
	{
		for (AMTAInteriorInteractive* Act : Inst->GetInteractives()) { if (Act) { Act->Destroy(); } }
		Inst->Destroy();
	}
	OnInteriorDestroyed.Broadcast(EntryKey);
}

bool UMTAInteriorSubsystem::IsOccupied(FName EntryKey) const
{
	for (const TPair<TWeakObjectPtr<APawn>, FName>& P : PawnInside)
	{
		if (P.Value == EntryKey && P.Key.IsValid()) { return true; }
	}
	return false;
}

float UMTAInteriorSubsystem::GetUsedBudgetKB() const
{
	float Sum = 0.f;
	for (const TPair<FName, TObjectPtr<AMTAInteriorInstance>>& P : Instances) { if (P.Value) { Sum += P.Value->CostKB; } }
	return Sum;
}

void UMTAInteriorSubsystem::RegisterEntry(AMTABuildingEntry* Entry)
{
	if (Entry) { Entries.AddUnique(Entry); }
}

void UMTAInteriorSubsystem::UnregisterEntry(AMTABuildingEntry* Entry)
{
	Entries.Remove(Entry);
}

// ---------------------------------------------------------------------------
// Uebergaenge
// ---------------------------------------------------------------------------

void UMTAInteriorSubsystem::TravelPawn(APawn* Pawn, const FTransform& Dest, TFunction<void()> OnArrive)
{
	UWorld* World = GetWorld();
	if (!World || !Pawn) { return; }
	PawnsInTransit.Add(Pawn);
	LastTravel.Add(Pawn, World->GetTimeSeconds());

	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (PC && PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, FadeOutTime, FLinearColor::Black, true, true);
	}

	TWeakObjectPtr<APawn> WeakPawn(Pawn);
	FTimerHandle Handle;
	const float Delay = (PC && FadeOutTime > 0.f) ? FadeOutTime : 0.01f;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, WeakPawn, Dest, OnArrive]()
	{
		APawn* P = WeakPawn.Get();
		PawnsInTransit.Remove(WeakPawn);
		if (!P) { return; }
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(P->GetRootComponent()))
		{
			// Fahrzeuge ohne Restschwung ankommen lassen
			if (Prim->IsSimulatingPhysics())
			{
				Prim->SetPhysicsLinearVelocity(FVector::ZeroVector);
				Prim->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			}
		}
		P->TeleportTo(Dest.GetLocation(), Dest.Rotator(), false, true);
		if (AController* C = P->GetController()) { C->SetControlRotation(Dest.Rotator()); }
		if (UWorld* W = GetWorld()) { LastTravel.Add(WeakPawn, W->GetTimeSeconds()); }
		if (OnArrive) { OnArrive(); }
		APlayerController* PC2 = Cast<APlayerController>(P->GetController());
		if (PC2 && PC2->PlayerCameraManager)
		{
			PC2->PlayerCameraManager->StartCameraFade(1.f, 0.f, FadeInTime, FLinearColor::Black, true, false);
		}
	}), Delay, false);
}

bool UMTAInteriorSubsystem::IsTravelCoolingDown(const APawn* Pawn) const
{
	const UWorld* World = GetWorld();
	if (!World || !Pawn) { return false; }
	const TWeakObjectPtr<APawn> Key(const_cast<APawn*>(Pawn));
	if (PawnsInTransit.Contains(Key)) { return true; }
	const double* Last = LastTravel.Find(Key);
	return Last && World->GetTimeSeconds() - *Last < TravelCooldown;
}

bool UMTAInteriorSubsystem::EnterInterior(APawn* Pawn, AMTABuildingEntry* Entry, int32 EntranceIndex)
{
	if (!Pawn || !Entry || !Entry->Entrances.IsValidIndex(EntranceIndex) || IsTravelCoolingDown(Pawn)) { return false; }
	AMTAInteriorInstance* Inst = AcquireInterior(Entry->MakeInteriorRequest());
	if (!Inst) { return false; }
	Inst->SourceEntry = Entry;

	FTransform Dest;
	if (!Inst->GetEntranceSpawn(Entry->Entrances[EntranceIndex].Kind, Dest)) { return false; }
	// Fahrzeuge etwas hoeher absetzen (Fahrwerk)
	if (!Pawn->IsA<ACharacter>()) { Dest.AddToTranslation(FVector(0.f, 0.f, 60.f)); }

	Inst->SetDormant(false);
	Inst->ApplyDaylight(CurrentHour, WeatherDim);
	const FName Key = Inst->EntryKey;
	TWeakObjectPtr<APawn> WeakPawn(Pawn);
	TWeakObjectPtr<AMTAInteriorInstance> WeakInst(Inst);
	TravelPawn(Pawn, Dest, [this, WeakPawn, WeakInst, Key]()
	{
		PawnInside.Add(WeakPawn, Key);
		if (WeakInst.IsValid()) { OnInteriorEntered.Broadcast(WeakInst.Get(), WeakPawn.Get()); }
	});
	return true;
}

bool UMTAInteriorSubsystem::ExitInterior(APawn* Pawn, AMTAInteriorInstance* Interior, EMTAEntranceKind Via)
{
	if (!Pawn || !Interior || IsTravelCoolingDown(Pawn)) { return false; }
	FTransform Dest(FRotator::ZeroRotator, Interior->ExteriorAnchor + FVector(0.f, 0.f, 150.f));
	if (AMTABuildingEntry* Entry = Interior->SourceEntry.Get())
	{
		int32 Index = Entry->FindEntranceIndexByKind(Via);
		if (Index == INDEX_NONE) { Index = Entry->FindEntranceIndexByKind(EMTAEntranceKind::Front); }
		if (Index == INDEX_NONE && Entry->Entrances.Num() > 0) { Index = 0; }
		if (Index != INDEX_NONE) { Dest = Entry->GetEntranceExteriorTransform(Index); }
	}
	if (!Pawn->IsA<ACharacter>()) { Dest.AddToTranslation(FVector(0.f, 0.f, 60.f)); }

	TWeakObjectPtr<APawn> WeakPawn(Pawn);
	TWeakObjectPtr<AMTAInteriorInstance> WeakInst(Interior);
	TravelPawn(Pawn, Dest, [this, WeakPawn, WeakInst]()
	{
		PawnInside.Remove(WeakPawn);
		AMTAInteriorInstance* I = WeakInst.Get();
		if (!I) { return; }
		if (!IsOccupied(I->EntryKey)) { I->SetDormant(true); }
		OnInteriorExited.Broadcast(I, WeakPawn.Get());
	});
	return true;
}

AMTAInteriorInstance* UMTAInteriorSubsystem::GetPawnInterior(const APawn* Pawn) const
{
	const FName* Key = PawnInside.Find(TWeakObjectPtr<APawn>(const_cast<APawn*>(Pawn)));
	return Key ? FindInterior(*Key) : nullptr;
}

// ---------------------------------------------------------------------------
// Tick: Tageszeit, Vorwaermen, Aufraeumen
// ---------------------------------------------------------------------------

void UMTAInteriorSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateAccumulator += DeltaTime;
	if (UpdateAccumulator < UpdateInterval) { return; }
	const float Step = UpdateAccumulator;
	UpdateAccumulator = 0.f;
	UpdateHour(Step);
	ApplyDaylightToAll(false);
	UpdateStreaming();
}

void UMTAInteriorSubsystem::SetHourOfDay(float Hour)
{
	bManualHour = true;
	CurrentHour = FMath::Fmod(FMath::Fmod(Hour, 24.f) + 24.f, 24.f);
	ApplyDaylightToAll(true);
}

void UMTAInteriorSubsystem::SetTimeProvider(UObject* Provider)
{
	TimeProvider = (Provider && Provider->GetClass()->ImplementsInterface(UMTATimeProvider::StaticClass())) ? Provider : nullptr;
	bManualHour = false;
}

void UMTAInteriorSubsystem::SetWeatherDim(float Dim)
{
	WeatherDim = FMath::Clamp(Dim, 0.f, 1.f);
	ApplyDaylightToAll(true);
}

void UMTAInteriorSubsystem::UpdateHour(float DeltaTime)
{
	if (bManualHour) { return; }
	if (!TimeProvider.IsValid())
	{
		ProviderSearchCooldown -= DeltaTime;
		if (ProviderSearchCooldown > 0.f) { return; }
		ProviderSearchCooldown = 5.f;
		// Tageszeit kommt aus jedem Actor mit IMTATimeProvider (z. B. AMTATimeOfDay), Datei bleibt unveraendert
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsWithInterface(this, UMTATimeProvider::StaticClass(), Found);
		if (Found.Num() == 0) { return; }
		TimeProvider = Found[0];
	}
	UObject* Obj = TimeProvider.Get();
	const float NewHour = IMTATimeProvider::Execute_GetTimeOfDayHours(Obj);
	const float Prev = CurrentHour;
	CurrentHour = FMath::Fmod(FMath::Fmod(NewHour, 24.f) + 24.f, 24.f);
	// Tageswechsel: Laeden auffuellen
	if (CurrentHour < Prev - 12.f)
	{
		for (const TPair<FName, TObjectPtr<AMTAInteriorInstance>>& P : Instances)
		{
			if (P.Value && P.Value->Shop) { P.Value->Shop->Restock(); }
		}
	}
}

void UMTAInteriorSubsystem::ApplyDaylightToAll(bool bForce)
{
	if (!bForce && FMath::Abs(CurrentHour - AppliedHour) < 0.1f) { return; }
	AppliedHour = CurrentHour;
	for (const TPair<FName, TObjectPtr<AMTAInteriorInstance>>& P : Instances)
	{
		if (P.Value && !P.Value->IsDormant()) { P.Value->ApplyDaylight(CurrentHour, WeatherDim); }
	}
}

APawn* UMTAInteriorSubsystem::GetPlayerPawn() const
{
	return UGameplayStatics::GetPlayerPawn(this, 0);
}

void UMTAInteriorSubsystem::UpdateStreaming()
{
	UWorld* World = GetWorld();
	APawn* Player = GetPlayerPawn();
	if (!World || !Player) { return; }

	// Bezugspunkt: drinnen zaehlt das Aussengebaeude, sonst die Spielerposition
	FVector Ref = Player->GetActorLocation();
	if (const AMTAInteriorInstance* Inside = GetPawnInterior(Player)) { Ref = Inside->ExteriorAnchor; }

	// 1) Vorwaermen: hoechstens ein Bau pro Aktualisierung (Hitch-Begrenzung)
	Entries.RemoveAll([](const TWeakObjectPtr<AMTABuildingEntry>& E) { return !E.IsValid(); });
	AMTABuildingEntry* Best = nullptr;
	double BestDist = FMath::Square((double)PrewarmRadius);
	for (const TWeakObjectPtr<AMTABuildingEntry>& E : Entries)
	{
		AMTABuildingEntry* Entry = E.Get();
		if (!Entry->bPrewarm || Instances.Contains(Entry->BuildingKey)) { continue; }
		const double D = FVector::DistSquared2D(Entry->GetActorLocation(), Ref);
		if (D < BestDist) { BestDist = D; Best = Entry; }
	}
	if (Best && GetUsedBudgetKB() < MemoryBudgetKB)
	{
		if (AMTAInteriorInstance* Inst = AcquireInterior(Best->MakeInteriorRequest())) { Inst->SourceEntry = Best; }
	}

	// 2) Aufraeumen: weit entfernte schlafende Zellen, dann LRU bis Anzahl/Budget passen
	TArray<FName> ToRemove;
	for (const TPair<FName, TObjectPtr<AMTAInteriorInstance>>& P : Instances)
	{
		const AMTAInteriorInstance* I = P.Value;
		if (!I) { ToRemove.Add(P.Key); continue; }
		if (I->IsDormant() && !IsOccupied(P.Key) && FVector::DistSquared2D(I->ExteriorAnchor, Ref) > FMath::Square((double)KeepAliveRadius))
		{
			ToRemove.Add(P.Key);
		}
	}
	for (const FName& K : ToRemove) { DestroyInstance(K); }

	while (Instances.Num() > MaxCachedInteriors || GetUsedBudgetKB() > MemoryBudgetKB)
	{
		FName Oldest = NAME_None;
		double OldestTime = TNumericLimits<double>::Max();
		for (const TPair<FName, TObjectPtr<AMTAInteriorInstance>>& P : Instances)
		{
			if (P.Value && P.Value->IsDormant() && !IsOccupied(P.Key) && P.Value->LastUsedTime < OldestTime)
			{
				OldestTime = P.Value->LastUsedTime;
				Oldest = P.Key;
			}
		}
		if (Oldest.IsNone()) { break; }
		DestroyInstance(Oldest);
	}
}

// ---------------------------------------------------------------------------
// Hacking, Geheimnisse, Verstecke, Schluessel
// ---------------------------------------------------------------------------

void UMTAInteriorSubsystem::SetPriceProvider(UObject* Provider)
{
	PriceProvider = (Provider && Cast<IMTAPriceProvider>(Provider)) ? Provider : nullptr;
}

void UMTAInteriorSubsystem::RequestHack(UObject* Target, int32 Difficulty, FName ContextId)
{
	if (!Target) { return; }
	PendingHacks.Add(Target, Difficulty);
	if (OnHackRequested.IsBound())
	{
		OnHackRequested.Broadcast(Target, Difficulty, ContextId);
		return;
	}
	// Ohne Minispiel (Prototyp): leichte Schloesser gelingen automatisch
	UE_LOG(LogMTAInterior, Log, TEXT("Kein Hack-Minispiel gebunden, automatisch aufgeloest (Stufe %d)"), Difficulty);
	ResolveHack(Target, Difficulty <= 2);
}

void UMTAInteriorSubsystem::ResolveHack(UObject* Target, bool bSuccess)
{
	if (!Target) { return; }
	PendingHacks.Remove(Target);
	if (IMTAHackable* H = Cast<IMTAHackable>(Target)) { H->OnHackResolved(bSuccess, NAME_None); }
}

void UMTAInteriorSubsystem::ReportSecretFound(FName SecretId)
{
	if (SecretId.IsNone() || FoundSecrets.Contains(SecretId)) { return; }
	FoundSecrets.Add(SecretId);
	OnSecretFound.Broadcast(SecretId);
}

void UMTAInteriorSubsystem::NotifyHidingChanged(APawn* Pawn, bool bHidden, AActor* Spot)
{
	OnHidingChanged.Broadcast(Pawn, bHidden, Spot);
}

bool UMTAInteriorSubsystem::PawnHasKey(const APawn* Pawn, FName KeyId)
{
	if (!Pawn || KeyId.IsNone()) { return false; }
	UObject* Candidates[] = { const_cast<APawn*>(Pawn), Pawn->GetController(), Pawn->GetPlayerState() };
	for (UObject* Obj : Candidates)
	{
		if (const IMTAKeyHolder* Holder = Cast<IMTAKeyHolder>(Obj))
		{
			if (Holder->HasKey(KeyId)) { return true; }
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Speichern
// ---------------------------------------------------------------------------

FString UMTAInteriorSubsystem::StateToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Secrets, Opened;
	for (const FName& S : FoundSecrets) { Secrets.Add(MakeShared<FJsonValueString>(S.ToString())); }
	for (const FName& U : Unlocked) { Opened.Add(MakeShared<FJsonValueString>(U.ToString())); }
	Root->SetArrayField(TEXT("secrets"), Secrets);
	Root->SetArrayField(TEXT("unlocked"), Opened);
	FString Out;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Out));
	return Out;
}

bool UMTAInteriorSubsystem::StateFromJson(const FString& Json)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) { return false; }
	FoundSecrets.Reset();
	Unlocked.Reset();
	for (const FName& N : MTAInteriorJson::Names(Root, TEXT("secrets"))) { FoundSecrets.Add(N); }
	for (const FName& N : MTAInteriorJson::Names(Root, TEXT("unlocked"))) { Unlocked.Add(N); }
	return true;
}
