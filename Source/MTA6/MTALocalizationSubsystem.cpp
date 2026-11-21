#include "MTALocalizationSubsystem.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "MTASettingsSubsystem.h"
#include "MTASettingsUser.h"

UMTALocalizationSubsystem* UMTALocalizationSubsystem::ActiveInstance = nullptr;

void UMTALocalizationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UMTASettingsSubsystem* SettingsSub = Collection.InitializeDependency<UMTASettingsSubsystem>();
	ActiveInstance = this;

	FString Code = TEXT("de");
	if (SettingsSub && SettingsSub->GetSettings())
	{
		Code = SettingsSub->GetSettings()->Language;
	}

	LoadTable(TEXT("de"), FallbackTable);
	if (!LoadTable(Code, CurrentTable))
	{
		Code = TEXT("de");
		CurrentTable = FallbackTable;
	}
	CurrentLanguage = Code;
}

void UMTALocalizationSubsystem::Deinitialize()
{
	if (ActiveInstance == this)
	{
		ActiveInstance = nullptr;
	}
	Super::Deinitialize();
}

bool UMTALocalizationSubsystem::LoadTable(const FString& Code, TMap<FString, FText>& OutTable) const
{
	OutTable.Reset();
	const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"), FString::Printf(TEXT("text.%s.json"), *Code));
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("MTA6: Sprachdatei nicht gefunden: %s"), *Path);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("MTA6: Sprachdatei ist kein gueltiges JSON: %s"), *Path);
		return false;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Root->Values)
	{
		FString Text;
		if (Pair.Value.IsValid() && Pair.Value->TryGetString(Text) && !Pair.Key.StartsWith(TEXT("_")))
		{
			OutTable.Add(Pair.Key, FText::FromString(Text));
		}
	}
	return OutTable.Num() > 0;
}

FText UMTALocalizationSubsystem::Tr(const FString& Key) const
{
	if (const FText* Found = CurrentTable.Find(Key))
	{
		return *Found;
	}
	if (const FText* Fallback = FallbackTable.Find(Key))
	{
		return *Fallback;
	}
	return FText::FromString(Key);
}

FString UMTALocalizationSubsystem::TrString(const FString& Key) const
{
	return Tr(Key).ToString();
}

FText UMTALocalizationSubsystem::TrFormat(const FString& Key, const TArray<FString>& Args) const
{
	FStringFormatOrderedArguments Ordered;
	for (const FString& Arg : Args)
	{
		Ordered.Add(FStringFormatArg(Arg));
	}
	return FText::FromString(FString::Format(*Tr(Key).ToString(), Ordered));
}

bool UMTALocalizationSubsystem::HasKey(const FString& Key) const
{
	return CurrentTable.Contains(Key) || FallbackTable.Contains(Key);
}

bool UMTALocalizationSubsystem::SetLanguage(const FString& Code)
{
	if (Code == CurrentLanguage)
	{
		return true;
	}
	TMap<FString, FText> NewTable;
	if (!LoadTable(Code, NewTable))
	{
		return false;
	}
	CurrentTable = MoveTemp(NewTable);
	CurrentLanguage = Code;

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMTASettingsSubsystem* SettingsSub = GI->GetSubsystem<UMTASettingsSubsystem>())
		{
			if (UMTAGameUserSettings* Settings = SettingsSub->GetSettings())
			{
				Settings->Language = Code;
				Settings->SaveConfig();
			}
		}
	}
	OnLanguageChanged.Broadcast();
	return true;
}

TArray<FString> UMTALocalizationSubsystem::GetAvailableLanguages() const
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"), TEXT("text.*.json")), true, false);
	TArray<FString> Codes;
	for (const FString& File : Files)
	{
		// text.<code>.json -> <code>
		TArray<FString> Parts;
		File.ParseIntoArray(Parts, TEXT("."));
		if (Parts.Num() == 3)
		{
			Codes.Add(Parts[1]);
		}
	}
	Codes.Sort();
	return Codes;
}

FText MTATr(const FString& Key)
{
	if (const UMTALocalizationSubsystem* Loc = UMTALocalizationSubsystem::GetActive())
	{
		return Loc->Tr(Key);
	}
	return FText::FromString(Key);
}

FText MTATrF(const FString& Key, const TArray<FString>& Args)
{
	if (const UMTALocalizationSubsystem* Loc = UMTALocalizationSubsystem::GetActive())
	{
		return Loc->TrFormat(Key, Args);
	}
	return FText::FromString(Key);
}
