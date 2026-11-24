#include "MTASaveManager.h"

#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "MTACharacterOriginSubsystem.h"
#include "MTADiscoverySubsystem.h"
#include "MTAEconomySubsystem.h"
#include "MTAGarageSubsystem.h"
#include "MTAHeatSubsystem.h"
#include "MTAInteriorSubsystem.h"
#include "MTAInventorySubsystem.h"
#include "MTANPCSubsystem.h"
#include "MTAProgressionSubsystem.h"
#include "MTARelationshipSubsystem.h"
#include "MTASaveGame.h"
#include "MTAStorySubsystem.h"
#include "MTAVehicleCatalog.h"
#include "MTAWorldEventSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTASave, Log, All);

namespace MTASaveImpl
{
	const TCHAR* const GameVersionText = TEXT("0.5.0");

	double GetNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, double Default)
	{
		double V = 0.0;
		return (O.IsValid() && O->TryGetNumberField(Field, V)) ? V : Default;
	}

	FString GetStr(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, const FString& Default = FString())
	{
		FString V;
		return (O.IsValid() && O->TryGetStringField(Field, V)) ? V : Default;
	}

	bool GetBool(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, bool Default)
	{
		bool V = false;
		return (O.IsValid() && O->TryGetBoolField(Field, V)) ? V : Default;
	}

	TSharedPtr<FJsonObject> GetObj(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* P = nullptr;
		if (O.IsValid() && O->TryGetObjectField(Field, P) && P && P->IsValid())
		{
			return *P;
		}
		return nullptr;
	}

	/** Kompakter JSON-Text (Nutzdaten und Zwischenstrings). */
	FString ToCondensed(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	/** Lesbarer JSON-Text (Datei-Kopf). */
	FString ToPretty(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR>> Writer = TJsonWriterFactory<TCHAR>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		if (Text.IsEmpty())
		{
			return nullptr;
		}
		TSharedPtr<FJsonObject> Parsed;
		TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
		{
			return nullptr;
		}
		return Parsed;
	}

	TSharedRef<FJsonObject> TransformToJson(const FTransform& T)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		const FVector L = T.GetLocation();
		const FRotator R = T.Rotator();
		O->SetNumberField(TEXT("x"), L.X);
		O->SetNumberField(TEXT("y"), L.Y);
		O->SetNumberField(TEXT("z"), L.Z);
		O->SetNumberField(TEXT("pitch"), R.Pitch);
		O->SetNumberField(TEXT("yaw"), R.Yaw);
		O->SetNumberField(TEXT("roll"), R.Roll);
		return O;
	}

	FTransform TransformFromJson(const TSharedPtr<FJsonObject>& J)
	{
		const FVector L(GetNum(J, TEXT("x"), 0.0), GetNum(J, TEXT("y"), 0.0), GetNum(J, TEXT("z"), 0.0));
		const FRotator R(GetNum(J, TEXT("pitch"), 0.0), GetNum(J, TEXT("yaw"), 0.0), GetNum(J, TEXT("roll"), 0.0));
		return FTransform(R, L);
	}

	TArray<TSharedPtr<FJsonValue>> VectorToArray(const FVector& V)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		A.Add(MakeShared<FJsonValueNumber>(V.X));
		A.Add(MakeShared<FJsonValueNumber>(V.Y));
		A.Add(MakeShared<FJsonValueNumber>(V.Z));
		return A;
	}

	FVector VectorFromArray(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O.IsValid() && O->TryGetArrayField(Field, A) && A && A->Num() >= 3)
		{
			return FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
		}
		return FVector::ZeroVector;
	}

	TSharedRef<FJsonObject> ContextToJson(const FMTASaveContext& C)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("money"), C.Money);
		O->SetNumberField(TEXT("hour"), C.Hour);
		O->SetNumberField(TEXT("playtime"), C.PlaytimeSeconds);
		O->SetNumberField(TEXT("heat"), C.Heat);
		O->SetStringField(TEXT("district"), C.District.IsNone() ? FString() : C.District.ToString());
		if (C.bHasPlayerTransform)
		{
			O->SetObjectField(TEXT("player"), TransformToJson(C.PlayerTransform));
		}
		if (C.bHasVehicle)
		{
			TSharedRef<FJsonObject> V = TransformToJson(C.VehicleTransform);
			V->SetNumberField(TEXT("health"), C.VehicleHealth);
			V->SetNumberField(TEXT("fuel"), C.VehicleFuel);
			V->SetBoolField(TEXT("inVehicle"), C.bInVehicle);
			O->SetObjectField(TEXT("vehicle"), V);
		}
		TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("label"), C.MissionLabel);
		M->SetBoolField(TEXT("story"), C.bStoryMission);
		M->SetArrayField(TEXT("target"), VectorToArray(C.MissionTarget));
		M->SetNumberField(TEXT("timeLeft"), C.MissionTimeLeft);
		O->SetObjectField(TEXT("mission"), M);
		return O;
	}

	FMTASaveContext ContextFromJson(const TSharedPtr<FJsonObject>& O)
	{
		FMTASaveContext C;
		C.Money = (int32)GetNum(O, TEXT("money"), 0.0);
		C.Hour = (float)GetNum(O, TEXT("hour"), 18.0);
		C.PlaytimeSeconds = (float)GetNum(O, TEXT("playtime"), 0.0);
		C.Heat = (float)GetNum(O, TEXT("heat"), 0.0);
		const FString District = GetStr(O, TEXT("district"));
		C.District = District.IsEmpty() ? NAME_None : FName(*District);
		if (const TSharedPtr<FJsonObject> P = GetObj(O, TEXT("player")))
		{
			C.bHasPlayerTransform = true;
			C.PlayerTransform = TransformFromJson(P);
		}
		if (const TSharedPtr<FJsonObject> V = GetObj(O, TEXT("vehicle")))
		{
			C.bHasVehicle = true;
			C.VehicleTransform = TransformFromJson(V);
			C.VehicleHealth = (float)GetNum(V, TEXT("health"), 100.0);
			C.VehicleFuel = (float)GetNum(V, TEXT("fuel"), 1.0);
			C.bInVehicle = GetBool(V, TEXT("inVehicle"), true);
		}
		if (const TSharedPtr<FJsonObject> M = GetObj(O, TEXT("mission")))
		{
			C.MissionLabel = GetStr(M, TEXT("label"));
			C.bStoryMission = GetBool(M, TEXT("story"), true);
			C.MissionTarget = VectorFromArray(M, TEXT("target"));
			C.MissionTimeLeft = (float)GetNum(M, TEXT("timeLeft"), 0.0);
		}
		return C;
	}

	FString SaveDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames"));
	}
}

using namespace MTASaveImpl;

// ------------------------------------------------------------------------------ Grundlagen

void UMTASaveManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	MigrateLegacySave();
}

UMTASaveManager* UMTASaveManager::Get(const UObject* WorldContext)
{
	if (!GEngine || !WorldContext)
	{
		return nullptr;
	}
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMTASaveManager>() : nullptr;
}

FString UMTASaveManager::SlotToName(int32 Slot)
{
	return Slot == AutosaveSlot ? FString(TEXT("Auto")) : FString::Printf(TEXT("Slot%d"), Slot);
}

int32 UMTASaveManager::NameToSlot(const FString& SlotName)
{
	if (SlotName.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
	{
		return AutosaveSlot;
	}
	FString Digits;
	if (SlotName.StartsWith(TEXT("Slot"), ESearchCase::IgnoreCase))
	{
		Digits = SlotName.Mid(4);
	}
	else if (SlotName.StartsWith(TEXT("MTA6_Slot"), ESearchCase::IgnoreCase))
	{
		Digits = SlotName.Mid(9); // alter Name des einzigen Platzes
	}
	if (!Digits.IsEmpty() && Digits.IsNumeric())
	{
		return FCString::Atoi(*Digits);
	}
	return INDEX_NONE;
}

FString UMTASaveManager::GetSlotFilePath(int32 Slot) const
{
	return SaveDir() / (TEXT("MTA6_") + SlotToName(Slot) + TEXT(".json"));
}

FString UMTASaveManager::ComputeChecksum(const FString& Payload)
{
	const FTCHARToUTF8 Utf8(*Payload);
	return FMD5::HashBytes(reinterpret_cast<const uint8*>(Utf8.Get()), (uint64)Utf8.Length());
}

// ------------------------------------------------------------------------------ Dateiebene

bool UMTASaveManager::ReadSlotFile(const FString& Path, TSharedPtr<FJsonObject>& OutRoot, FString& OutError) const
{
	FString Text;
	if (!IFileManager::Get().FileExists(*Path))
	{
		OutError = TEXT("Datei fehlt");
		return false;
	}
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		OutError = TEXT("Datei nicht lesbar");
		return false;
	}
	const TSharedPtr<FJsonObject> Root = ParseObject(Text);
	if (!Root.IsValid())
	{
		OutError = TEXT("kein gueltiges JSON");
		return false;
	}
	const int32 Schema = (int32)GetNum(Root, TEXT("schema"), 0.0);
	if (Schema < 1)
	{
		OutError = TEXT("kein Spielstand-Format");
		return false;
	}
	// Ab Schema 3 gibt es Nutzdaten und Pruefsumme; neuere Schemata werden nicht geprueft (Laden lehnt sie ab).
	if (Schema >= 3 && Schema <= CurrentSchema)
	{
		FString Payload;
		if (!Root->TryGetStringField(TEXT("payload"), Payload) || Payload.IsEmpty())
		{
			OutError = TEXT("Nutzdaten fehlen");
			return false;
		}
		if (GetStr(Root, TEXT("checksum")) != ComputeChecksum(Payload))
		{
			OutError = TEXT("Pruefsumme stimmt nicht");
			return false;
		}
	}
	OutRoot = Root;
	return true;
}

bool UMTASaveManager::ReadWithFallback(int32 Slot, TSharedPtr<FJsonObject>& OutRoot, bool& bOutFromBackup, FString& OutError) const
{
	bOutFromBackup = false;
	const FString Main = GetSlotFilePath(Slot);
	FString MainErr;
	if (ReadSlotFile(Main, OutRoot, MainErr))
	{
		return true;
	}
	FString BakErr;
	if (ReadSlotFile(Main + TEXT(".bak"), OutRoot, BakErr))
	{
		bOutFromBackup = true;
		OutError = FString::Printf(TEXT("Spielstand war beschädigt (%s); die Sicherung wurde geladen."), *MainErr);
		return true;
	}
	OutError = FString::Printf(TEXT("Spielstand unbrauchbar (%s), Sicherung ebenfalls (%s)."), *MainErr, *BakErr);
	return false;
}

bool UMTASaveManager::WriteSlotFile(int32 Slot, const TSharedRef<FJsonObject>& Root)
{
	IFileManager& FM = IFileManager::Get();
	const FString Main = GetSlotFilePath(Slot);
	const FString Tmp = Main + TEXT(".tmp");
	const FString Bak = Main + TEXT(".bak");
	const FString Corrupt = Main + TEXT(".corrupt");
	FM.MakeDirectory(*FPaths::GetPath(Main), true);

	// 1) Temporaer schreiben und gegenlesen (Pruefsumme)
	if (!FFileHelper::SaveStringToFile(ToPretty(Root), *Tmp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		LastError = TEXT("Speichern fehlgeschlagen: Datei konnte nicht geschrieben werden.");
		return false;
	}
	TSharedPtr<FJsonObject> Check;
	FString CheckErr;
	if (!ReadSlotFile(Tmp, Check, CheckErr))
	{
		FM.Delete(*Tmp, false, true, true);
		LastError = FString::Printf(TEXT("Speichern fehlgeschlagen: Gegenprobe misslungen (%s)."), *CheckErr);
		return false;
	}

	// 2) Bisherige Hauptdatei: gueltig -> neue Sicherung, beschaedigt -> beiseitelegen (gute Sicherung bleibt)
	if (FM.FileExists(*Main))
	{
		TSharedPtr<FJsonObject> Old;
		FString OldErr;
		if (ReadSlotFile(Main, Old, OldErr))
		{
			FM.Delete(*Bak, false, true, true);
			FM.Move(*Bak, *Main, true, true);
		}
		else
		{
			FM.Delete(*Corrupt, false, true, true);
			FM.Move(*Corrupt, *Main, true, true);
		}
	}

	// 3) Neue Datei an die Stelle setzen
	if (!FM.Move(*Main, *Tmp, true, true))
	{
		LastError = TEXT("Speichern fehlgeschlagen: Datei konnte nicht umbenannt werden.");
		return false;
	}
	return true;
}

TSharedRef<FJsonObject> UMTASaveManager::BuildFileRoot(int32 Slot, const FMTASaveContext& Ctx, const TSharedRef<FJsonObject>& Payload) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	const FString PayloadText = ToCondensed(Payload);
	Root->SetNumberField(TEXT("schema"), CurrentSchema);
	Root->SetStringField(TEXT("gameVersion"), GameVersionText);
	Root->SetStringField(TEXT("savedAt"), FDateTime::Now().ToIso8601());
	Root->SetNumberField(TEXT("slot"), Slot);

	TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
	Meta->SetNumberField(TEXT("playtime"), Ctx.PlaytimeSeconds);
	Meta->SetStringField(TEXT("mission"), Ctx.MissionLabel);
	Meta->SetStringField(TEXT("district"), Ctx.District.IsNone() ? FString() : Ctx.District.ToString());
	Meta->SetNumberField(TEXT("money"), Ctx.Money);
	Meta->SetNumberField(TEXT("hour"), Ctx.Hour);
	Root->SetObjectField(TEXT("meta"), Meta);

	Root->SetStringField(TEXT("checksum"), ComputeChecksum(PayloadText));
	Root->SetStringField(TEXT("payload"), PayloadText);
	return Root;
}

// ------------------------------------------------------------------------------ Migration

void UMTASaveManager::MigrateV1ToV2(const TSharedPtr<FJsonObject>& Root)
{
	// v1 hatte keinen Story-Zustand; v2 fuegt ihn hinzu (fehlt = Story zuruecksetzen). Struktur sonst gleich.
	Root->SetNumberField(TEXT("schema"), 2);
}

bool UMTASaveManager::MigrateV2ToV3(TSharedPtr<FJsonObject>& InOutRoot, FString& OutError)
{
	const TSharedPtr<FJsonObject> Old = InOutRoot;
	if (!Old.IsValid())
	{
		OutError = TEXT("Migration v2 zu v3: leere Datei.");
		return false;
	}

	// Nutzdaten aus den Feldern des alten Spielstands zusammensetzen
	TSharedRef<FJsonObject> Ctx = MakeShared<FJsonObject>();
	const double Money = GetNum(Old, TEXT("money"), 0.0);
	Ctx->SetNumberField(TEXT("money"), Money);
	Ctx->SetNumberField(TEXT("hour"), GetNum(Old, TEXT("hour"), 18.0));
	Ctx->SetNumberField(TEXT("playtime"), 0.0);
	Ctx->SetNumberField(TEXT("heat"), GetNum(Old, TEXT("heat"), 0.0));
	Ctx->SetStringField(TEXT("district"), TEXT(""));
	if (TSharedPtr<FJsonObject> Veh = GetObj(Old, TEXT("vehicle")))
	{
		Veh->SetNumberField(TEXT("health"), GetNum(Old, TEXT("vehicleHealth"), 100.0));
		Veh->SetNumberField(TEXT("fuel"), 1.0);
		Veh->SetBoolField(TEXT("inVehicle"), true);
		Ctx->SetObjectField(TEXT("vehicle"), Veh);
	}
	TSharedRef<FJsonObject> Mission = MakeShared<FJsonObject>();
	Mission->SetStringField(TEXT("label"), TEXT(""));
	Mission->SetBoolField(TEXT("story"), true);
	Mission->SetArrayField(TEXT("target"), VectorToArray(VectorFromArray(Old, TEXT("missionTarget"))));
	Mission->SetNumberField(TEXT("timeLeft"), GetNum(Old, TEXT("missionTimeLeft"), 0.0));
	Ctx->SetObjectField(TEXT("mission"), Mission);

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetObjectField(TEXT("context"), Ctx);
	const FString StoryText = GetStr(Old, TEXT("storyJson"));
	if (const TSharedPtr<FJsonObject> Story = ParseObject(StoryText))
	{
		Payload->SetObjectField(TEXT("story"), Story);
	}
	// Alle uebrigen Felder fehlen absichtlich: das Laden laesst diese Subsysteme unveraendert.

	const FString PayloadText = ToCondensed(Payload);
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema"), 3);
	Root->SetStringField(TEXT("gameVersion"), TEXT("legacy"));
	const FString SavedAt = GetStr(Old, TEXT("savedAt"), FDateTime::Now().ToIso8601());
	Root->SetStringField(TEXT("savedAt"), SavedAt);
	Root->SetNumberField(TEXT("migratedFrom"), GetNum(Old, TEXT("schema"), 2.0));

	TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
	Meta->SetNumberField(TEXT("playtime"), 0.0);
	Meta->SetStringField(TEXT("mission"), TEXT(""));
	Meta->SetStringField(TEXT("district"), TEXT(""));
	Meta->SetNumberField(TEXT("money"), Money);
	Meta->SetNumberField(TEXT("hour"), GetNum(Old, TEXT("hour"), 18.0));
	Root->SetObjectField(TEXT("meta"), Meta);
	Root->SetStringField(TEXT("checksum"), ComputeChecksum(PayloadText));
	Root->SetStringField(TEXT("payload"), PayloadText);
	InOutRoot = Root;
	return true;
}

bool UMTASaveManager::MigrateRoot(TSharedPtr<FJsonObject>& InOutRoot, FString& OutError)
{
	if (!InOutRoot.IsValid())
	{
		OutError = TEXT("Leerer Spielstand.");
		return false;
	}
	int32 Schema = (int32)GetNum(InOutRoot, TEXT("schema"), 0.0);
	if (Schema > CurrentSchema)
	{
		OutError = TEXT("Der Spielstand stammt aus einer neueren Version des Spiels und kann nicht geladen werden.");
		return false;
	}
	// Migrationskette: jede Stufe hebt genau eine Version an.
	while (Schema < CurrentSchema)
	{
		switch (Schema)
		{
		case 1:
			MigrateV1ToV2(InOutRoot);
			break;
		case 2:
			if (!MigrateV2ToV3(InOutRoot, OutError))
			{
				return false;
			}
			break;
		default:
			OutError = FString::Printf(TEXT("Unbekanntes Spielstand-Format (Schema %d)."), Schema);
			return false;
		}
		const int32 Next = (int32)GetNum(InOutRoot, TEXT("schema"), 0.0);
		if (Next <= Schema)
		{
			OutError = TEXT("Migration ohne Fortschritt abgebrochen.");
			return false;
		}
		Schema = Next;
	}
	return true;
}

TSharedRef<FJsonObject> UMTASaveManager::LegacyToV2Json(const UMTASaveGame& Legacy)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("schema"), Legacy.Version == 1 ? 1 : 2);
	O->SetNumberField(TEXT("money"), Legacy.Money);
	O->SetNumberField(TEXT("heat"), Legacy.Heat);
	O->SetNumberField(TEXT("hour"), Legacy.Hour);
	O->SetObjectField(TEXT("vehicle"), TransformToJson(Legacy.VehicleTransform));
	O->SetNumberField(TEXT("vehicleHealth"), Legacy.VehicleHealth);
	O->SetArrayField(TEXT("missionTarget"), VectorToArray(Legacy.MissionTarget));
	O->SetNumberField(TEXT("missionTimeLeft"), Legacy.MissionTimeLeft);
	if (!Legacy.StoryJson.IsEmpty())
	{
		O->SetStringField(TEXT("storyJson"), Legacy.StoryJson);
	}
	O->SetStringField(TEXT("savedAt"), FDateTime::Now().ToIso8601());
	return O;
}

bool UMTASaveManager::MigrateLegacySave()
{
	const FString Marker = SaveDir() / TEXT("MTA6_legacy_migrated.flag");
	IFileManager& FM = IFileManager::Get();
	if (FM.FileExists(*Marker))
	{
		return false;
	}
	if (!UGameplayStatics::DoesSaveGameExist(UMTASaveGame::SlotName(), UMTASaveGame::UserIndex))
	{
		return false;
	}
	const FString Slot1 = GetSlotFilePath(1);
	if (FM.FileExists(*Slot1) || FM.FileExists(*(Slot1 + TEXT(".bak"))))
	{
		return false; // Platz 1 ist schon belegt: nichts ueberschreiben
	}
	const UMTASaveGame* Legacy = Cast<UMTASaveGame>(UGameplayStatics::LoadGameFromSlot(UMTASaveGame::SlotName(), UMTASaveGame::UserIndex));
	if (!Legacy || (Legacy->Version != 1 && Legacy->Version != 2))
	{
		UE_LOG(LogMTASave, Warning, TEXT("Alter Spielstand unbrauchbar, Uebernahme uebersprungen."));
		return false;
	}

	TSharedRef<FJsonObject> V2 = LegacyToV2Json(*Legacy);
	const FString LegacyFile = SaveDir() / (FString(UMTASaveGame::SlotName()) + TEXT(".sav"));
	const FDateTime Stamp = FM.GetTimeStamp(*LegacyFile);
	if (Stamp > FDateTime(0))
	{
		V2->SetStringField(TEXT("savedAt"), Stamp.ToIso8601());
	}
	TSharedPtr<FJsonObject> Migrated = V2;
	FString Error;
	if (!MigrateRoot(Migrated, Error) || !WriteSlotFile(1, Migrated.ToSharedRef()))
	{
		UE_LOG(LogMTASave, Warning, TEXT("Uebernahme des alten Spielstands fehlgeschlagen: %s %s"), *Error, *LastError);
		return false;
	}
	FFileHelper::SaveStringToFile(TEXT("migriert"), *Marker);
	UE_LOG(LogMTASave, Display, TEXT("Alter Spielstand (v%d) in Platz 1 uebernommen."), Legacy->Version);
	return true;
}

// ------------------------------------------------------------------------------ Zustand sammeln

TSharedRef<FJsonObject> UMTASaveManager::BuildPayload(const FMTASaveContext& Ctx) const
{
	TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
	P->SetObjectField(TEXT("context"), ContextToJson(Ctx));

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return P;
	}
	auto AddText = [&P](const TCHAR* Name, const FString& Text)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		if (const TSharedPtr<FJsonObject> Parsed = ParseObject(Text))
		{
			P->SetObjectField(Name, Parsed);
		}
		else
		{
			UE_LOG(LogMTASave, Warning, TEXT("Zustand '%s' ist kein gueltiges JSON und wird nicht gespeichert."), Name);
		}
	};

	if (const UMTAProgressionSubsystem* Prog = GI->GetSubsystem<UMTAProgressionSubsystem>())
	{
		P->SetObjectField(TEXT("progression"), Prog->ExportBundleJson());
	}
	if (const UMTAStorySubsystem* Story = GI->GetSubsystem<UMTAStorySubsystem>())
	{
		AddText(TEXT("story"), Story->StateToJson());
	}
	if (const UMTANPCSubsystem* Npc = GI->GetSubsystem<UMTANPCSubsystem>())
	{
		AddText(TEXT("npc"), Npc->SaveStateToJson());
	}
	if (const UMTADiscoverySubsystem* Disc = GI->GetSubsystem<UMTADiscoverySubsystem>())
	{
		AddText(TEXT("discovery"), Disc->ToJson());
	}
	if (const UMTAWorldEventSubsystem* Events = GI->GetSubsystem<UMTAWorldEventSubsystem>())
	{
		AddText(TEXT("worldEvents"), Events->ToJson());
	}

	if (const UWorld* World = GI->GetWorld())
	{
		if (const UMTAInteriorSubsystem* Interior = World->GetSubsystem<UMTAInteriorSubsystem>())
		{
			AddText(TEXT("interior"), Interior->StateToJson());
		}
		if (const UMTAHeatSubsystem* Heat = World->GetSubsystem<UMTAHeatSubsystem>())
		{
			const FMTAPlayerHeat State = Heat->GetPlayerHeatState(0);
			TSharedRef<FJsonObject> H = MakeShared<FJsonObject>();
			H->SetNumberField(TEXT("global"), State.GlobalHeat);
			H->SetStringField(TEXT("current"), State.CurrentDistrict.IsNone() ? FString() : State.CurrentDistrict.ToString());
			TSharedRef<FJsonObject> Districts = MakeShared<FJsonObject>();
			for (const TPair<FName, float>& Pair : State.DistrictHeat)
			{
				Districts->SetNumberField(Pair.Key.ToString(), Pair.Value);
			}
			H->SetObjectField(TEXT("districts"), Districts);
			P->SetObjectField(TEXT("heat"), H);
		}
	}
	return P;
}

bool UMTASaveManager::ApplyPayload(const TSharedPtr<FJsonObject>& P, FMTASaveContext& OutCtx, TArray<FString>& OutWarnings)
{
	if (!P.IsValid())
	{
		OutWarnings.Add(TEXT("Nutzdaten fehlen."));
		return false;
	}
	OutCtx = ContextFromJson(GetObj(P, TEXT("context")));

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		OutWarnings.Add(TEXT("Kein GameInstance."));
		return false;
	}
	auto Warn = [&OutWarnings](const TCHAR* Name, const TCHAR* Text)
	{
		OutWarnings.Add(FString::Printf(TEXT("%s: %s"), Name, Text));
		UE_LOG(LogMTASave, Warning, TEXT("Laden: %s: %s"), Name, Text);
	};

	// Reihenfolge: Fortschritt (Herkunft, Wirtschaft, Skills, Garage, Inventar) -> Story -> NPC -> Entdeckungen -> Ereignisse -> Welt
	if (UMTAProgressionSubsystem* Prog = GI->GetSubsystem<UMTAProgressionSubsystem>())
	{
		if (const TSharedPtr<FJsonObject> Bundle = GetObj(P, TEXT("progression")))
		{
			if (!Prog->ImportBundleJson(Bundle))
			{
				Warn(TEXT("progression"), TEXT("Teile des Fortschritts konnten nicht wiederhergestellt werden"));
			}
		}
	}
	if (UMTAStorySubsystem* Story = GI->GetSubsystem<UMTAStorySubsystem>())
	{
		const TSharedPtr<FJsonObject> S = GetObj(P, TEXT("story"));
		if (!S.IsValid() || !Story->StateFromJson(ToCondensed(S.ToSharedRef())))
		{
			Story->ResetState(); // alter Spielstand ohne Story oder unlesbar: wie bisher zuruecksetzen
			if (S.IsValid())
			{
				Warn(TEXT("story"), TEXT("Story-Zustand unlesbar, zurueckgesetzt"));
			}
		}
	}
	if (UMTANPCSubsystem* Npc = GI->GetSubsystem<UMTANPCSubsystem>())
	{
		if (const TSharedPtr<FJsonObject> N = GetObj(P, TEXT("npc")))
		{
			if (!Npc->LoadStateFromJson(ToCondensed(N.ToSharedRef())))
			{
				Warn(TEXT("npc"), TEXT("NPC-Zustand (Beziehungen, Geruechte, Dialoge) unvollstaendig"));
			}
		}
	}
	if (UMTADiscoverySubsystem* Disc = GI->GetSubsystem<UMTADiscoverySubsystem>())
	{
		const TSharedPtr<FJsonObject> D = GetObj(P, TEXT("discovery"));
		if (!D.IsValid() || !Disc->FromJson(ToCondensed(D.ToSharedRef())))
		{
			Disc->ResetProgress();
			if (D.IsValid())
			{
				Warn(TEXT("discovery"), TEXT("Entdeckungen unlesbar, zurueckgesetzt"));
			}
		}
	}
	if (UMTAWorldEventSubsystem* Events = GI->GetSubsystem<UMTAWorldEventSubsystem>())
	{
		if (const TSharedPtr<FJsonObject> E = GetObj(P, TEXT("worldEvents")))
		{
			if (!Events->FromJson(ToCondensed(E.ToSharedRef())))
			{
				Warn(TEXT("worldEvents"), TEXT("Weltereignisse unlesbar"));
			}
		}
	}

	if (UWorld* World = GI->GetWorld())
	{
		if (UMTAInteriorSubsystem* Interior = World->GetSubsystem<UMTAInteriorSubsystem>())
		{
			if (const TSharedPtr<FJsonObject> I = GetObj(P, TEXT("interior")))
			{
				if (!Interior->StateFromJson(ToCondensed(I.ToSharedRef())))
				{
					Warn(TEXT("interior"), TEXT("Innenraum-Zustand unlesbar"));
				}
			}
		}
		if (UMTAHeatSubsystem* Heat = World->GetSubsystem<UMTAHeatSubsystem>())
		{
			if (const TSharedPtr<FJsonObject> H = GetObj(P, TEXT("heat")))
			{
				// Bezirks-Ruf absolut setzen: es gibt nur AddPlayerHeat, also ueber die Differenz.
				TMap<FName, float> Target;
				if (const TSharedPtr<FJsonObject> D = GetObj(H, TEXT("districts")))
				{
					for (const auto& Pair : D->Values)
					{
						double V = 0.0;
						if (Pair.Value.IsValid() && Pair.Value->TryGetNumber(V))
						{
							Target.Add(FName(*FString(Pair.Key.ToView())), (float)V);
						}
					}
				}
				const FMTAPlayerHeat Cur = Heat->GetPlayerHeatState(0);
				for (const TPair<FName, float>& Pair : Cur.DistrictHeat)
				{
					if (!Target.Contains(Pair.Key))
					{
						Target.Add(Pair.Key, 0.f);
					}
				}
				for (const TPair<FName, float>& Pair : Target)
				{
					const float Delta = Pair.Value - Heat->GetDistrictHeat(0, Pair.Key);
					if (!FMath::IsNearlyZero(Delta, 0.001f))
					{
						Heat->AddPlayerHeat(0, Delta, Pair.Key);
					}
				}
				Heat->SetGlobalHeat(0, (float)GetNum(H, TEXT("global"), 0.0));
			}
		}
	}
	return true;
}

// ------------------------------------------------------------------------------ Oeffentliche API

bool UMTASaveManager::SaveToSlot(int32 Slot, const FMTASaveContext& Ctx)
{
	LastError.Reset();
	if (Slot < 0)
	{
		LastError = TEXT("Ungültiger Speicherplatz.");
		return false;
	}
	const TSharedRef<FJsonObject> Payload = BuildPayload(Ctx);
	const TSharedRef<FJsonObject> Root = BuildFileRoot(Slot, Ctx, Payload);
	const bool bOk = WriteSlotFile(Slot, Root);
	UE_LOG(LogMTASave, Display, TEXT("Speichern Platz %d (%s): %s"), Slot, *SlotToName(Slot), bOk ? TEXT("ok") : *LastError);
	return bOk;
}

bool UMTASaveManager::LoadFromSlot(int32 Slot, FMTASaveContext& OutCtx)
{
	LastError.Reset();
	LastWarnings.Reset();
	bLastLoadUsedBackup = false;

	IFileManager& FM = IFileManager::Get();
	const FString Main = GetSlotFilePath(Slot);
	const FString Bak = Main + TEXT(".bak");
	if (!FM.FileExists(*Main) && !FM.FileExists(*Bak))
	{
		LastError = TEXT("In diesem Platz gibt es keinen Spielstand.");
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	bool bFromBackup = false;
	FString ReadError;
	if (!ReadWithFallback(Slot, Root, bFromBackup, ReadError))
	{
		LastError = ReadError;
		UE_LOG(LogMTASave, Warning, TEXT("Laden Platz %d: %s"), Slot, *LastError);
		return false;
	}

	FString MigrateError;
	if (!MigrateRoot(Root, MigrateError))
	{
		LastError = MigrateError;
		UE_LOG(LogMTASave, Warning, TEXT("Laden Platz %d: %s"), Slot, *LastError);
		return false;
	}
	const TSharedPtr<FJsonObject> Payload = ParseObject(GetStr(Root, TEXT("payload")));
	if (!Payload.IsValid())
	{
		LastError = TEXT("Spielstand unbrauchbar: Nutzdaten nicht lesbar.");
		return false;
	}

	if (!ApplyPayload(Payload, OutCtx, LastWarnings))
	{
		LastError = LastWarnings.Num() > 0 ? LastWarnings.Last() : TEXT("Spielstand konnte nicht angewendet werden.");
		return false;
	}

	if (bFromBackup)
	{
		bLastLoadUsedBackup = true;
		LastError = ReadError; // Meldung fuer den Spieler
		// Hauptdatei heilen: beschaedigte Datei beiseitelegen, Sicherung als Hauptdatei kopieren
		if (FM.FileExists(*Main))
		{
			const FString Corrupt = Main + TEXT(".corrupt");
			FM.Delete(*Corrupt, false, true, true);
			FM.Move(*Corrupt, *Main, true, true);
		}
		FPlatformFileManager::Get().GetPlatformFile().CopyFile(*Main, *Bak);
	}
	UE_LOG(LogMTASave, Display, TEXT("Laden Platz %d (%s): ok%s, %d Warnungen"), Slot, *SlotToName(Slot),
		bFromBackup ? TEXT(" (aus Sicherung)") : TEXT(""), LastWarnings.Num());
	return true;
}

bool UMTASaveManager::Autosave(const FMTASaveContext& Ctx)
{
	LastAutosaveRealTime = FPlatformTime::Seconds();
	return SaveToSlot(AutosaveSlot, Ctx);
}

bool UMTASaveManager::TryAutosave(const FMTASaveContext& Ctx, float MinIntervalSeconds)
{
	if (FPlatformTime::Seconds() - LastAutosaveRealTime < (double)MinIntervalSeconds)
	{
		return false;
	}
	return Autosave(Ctx);
}

bool UMTASaveManager::DoesSlotExist(int32 Slot) const
{
	IFileManager& FM = IFileManager::Get();
	const FString Main = GetSlotFilePath(Slot);
	return FM.FileExists(*Main) || FM.FileExists(*(Main + TEXT(".bak")));
}

bool UMTASaveManager::DeleteSlot(int32 Slot)
{
	IFileManager& FM = IFileManager::Get();
	const FString Main = GetSlotFilePath(Slot);
	bool bAny = false;
	for (const TCHAR* Suffix : { TEXT(""), TEXT(".bak"), TEXT(".tmp"), TEXT(".corrupt") })
	{
		const FString Path = Main + Suffix;
		if (FM.FileExists(*Path))
		{
			bAny |= FM.Delete(*Path, false, true, true);
		}
	}
	return bAny;
}

bool UMTASaveManager::ReadSlotMeta(int32 Slot, FMTASaveSlotMeta& OutMeta) const
{
	OutMeta = FMTASaveSlotMeta();
	OutMeta.Slot = Slot;
	OutMeta.SlotName = SlotToName(Slot);
	if (!DoesSlotExist(Slot))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	FString Error;
	bool bFromBackup = false;
	if (!ReadWithFallback(Slot, Root, bFromBackup, Error))
	{
		OutMeta.bCorrupt = true;
		return true;
	}
	OutMeta.bFromBackup = bFromBackup;
	OutMeta.Schema = (int32)GetNum(Root, TEXT("schema"), 0.0);
	if (OutMeta.Schema > CurrentSchema)
	{
		OutMeta.bTooNew = true;
		OutMeta.bExists = true;
		return true;
	}
	if (!MigrateRoot(Root, Error))
	{
		OutMeta.bCorrupt = true;
		return true;
	}
	OutMeta.bExists = true;
	OutMeta.GameVersion = GetStr(Root, TEXT("gameVersion"));
	FDateTime Parsed;
	if (FDateTime::ParseIso8601(*GetStr(Root, TEXT("savedAt")), Parsed))
	{
		OutMeta.SavedAt = Parsed;
	}
	if (const TSharedPtr<FJsonObject> Meta = GetObj(Root, TEXT("meta")))
	{
		OutMeta.PlaytimeSeconds = (float)GetNum(Meta, TEXT("playtime"), 0.0);
		OutMeta.Mission = GetStr(Meta, TEXT("mission"));
		OutMeta.District = GetStr(Meta, TEXT("district"));
		OutMeta.Money = (int32)GetNum(Meta, TEXT("money"), 0.0);
		OutMeta.Hour = (float)GetNum(Meta, TEXT("hour"), 0.0);
	}
	return true;
}

void UMTASaveManager::GetSlotMetas(TArray<FMTASaveSlotMeta>& OutMetas) const
{
	OutMetas.Reset();
	for (int32 Slot = AutosaveSlot; Slot <= NumManualSlots; ++Slot)
	{
		FMTASaveSlotMeta Meta;
		ReadSlotMeta(Slot, Meta);
		OutMetas.Add(Meta);
	}
}

void UMTASaveManager::GetSlotInfos(TArray<FMTASaveSlotInfo>& OutInfos) const
{
	OutInfos.Reset();
	TArray<FMTASaveSlotMeta> Metas;
	GetSlotMetas(Metas);
	for (const FMTASaveSlotMeta& Meta : Metas)
	{
		FMTASaveSlotInfo Info;
		Info.SlotName = Meta.SlotName;
		Info.bAutosave = (Meta.Slot == AutosaveSlot);
		Info.DisplayName = Info.bAutosave ? FString(TEXT("Autosave")) : FString::Printf(TEXT("Slot %d"), Meta.Slot);
		// Beschaedigte oder zu neue Plaetze sind nicht ladbar (bExists = false), tragen aber einen Hinweis im Namen.
		Info.bExists = Meta.bExists && !Meta.bTooNew && !Meta.bCorrupt;
		if (Meta.bCorrupt)
		{
			Info.DisplayName += TEXT(" (beschädigt)");
		}
		else if (Meta.bTooNew)
		{
			Info.DisplayName += TEXT(" (neuere Version)");
		}
		if (Info.bExists)
		{
			Info.PlaytimeSeconds = Meta.PlaytimeSeconds;
			Info.Mission = Meta.Mission;
			Info.Location = FString::Printf(TEXT("%s  |  $%d"), Meta.District.IsEmpty() ? TEXT("-") : *Meta.District, Meta.Money);
			Info.SavedAt = Meta.SavedAt;
		}
		OutInfos.Add(Info);
	}
}

int32 UMTASaveManager::GetNewestSlot() const
{
	int32 Best = INDEX_NONE;
	FDateTime BestTime(0);
	TArray<FMTASaveSlotMeta> Metas;
	GetSlotMetas(Metas);
	for (const FMTASaveSlotMeta& Meta : Metas)
	{
		if (Meta.bExists && !Meta.bTooNew && !Meta.bCorrupt && (Best == INDEX_NONE || Meta.SavedAt > BestTime))
		{
			Best = Meta.Slot;
			BestTime = Meta.SavedAt;
		}
	}
	return Best;
}

// ------------------------------------------------------------------------------ Selbsttest

FString UMTASaveManager::RunSaveSelfTest()
{
	FString R = TEXT("=== SaveManager-Selbsttest ===\n");
	int32 Fail = 0;
	int32 Pass = 0;
	auto Check = [&](const FString& Name, bool bOk, const FString& Detail = FString())
	{
		if (bOk)
		{
			++Pass;
			R += FString::Printf(TEXT("  OK      %s\n"), *Name);
		}
		else
		{
			++Fail;
			R += FString::Printf(TEXT("  FEHLER  %s %s\n"), *Name, *Detail);
		}
	};

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return TEXT("SaveManager-Selbsttest: kein GameInstance.");
	}
	UWorld* World = GI->GetWorld();
	UMTAEconomySubsystem* Eco = GI->GetSubsystem<UMTAEconomySubsystem>();
	UMTAProgressionSubsystem* Prog = GI->GetSubsystem<UMTAProgressionSubsystem>();
	UMTAInventorySubsystem* Inv = GI->GetSubsystem<UMTAInventorySubsystem>();
	UMTAGarageSubsystem* Garage = GI->GetSubsystem<UMTAGarageSubsystem>();
	UMTAVehicleCatalog* Catalog = GI->GetSubsystem<UMTAVehicleCatalog>();
	UMTAStorySubsystem* Story = GI->GetSubsystem<UMTAStorySubsystem>();
	UMTANPCSubsystem* Npc = GI->GetSubsystem<UMTANPCSubsystem>();
	UMTARelationshipSubsystem* Rel = GI->GetSubsystem<UMTARelationshipSubsystem>();
	UMTADiscoverySubsystem* Disc = GI->GetSubsystem<UMTADiscoverySubsystem>();
	UMTAHeatSubsystem* Heat = World ? World->GetSubsystem<UMTAHeatSubsystem>() : nullptr;

	R += FString::Printf(TEXT("Subsysteme: Wirtschaft %d, Fortschritt %d, Inventar %d, Garage %d, Katalog %d, Story %d, NPC %d, Beziehungen %d, Entdeckungen %d, Fahndung %d\n"),
		Eco != nullptr, Prog != nullptr, Inv != nullptr, Garage != nullptr, Catalog != nullptr, Story != nullptr, Npc != nullptr, Rel != nullptr, Disc != nullptr, Heat != nullptr);

	const int32 Slot = SelfTestSlot;
	const FString Main = GetSlotFilePath(Slot);
	const FString Bak = Main + TEXT(".bak");
	IFileManager& FM = IFileManager::Get();
	DeleteSlot(Slot);

	// Urspruenglichen Zustand sichern, um ihn am Ende wiederherzustellen
	FMTASaveContext OrigCtx;
	const TSharedRef<FJsonObject> Original = BuildPayload(OrigCtx);

	auto SetStoryFlag = [GI](const FString& Value)
	{
		MTAProgression::PatchStoryState(GI, [&Value](const TSharedPtr<FJsonObject>& State)
		{
			const TSharedPtr<FJsonObject>* Existing = nullptr;
			if (State->TryGetObjectField(TEXT("flags"), Existing) && Existing && Existing->IsValid())
			{
				(*Existing)->SetStringField(TEXT("selftest.flag"), Value);
			}
			else
			{
				TSharedRef<FJsonObject> Flags = MakeShared<FJsonObject>();
				Flags->SetStringField(TEXT("selftest.flag"), Value);
				State->SetObjectField(TEXT("flags"), Flags);
			}
		});
	};
	auto GetStoryFlag = [Story]() -> FString
	{
		if (!Story)
		{
			return FString();
		}
		const TSharedPtr<FJsonObject> State = ParseObject(Story->StateToJson());
		return GetStr(GetObj(State, TEXT("flags")), TEXT("selftest.flag"));
	};

	// Auswahl der Testdaten aus den echten Katalogen
	FName ItemId;
	if (Inv)
	{
		for (const FMTAItemDef& Def : Inv->GetAllItemDefs())
		{
			if (!Def.bStoryItem)
			{
				ItemId = Def.Id;
				break;
			}
		}
	}
	FName ModelId;
	if (Catalog)
	{
		const TArray<FMTAVehicleModel> Models = Catalog->GetAllModels();
		if (Models.Num() > 0)
		{
			ModelId = Models[0].Id;
		}
	}
	FName NpcId;
	if (Npc)
	{
		const TArray<FName> Ids = Npc->GetAllNPCIds();
		if (Ids.Num() > 0)
		{
			NpcId = Ids[0];
		}
	}
	FName DiscId;
	if (Disc)
	{
		for (const FMTADiscoveryDef& Def : Disc->GetAllDiscoveries())
		{
			if (!Disc->IsDiscovered(Def.Id))
			{
				DiscId = Def.Id;
				break;
			}
		}
	}

	// ---- Zustand A herstellen ------------------------------------------------------------------
	if (Eco) { Eco->AddMoney(7777, EMTATransactionCategory::Other, TEXT("Selbsttest A")); }
	if (Prog) { Prog->AddPerkPoints(5); Prog->AddXp(150); }
	if (Inv && !ItemId.IsNone()) { Inv->AddItem(ItemId, 2, EMTAInventoryContainer::Personal); }
	if (Garage && !ModelId.IsNone()) { Garage->AddVehicle(ModelId, TEXT("core"), false); }
	if (Story) { SetStoryFlag(TEXT("A")); }
	if (Rel && !NpcId.IsNone()) { Rel->ModifyPlayerTrust(NpcId, 0.2f); }
	bool bDiscForced = false;
	if (Disc && !DiscId.IsNone()) { bDiscForced = Disc->ForceDiscover(DiscId, false); }
	if (Heat) { Heat->AddPlayerHeat(0, 1.25f, TEXT("core")); }

	FMTASaveContext CtxA;
	CtxA.Money = 4321;
	CtxA.Hour = 7.25f;
	CtxA.PlaytimeSeconds = 9876.5f;
	CtxA.bHasPlayerTransform = true;
	CtxA.PlayerTransform = FTransform(FRotator(0.f, 30.f, 0.f), FVector(1.f, 2.f, 3.f));
	CtxA.bHasVehicle = true;
	CtxA.bInVehicle = false;
	CtxA.VehicleTransform = FTransform(FRotator(0.f, 45.f, 0.f), FVector(1000.f, -2000.f, 300.f));
	CtxA.VehicleHealth = 55.f;
	CtxA.VehicleFuel = 0.4f;
	CtxA.Heat = 2.5f;
	CtxA.District = TEXT("saltmarket");
	CtxA.MissionLabel = TEXT("Selbsttest-Mission");
	CtxA.bStoryMission = false;
	CtxA.MissionTarget = FVector(10.f, 20.f, 30.f);
	CtxA.MissionTimeLeft = 77.f;

	const int64 MoneyA = Eco ? Eco->GetMoney() : 0;
	const int32 PerkA = Prog ? Prog->GetPerkPoints() : 0;
	const int32 ItemCountA = (Inv && !ItemId.IsNone()) ? Inv->GetItemCount(ItemId) : 0;
	const int32 VehiclesA = Garage ? Garage->GetVehicles().Num() : 0;
	const float TrustA = (Rel && !NpcId.IsNone()) ? Rel->GetPlayerTrust(NpcId) : 0.f;
	const float DistrictHeatA = Heat ? Heat->GetDistrictHeat(0, TEXT("core")) : 0.f;

	const TSharedRef<FJsonObject> SnapshotA = BuildPayload(CtxA);

	// ---- Speichern -------------------------------------------------------------------------------
	R += TEXT("-- Speichern und Laden\n");
	Check(TEXT("Speichern in Testplatz"), SaveToSlot(Slot, CtxA), GetLastError());
	Check(TEXT("Datei vorhanden"), FM.FileExists(*Main));
	FMTASaveSlotMeta Meta;
	ReadSlotMeta(Slot, Meta);
	Check(TEXT("Metadaten lesbar"), Meta.bExists && !Meta.bCorrupt);
	Check(TEXT("Metadaten: Geld/Mission/Bezirk/Spielzeit"),
		Meta.Money == 4321 && Meta.Mission == TEXT("Selbsttest-Mission") && Meta.District == TEXT("saltmarket") && FMath::IsNearlyEqual(Meta.PlaytimeSeconds, 9876.5f, 0.01f));
	Check(TEXT("Metadaten: Schema und Version"), Meta.Schema == CurrentSchema && !Meta.GameVersion.IsEmpty());
	TArray<FMTASaveSlotInfo> Infos;
	GetSlotInfos(Infos);
	Check(TEXT("Slot-Liste: Autosave + 3 Plaetze"), Infos.Num() == NumManualSlots + 1 && Infos[0].bAutosave);

	// ---- Zustand B (alles anders) ----------------------------------------------------------------
	if (Eco) { Eco->AddMoney(100000, EMTATransactionCategory::Other, TEXT("Selbsttest B")); }
	if (Prog) { Prog->AddPerkPoints(9); }
	if (Inv && !ItemId.IsNone()) { Inv->AddItem(ItemId, 1, EMTAInventoryContainer::Personal); }
	if (Garage && !ModelId.IsNone()) { Garage->AddVehicle(ModelId, TEXT("core"), false); }
	if (Story) { SetStoryFlag(TEXT("B")); }
	if (Rel && !NpcId.IsNone()) { Rel->ModifyPlayerTrust(NpcId, -0.3f); }
	if (Heat) { Heat->AddPlayerHeat(0, -1.f, TEXT("core")); Heat->AddPlayerHeat(0, 0.5f, TEXT("rustline")); }
	Check(TEXT("Zustand B unterscheidet sich"), (!Eco || Eco->GetMoney() != MoneyA) && (!Prog || Prog->GetPerkPoints() != PerkA) && (!Story || GetStoryFlag() == TEXT("B")));

	// ---- Laden -----------------------------------------------------------------------------------
	FMTASaveContext CtxB;
	const bool bLoaded = LoadFromSlot(Slot, CtxB);
	Check(TEXT("Laden aus Testplatz"), bLoaded, GetLastError());
	Check(TEXT("Laden ohne Sicherung"), !DidLastLoadUseBackup());

	const bool bCtxOk = CtxB.Money == CtxA.Money
		&& FMath::IsNearlyEqual(CtxB.Hour, CtxA.Hour, 0.001f)
		&& FMath::IsNearlyEqual(CtxB.PlaytimeSeconds, CtxA.PlaytimeSeconds, 0.01f)
		&& CtxB.bHasPlayerTransform && CtxB.PlayerTransform.GetLocation().Equals(CtxA.PlayerTransform.GetLocation(), 0.01)
		&& CtxB.bHasVehicle && !CtxB.bInVehicle
		&& CtxB.VehicleTransform.GetLocation().Equals(CtxA.VehicleTransform.GetLocation(), 0.01)
		&& CtxB.VehicleTransform.GetRotation().Equals(CtxA.VehicleTransform.GetRotation(), 0.001)
		&& FMath::IsNearlyEqual(CtxB.VehicleHealth, CtxA.VehicleHealth, 0.001f)
		&& FMath::IsNearlyEqual(CtxB.VehicleFuel, CtxA.VehicleFuel, 0.001f)
		&& FMath::IsNearlyEqual(CtxB.Heat, CtxA.Heat, 0.001f)
		&& CtxB.District == CtxA.District && CtxB.MissionLabel == CtxA.MissionLabel && !CtxB.bStoryMission
		&& CtxB.MissionTarget.Equals(CtxA.MissionTarget, 0.01) && FMath::IsNearlyEqual(CtxB.MissionTimeLeft, CtxA.MissionTimeLeft, 0.01f);
	Check(TEXT("GameMode-Kontext (Geld, Zeit, Positionen, Fahrzeug, Fahndung, Mission)"), bCtxOk);

	if (Eco) { Check(TEXT("Wirtschaft: Kontostand"), Eco->GetMoney() == MoneyA, FString::Printf(TEXT("(%lld statt %lld)"), Eco->GetMoney(), MoneyA)); }
	if (Prog) { Check(TEXT("Fortschritt: Perk-Punkte"), Prog->GetPerkPoints() == PerkA, FString::Printf(TEXT("(%d statt %d)"), Prog->GetPerkPoints(), PerkA)); }
	if (Inv && !ItemId.IsNone()) { Check(TEXT("Inventar: Stueckzahl"), Inv->GetItemCount(ItemId) == ItemCountA); }
	if (Garage && !ModelId.IsNone()) { Check(TEXT("Garage: Fahrzeuganzahl"), Garage->GetVehicles().Num() == VehiclesA); }
	if (Story) { Check(TEXT("Story: Flag"), GetStoryFlag() == TEXT("A"), FString::Printf(TEXT("(%s)"), *GetStoryFlag())); }
	if (Rel && !NpcId.IsNone()) { Check(TEXT("Beziehungen: Spieler-Vertrauen"), FMath::IsNearlyEqual(Rel->GetPlayerTrust(NpcId), TrustA, 0.001f)); }
	if (Disc && bDiscForced) { Check(TEXT("Entdeckungen: gefunden"), Disc->IsDiscovered(DiscId)); }
	if (Heat) { Check(TEXT("Fahndung: Bezirks-Ruf"), FMath::IsNearlyEqual(Heat->GetDistrictHeat(0, TEXT("core")), DistrictHeatA, 0.01f) && Heat->GetDistrictHeat(0, TEXT("rustline")) < 0.01f); }

	// Gleichheit je Feld der Nutzdaten (Vergleich unabhaengig von der Schluesselreihenfolge)
	{
		const TSharedRef<FJsonObject> SnapshotB = BuildPayload(CtxB);
		for (const auto& Pair : SnapshotA->Values)
		{
			const TSharedPtr<FJsonValue> Other = SnapshotB->TryGetField(Pair.Key.ToView());
			const bool bEqual = Other.IsValid() && Pair.Value.IsValid() && FJsonValue::CompareEqual(*Pair.Value, *Other);
			Check(FString::Printf(TEXT("Feld '%s' nach Laden identisch"), *FString(Pair.Key.ToView())), bEqual, TEXT("(Zustand weicht ab)"));
		}
		Check(TEXT("Keine zusaetzlichen Felder nach Laden"), SnapshotA->Values.Num() == SnapshotB->Values.Num());
	}

	// ---- Beschaedigung und Sicherung -------------------------------------------------------------
	R += TEXT("-- Beschaedigte Dateien\n");
	Check(TEXT("Zweites Speichern"), SaveToSlot(Slot, CtxA), GetLastError());
	Check(TEXT("Sicherung (.bak) angelegt"), FM.FileExists(*Bak));

	FFileHelper::SaveStringToFile(TEXT("{ das ist kein gueltiges json"), *Main);
	if (Eco) { Eco->AddMoney(5, EMTATransactionCategory::Other, TEXT("Selbsttest C")); }
	FMTASaveContext CtxC;
	const bool bBakLoad = LoadFromSlot(Slot, CtxC);
	Check(TEXT("Hauptdatei Muell: Laden aus Sicherung"), bBakLoad && DidLastLoadUseBackup() && !GetLastError().IsEmpty(), GetLastError());
	if (Eco) { Check(TEXT("Sicherung stellt Zustand wieder her"), Eco->GetMoney() == MoneyA); }
	{
		TSharedPtr<FJsonObject> Healed;
		FString HealErr;
		Check(TEXT("Hauptdatei wurde geheilt"), ReadSlotFile(Main, Healed, HealErr), HealErr);
	}

	{
		// Pruefsumme verfaelschen
		FString Text;
		FFileHelper::LoadFileToString(Text, *Main);
		TSharedPtr<FJsonObject> Tampered = ParseObject(Text);
		if (Tampered.IsValid())
		{
			Tampered->SetStringField(TEXT("checksum"), TEXT("00000000000000000000000000000000"));
			FFileHelper::SaveStringToFile(ToPretty(Tampered.ToSharedRef()), *Main, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
		FMTASaveContext CtxD;
		const bool bSumLoad = LoadFromSlot(Slot, CtxD);
		Check(TEXT("Falsche Pruefsumme: Rueckfall auf Sicherung"), bSumLoad && DidLastLoadUseBackup(), GetLastError());
	}

	{
		// Beides kaputt: Laden schlaegt fehl, Zustand bleibt unveraendert
		FFileHelper::SaveStringToFile(TEXT("kaputt"), *Main);
		FFileHelper::SaveStringToFile(TEXT("auch kaputt"), *Bak);
		if (Eco) { Eco->AddMoney(11, EMTATransactionCategory::Other, TEXT("Selbsttest D")); }
		const int64 MoneyBefore = Eco ? Eco->GetMoney() : 0;
		FMTASaveContext CtxE;
		const bool bBothBad = LoadFromSlot(Slot, CtxE);
		Check(TEXT("Datei und Sicherung kaputt: Laden meldet Fehler"), !bBothBad && !GetLastError().IsEmpty());
		if (Eco) { Check(TEXT("Zustand nach Fehlschlag unveraendert"), Eco->GetMoney() == MoneyBefore); }
		FMTASaveSlotMeta BadMeta;
		ReadSlotMeta(Slot, BadMeta);
		Check(TEXT("Slot-Metadaten melden 'beschaedigt'"), BadMeta.bCorrupt);
		TArray<FMTASaveSlotInfo> BadInfos;
		GetSlotInfos(BadInfos);
		Check(TEXT("Menue: beschaedigter Platz nicht ladbar"), BadInfos.Num() > 0);
	}
	DeleteSlot(Slot);
	{
		FMTASaveContext CtxF;
		Check(TEXT("Leerer Platz: Laden meldet Fehler"), !LoadFromSlot(Slot, CtxF) && !GetLastError().IsEmpty());
	}

	// ---- Migration -------------------------------------------------------------------------------
	R += TEXT("-- Migration\n");
	{
		UMTASaveGame* Legacy = Cast<UMTASaveGame>(UGameplayStatics::CreateSaveGameObject(UMTASaveGame::StaticClass()));
		Check(TEXT("Alter Spielstand (v2) erzeugbar"), Legacy != nullptr);
		if (Legacy)
		{
			Legacy->Version = 2;
			Legacy->Money = 2468;
			Legacy->Heat = 3.f;
			Legacy->Hour = 21.5f;
			Legacy->VehicleTransform = FTransform(FRotator(0.f, 90.f, 0.f), FVector(500.f, 600.f, 700.f));
			Legacy->VehicleHealth = 61.f;
			Legacy->MissionTarget = FVector(10.f, 20.f, 30.f);
			Legacy->MissionTimeLeft = 44.f;
			if (Story)
			{
				const TSharedPtr<FJsonObject> StoryState = ParseObject(Story->StateToJson());
				if (StoryState.IsValid())
				{
					TSharedPtr<FJsonObject> Flags = GetObj(StoryState, TEXT("flags"));
					if (!Flags.IsValid())
					{
						Flags = MakeShared<FJsonObject>();
						StoryState->SetObjectField(TEXT("flags"), Flags);
					}
					Flags->SetStringField(TEXT("selftest.flag"), TEXT("legacy"));
					Legacy->StoryJson = ToPretty(StoryState.ToSharedRef());
				}
			}

			// Direkter Aufruf der Migrationskette
			TSharedPtr<FJsonObject> V2 = LegacyToV2Json(*Legacy);
			FString MigErr;
			Check(TEXT("Migrationskette v2 -> v3"), MigrateRoot(V2, MigErr) && (int32)GetNum(V2, TEXT("schema"), 0.0) == 3, MigErr);

			// Als Datei auf der Platte laden (wie ein echter alter Stand)
			FM.MakeDirectory(*FPaths::GetPath(Main), true);
			FFileHelper::SaveStringToFile(ToPretty(LegacyToV2Json(*Legacy)), *Main, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			FMTASaveSlotMeta OldMeta;
			ReadSlotMeta(Slot, OldMeta);
			Check(TEXT("v2-Datei: Metadaten lesbar (Geld)"), OldMeta.bExists && OldMeta.Money == 2468);
			FMTASaveContext CtxOld;
			const bool bOldLoad = LoadFromSlot(Slot, CtxOld);
			Check(TEXT("v2-Datei laden"), bOldLoad, GetLastError());
			Check(TEXT("v2: Geld, Fahndung, Uhrzeit"), CtxOld.Money == 2468 && FMath::IsNearlyEqual(CtxOld.Heat, 3.f, 0.001f) && FMath::IsNearlyEqual(CtxOld.Hour, 21.5f, 0.001f));
			Check(TEXT("v2: Fahrzeug (Position, Gier, Zustand)"),
				CtxOld.bHasVehicle && CtxOld.VehicleTransform.GetLocation().Equals(FVector(500.f, 600.f, 700.f), 0.01)
				&& FMath::IsNearlyEqual(CtxOld.VehicleTransform.Rotator().Yaw, 90.f, 0.01f) && FMath::IsNearlyEqual(CtxOld.VehicleHealth, 61.f, 0.001f));
			Check(TEXT("v2: Mission"), CtxOld.MissionTarget.Equals(FVector(10.f, 20.f, 30.f), 0.01) && FMath::IsNearlyEqual(CtxOld.MissionTimeLeft, 44.f, 0.01f));
			if (Story) { Check(TEXT("v2: Story-Zustand uebernommen"), GetStoryFlag() == TEXT("legacy"), FString::Printf(TEXT("(%s)"), *GetStoryFlag())); }
			DeleteSlot(Slot);

			// Version 1: kein Story-Feld
			TSharedRef<FJsonObject> V1 = LegacyToV2Json(*Legacy);
			V1->SetNumberField(TEXT("schema"), 1);
			V1->RemoveField(TEXT("storyJson"));
			FFileHelper::SaveStringToFile(ToPretty(V1), *Main, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			FMTASaveContext CtxV1;
			Check(TEXT("v1-Datei laden (ohne Story)"), LoadFromSlot(Slot, CtxV1) && CtxV1.Money == 2468, GetLastError());
			DeleteSlot(Slot);
		}

		// Zu neue Version wird verweigert
		TSharedRef<FJsonObject> Future = MakeShared<FJsonObject>();
		Future->SetNumberField(TEXT("schema"), 99);
		Future->SetStringField(TEXT("payload"), TEXT("{}"));
		FM.MakeDirectory(*FPaths::GetPath(Main), true);
		FFileHelper::SaveStringToFile(ToPretty(Future), *Main, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FMTASaveContext CtxFuture;
		Check(TEXT("Neuere Version: Laden verweigert"), !LoadFromSlot(Slot, CtxFuture) && GetLastError().Contains(TEXT("neuere")), GetLastError());
		FMTASaveSlotMeta FutureMeta;
		ReadSlotMeta(Slot, FutureMeta);
		Check(TEXT("Neuere Version: Slot-Info markiert"), FutureMeta.bTooNew);
		DeleteSlot(Slot);
	}

	// ---- Aufraeumen und Zustand wiederherstellen ----------------------------------------------------
	DeleteSlot(Slot);
	{
		FMTASaveContext Dummy;
		TArray<FString> Warnings;
		ApplyPayload(Original, Dummy, Warnings);
		const TSharedRef<FJsonObject> After = BuildPayload(OrigCtx);
		bool bAllEqual = true;
		FString Differing;
		for (const auto& Pair : Original->Values)
		{
			const TSharedPtr<FJsonValue> Other = After->TryGetField(Pair.Key.ToView());
			if (!Other.IsValid() || !Pair.Value.IsValid() || !FJsonValue::CompareEqual(*Pair.Value, *Other))
			{
				bAllEqual = false;
				Differing += FString(Pair.Key.ToView()) + TEXT(" ");
			}
		}
		Check(TEXT("Urspruenglicher Zustand wiederhergestellt"), bAllEqual, FString::Printf(TEXT("(abweichend: %s)"), *Differing));
	}

	R += FString::Printf(TEXT("SELBSTTEST SPEICHERN: %d Fehler, %d Pruefungen bestanden\n"), Fail, Pass);
	UE_LOG(LogMTASave, Display, TEXT("%s"), *R);
	return R;
}
