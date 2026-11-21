#include "MTASettingsSubsystem.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/PlatformMisc.h"
#include "MTASettingsUser.h"

// UInputSettings (UE 5.8): AxisMappings/ActionMappings sind privat. Die Belegung wird deshalb als Kopie
// bearbeitet und anschliessend ueber die oeffentlichen Add/Remove-Funktionen zurueckgeschrieben.
namespace MTAInputHelp
{
	static void ReplaceAll(UInputSettings* Input, const TArray<FInputAxisKeyMapping>& Axes, const TArray<FInputActionKeyMapping>& Acts)
	{
		const TArray<FInputAxisKeyMapping> OldAxes = Input->GetAxisMappings();
		for (const FInputAxisKeyMapping& M : OldAxes) { Input->RemoveAxisMapping(M, false); }
		const TArray<FInputActionKeyMapping> OldActs = Input->GetActionMappings();
		for (const FInputActionKeyMapping& M : OldActs) { Input->RemoveActionMapping(M, false); }
		for (const FInputAxisKeyMapping& M : Axes) { Input->AddAxisMapping(M, false); }
		for (const FInputActionKeyMapping& M : Acts) { Input->AddActionMapping(M, false); }
	}
}

UMTASettingsSubsystem* UMTASettingsSubsystem::ActiveInstance = nullptr;

namespace
{
	/** Standardbelegung (Spiegel von Config/DefaultInput.ini plus Gamepad). */
	struct FDefaultBinding
	{
		FMTAKeyBindingEntry Entry;
		FKey Key1;
		FKey Key2;
		FKey Pad;
	};

	const TArray<FDefaultBinding>& GetDefaults()
	{
		static TArray<FDefaultBinding> Table;
		if (Table.Num() == 0)
		{
			auto Add = [](const TCHAR* Name, bool bAxis, float Scale, bool bGamepad, const TCHAR* LabelKey, FKey K1, FKey K2, FKey Pad)
			{
				FDefaultBinding B;
				B.Entry.Name = Name; B.Entry.bAxis = bAxis; B.Entry.Scale = Scale;
				B.Entry.bGamepad = bGamepad; B.Entry.LabelKey = LabelKey;
				B.Key1 = K1; B.Key2 = K2; B.Pad = Pad;
				Table.Add(B);
			};
			Add(TEXT("Throttle"),  true,  1.f,  true,  TEXT("ctrl.throttle"),   EKeys::W, EKeys::Up,   EKeys::Gamepad_RightTriggerAxis);
			Add(TEXT("Brake"),     true,  1.f,  true,  TEXT("ctrl.brake"),      EKeys::S, EKeys::Down, EKeys::Gamepad_LeftTriggerAxis);
			Add(TEXT("Steer"),     true,  -1.f, false, TEXT("ctrl.steer_left"), EKeys::A, EKeys::Left, EKeys::Invalid);
			Add(TEXT("Steer"),     true,  1.f,  false, TEXT("ctrl.steer_right"),EKeys::D, EKeys::Right,EKeys::Gamepad_LeftX);
			Add(TEXT("Handbrake"), false, 1.f,  true,  TEXT("ctrl.handbrake"),  EKeys::SpaceBar, EKeys::Invalid, EKeys::Gamepad_FaceButton_Left);
			Add(TEXT("Reset"),     false, 1.f,  true,  TEXT("ctrl.reset"),      EKeys::R, EKeys::Invalid, EKeys::Gamepad_FaceButton_Top);
			Add(TEXT("Save"),      false, 1.f,  false, TEXT("ctrl.quicksave"),  EKeys::F5, EKeys::Invalid, EKeys::Invalid);
			Add(TEXT("Load"),      false, 1.f,  false, TEXT("ctrl.quickload"),  EKeys::F9, EKeys::Invalid, EKeys::Invalid);
			Add(TEXT("Choice1"),   false, 1.f,  false, TEXT("ctrl.choice1"),    EKeys::One,   EKeys::Invalid, EKeys::Invalid);
			Add(TEXT("Choice2"),   false, 1.f,  false, TEXT("ctrl.choice2"),    EKeys::Two,   EKeys::Invalid, EKeys::Invalid);
			Add(TEXT("Choice3"),   false, 1.f,  false, TEXT("ctrl.choice3"),    EKeys::Three, EKeys::Invalid, EKeys::Invalid);
			Add(TEXT("Choice4"),   false, 1.f,  false, TEXT("ctrl.choice4"),    EKeys::Four,  EKeys::Invalid, EKeys::Invalid);
			Add(TEXT("Confirm"),   false, 1.f,  true,  TEXT("ctrl.confirm"),    EKeys::Enter, EKeys::Invalid, EKeys::Gamepad_FaceButton_Bottom);
		}
		return Table;
	}

	bool SameEntry(const FMTAKeyBindingEntry& E, bool bAxis, FName Name, float Scale)
	{
		return E.bAxis == bAxis && E.Name == Name && (!bAxis || FMath::IsNearlyEqual(E.Scale, Scale));
	}

	/** Stellt sicher, dass fuer die Achse eine Konfiguration existiert, und liefert sie. */
	FInputAxisProperties& FindOrAddAxisConfig(UInputSettings* Input, const FName AxisKey)
	{
		for (FInputAxisConfigEntry& Entry : Input->AxisConfig)
		{
			if (Entry.AxisKeyName == AxisKey)
			{
				return Entry.AxisProperties;
			}
		}
		FInputAxisConfigEntry NewEntry;
		NewEntry.AxisKeyName = AxisKey;
		const int32 Index = Input->AxisConfig.Add(NewEntry);
		return Input->AxisConfig[Index].AxisProperties;
	}
}

void UMTASettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ActiveInstance = this;

	Settings = UMTAGameUserSettings::Get();
	if (!Settings)
	{
		// GameUserSettingsClassName ist nicht gesetzt: eigene Instanz (liest dieselbe INI-Sektion).
		Settings = NewObject<UMTAGameUserSettings>(this);
		Settings->LoadConfig();
		UE_LOG(LogTemp, Warning, TEXT("MTA6: GameUserSettingsClassName zeigt nicht auf UMTAGameUserSettings; eigene Instanz verwendet."));
	}
	else
	{
		Settings->LoadSettings();
	}

	if (!Settings->bHardwareDetected)
	{
		// Erster Start: Preset passend zur Hardware (RX 5700 XT -> Mittel).
		Settings->DetectHardwareAndApply();
		Settings->SaveConfig();
	}

	Settings->ApplyMTAGraphics();
	Settings->ApplyNonResolutionSettings();

	// Tastenbelegung: gespeicherte Momentaufnahme anwenden, sonst Gamepad-Standard ergaenzen.
	EnsureGamepadDefaults();
	if (Settings->KeyBindingOverrides.Num() > 0)
	{
		if (UInputSettings* Input = UInputSettings::GetInputSettings())
		{
			TArray<FInputAxisKeyMapping> Axes = Input->GetAxisMappings();
			TArray<FInputActionKeyMapping> Acts = Input->GetActionMappings();
			// Alle verwalteten Eintraege entfernen und aus der Momentaufnahme neu aufbauen.
			for (const FDefaultBinding& D : GetDefaults())
			{
				if (D.Entry.bAxis)
				{
					Axes.RemoveAll([&D](const FInputAxisKeyMapping& M) { return M.AxisName == D.Entry.Name && FMath::IsNearlyEqual(M.Scale, D.Entry.Scale); });
				}
				else
				{
					Acts.RemoveAll([&D](const FInputActionKeyMapping& M) { return M.ActionName == D.Entry.Name; });
				}
			}
			for (const FString& Line : Settings->KeyBindingOverrides)
			{
				TArray<FString> P;
				Line.ParseIntoArray(P, TEXT("|"));
				if (P.Num() == 3 && P[0] == TEXT("A"))
				{
					Acts.AddUnique(FInputActionKeyMapping(FName(*P[1]), FKey(FName(*P[2]))));
				}
				else if (P.Num() == 4 && P[0] == TEXT("X"))
				{
					Axes.AddUnique(FInputAxisKeyMapping(FName(*P[1]), FKey(FName(*P[3])), FCString::Atof(*P[2])));
				}
			}
			MTAInputHelp::ReplaceAll(Input, Axes, Acts);
			Input->ForceRebuildKeymaps();
		}
	}
	ApplyControllerSettings();
	ApplyAudio();
}

void UMTASettingsSubsystem::Deinitialize()
{
	if (ActiveInstance == this)
	{
		ActiveInstance = nullptr;
	}
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------- Anwenden

void UMTASettingsSubsystem::ApplyAndSave()
{
	if (!Settings)
	{
		return;
	}
	Settings->ApplySettings(false);
	Settings->SaveSettings();
	Settings->SaveConfig();   // greift auch bei eigener Instanz (ohne GameUserSettingsClassName)
	ApplyControllerSettings();
	ApplyAudio();
	OnSettingsChanged.Broadcast();
}

void UMTASettingsSubsystem::RevertChanges()
{
	if (!Settings)
	{
		return;
	}
	Settings->LoadSettings(true);
	Settings->LoadConfig();
	Settings->ApplySettings(false);
	ApplyControllerSettings();
	ApplyAudio();
	OnSettingsChanged.Broadcast();
}

void UMTASettingsSubsystem::ResetAllToDefaults()
{
	if (!Settings)
	{
		return;
	}
	Settings->SetToDefaults();
	Settings->DetectHardwareAndApply();
	ResetBindingsToDefault();
	ApplyAndSave();
}

void UMTASettingsSubsystem::ApplyPreset(EMTAGraphicsPreset Preset)
{
	if (Settings)
	{
		Settings->ApplyGraphicsPreset(Preset);
	}
}

void UMTASettingsSubsystem::MarkCustomPreset()
{
	if (Settings)
	{
		Settings->GraphicsPreset = EMTAGraphicsPreset::Custom;
	}
}

EMTAGraphicsPreset UMTASettingsSubsystem::AutoDetectHardware()
{
	return Settings ? Settings->DetectHardwareAndApply() : EMTAGraphicsPreset::Medium;
}

FString UMTASettingsSubsystem::GetDetectedHardwareName() const
{
	return FString::Printf(TEXT("%s / %s"), *FPlatformMisc::GetPrimaryGPUBrand(), *FPlatformMisc::GetCPUBrand().TrimStartAndEnd());
}

FString UMTASettingsSubsystem::PresetKey(EMTAGraphicsPreset Preset)
{
	switch (Preset)
	{
	case EMTAGraphicsPreset::Low:    return TEXT("opt.preset.low");
	case EMTAGraphicsPreset::Medium: return TEXT("opt.preset.medium");
	case EMTAGraphicsPreset::High:   return TEXT("opt.preset.high");
	case EMTAGraphicsPreset::Ultra:  return TEXT("opt.preset.ultra");
	default:                         return TEXT("opt.preset.custom");
	}
}

// ---------------------------------------------------------------------------- Audio

float UMTASettingsSubsystem::GetBusVolume(EMTAAudioBus Bus) const
{
	if (!Settings)
	{
		return 1.f;
	}
	const float Master = Settings->MasterVolume;
	switch (Bus)
	{
	case EMTAAudioBus::Music: return Master * Settings->MusicVolume;
	case EMTAAudioBus::Radio: return Master * Settings->RadioVolume;
	case EMTAAudioBus::Sfx:   return Master * Settings->SfxVolume;
	case EMTAAudioBus::Voice: return Master * Settings->VoiceVolume;
	default:                  return Master;
	}
}

void UMTASettingsSubsystem::SetBusVolume(EMTAAudioBus Bus, float Volume)
{
	if (!Settings)
	{
		return;
	}
	Volume = FMath::Clamp(Volume, 0.f, 1.f);
	switch (Bus)
	{
	case EMTAAudioBus::Master: Settings->MasterVolume = Volume; break;
	case EMTAAudioBus::Music:  Settings->MusicVolume = Volume; break;
	case EMTAAudioBus::Radio:  Settings->RadioVolume = Volume; break;
	case EMTAAudioBus::Sfx:    Settings->SfxVolume = Volume; break;
	case EMTAAudioBus::Voice:  Settings->VoiceVolume = Volume; break;
	default: return;
	}
	ApplyAudio();
}

void UMTASettingsSubsystem::ApplyAudio()
{
	if (!Settings)
	{
		return;
	}
	// Gesamtlautstaerke direkt am Audiogeraet; die uebrigen Busse melden wir per Delegate.
	if (GEngine)
	{
		if (FAudioDevice* Device = GEngine->GetMainAudioDeviceRaw())
		{
			Device->SetTransientPrimaryVolume(Settings->MasterVolume);
		}
	}
	for (int32 i = 0; i < static_cast<int32>(EMTAAudioBus::Count); ++i)
	{
		const EMTAAudioBus Bus = static_cast<EMTAAudioBus>(i);
		OnAudioVolumeChanged.Broadcast(Bus, GetBusVolume(Bus));
	}
}

// ------------------------------------------------------------------------- Steuerung

const TArray<FMTAKeyBindingEntry>& UMTASettingsSubsystem::GetBindingEntries()
{
	static TArray<FMTAKeyBindingEntry> Entries;
	if (Entries.Num() == 0)
	{
		for (const FDefaultBinding& D : GetDefaults())
		{
			Entries.Add(D.Entry);
		}
	}
	return Entries;
}

void UMTASettingsSubsystem::GetBindingKeys(const FMTAKeyBindingEntry& Entry, bool bGamepad, TArray<FKey>& OutKeys) const
{
	OutKeys.Reset();
	const UInputSettings* Input = UInputSettings::GetInputSettings();
	if (!Input)
	{
		return;
	}
	if (Entry.bAxis)
	{
		for (const FInputAxisKeyMapping& M : Input->GetAxisMappings())
		{
			if (M.AxisName == Entry.Name && FMath::IsNearlyEqual(M.Scale, Entry.Scale) && M.Key.IsGamepadKey() == bGamepad)
			{
				OutKeys.Add(M.Key);
			}
		}
	}
	else
	{
		for (const FInputActionKeyMapping& M : Input->GetActionMappings())
		{
			if (M.ActionName == Entry.Name && M.Key.IsGamepadKey() == bGamepad)
			{
				OutKeys.Add(M.Key);
			}
		}
	}
}

FString UMTASettingsSubsystem::Rebind(const FMTAKeyBindingEntry& Entry, bool bGamepad, int32 Slot, FKey NewKey)
{
	UInputSettings* Input = UInputSettings::GetInputSettings();
	if (!Input || !NewKey.IsValid() || NewKey.IsGamepadKey() != bGamepad || NewKey == EKeys::Escape)
	{
		return FString();
	}

	TArray<FKey> Current;
	GetBindingKeys(Entry, bGamepad, Current);
	const FKey OldKey = Current.IsValidIndex(Slot) ? Current[Slot] : FKey();
	if (OldKey == NewKey)
	{
		return FString();
	}

	// 1. Konflikte entfernen (gleiche Taste bei anderen Eintraegen derselben Geraeteart).
	FString Displaced;
	TArray<FInputAxisKeyMapping> Axes = Input->GetAxisMappings();
	TArray<FInputActionKeyMapping> Acts = Input->GetActionMappings();
	for (int32 i = Axes.Num() - 1; i >= 0; --i)
	{
		const FInputAxisKeyMapping& M = Axes[i];
		if (M.Key == NewKey)
		{
			for (const FMTAKeyBindingEntry& E : GetBindingEntries())
			{
				if (SameEntry(E, true, M.AxisName, M.Scale) && !(Entry.bAxis && SameEntry(Entry, true, M.AxisName, M.Scale)))
				{
					Displaced = E.LabelKey;
				}
			}
			Axes.RemoveAt(i);
		}
	}
	for (int32 i = Acts.Num() - 1; i >= 0; --i)
	{
		const FInputActionKeyMapping& M = Acts[i];
		if (M.Key == NewKey)
		{
			for (const FMTAKeyBindingEntry& E : GetBindingEntries())
			{
				if (SameEntry(E, false, M.ActionName, 1.f) && !(!Entry.bAxis && SameEntry(Entry, false, M.ActionName, 1.f)))
				{
					Displaced = E.LabelKey;
				}
			}
			Acts.RemoveAt(i);
		}
	}

	// 2. Alten Slot ersetzen (an gleicher Position) oder neue Taste anhaengen.
	bool bReplaced = false;
	if (OldKey.IsValid())
	{
		if (Entry.bAxis)
		{
			for (FInputAxisKeyMapping& M : Axes)
			{
				if (M.AxisName == Entry.Name && FMath::IsNearlyEqual(M.Scale, Entry.Scale) && M.Key == OldKey)
				{
					M.Key = NewKey; bReplaced = true; break;
				}
			}
		}
		else
		{
			for (FInputActionKeyMapping& M : Acts)
			{
				if (M.ActionName == Entry.Name && M.Key == OldKey)
				{
					M.Key = NewKey; bReplaced = true; break;
				}
			}
		}
	}
	if (!bReplaced)
	{
		if (Entry.bAxis)
		{
			Axes.Add(FInputAxisKeyMapping(Entry.Name, NewKey, Entry.Scale));
		}
		else
		{
			Acts.Add(FInputActionKeyMapping(Entry.Name, NewKey));
		}
	}
	MTAInputHelp::ReplaceAll(Input, Axes, Acts);
	Input->ForceRebuildKeymaps();

	// 3. Momentaufnahme in den eigenen Einstellungen merken (UInputSettings ist defaultconfig).
	if (Settings)
	{
		Settings->KeyBindingOverrides.Reset();
		for (const FMTAKeyBindingEntry& E : GetBindingEntries())
		{
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				TArray<FKey> Keys;
				GetBindingKeys(E, Pass == 1, Keys);
				for (const FKey& K : Keys)
				{
					Settings->KeyBindingOverrides.AddUnique(E.bAxis
						? FString::Printf(TEXT("X|%s|%g|%s"), *E.Name.ToString(), E.Scale, *K.GetFName().ToString())
						: FString::Printf(TEXT("A|%s|%s"), *E.Name.ToString(), *K.GetFName().ToString()));
				}
			}
		}
		Settings->SaveConfig();
	}
	return Displaced;
}

void UMTASettingsSubsystem::ResetBindingsToDefault()
{
	UInputSettings* Input = UInputSettings::GetInputSettings();
	if (!Input)
	{
		return;
	}
	TArray<FInputAxisKeyMapping> Axes = Input->GetAxisMappings();
	TArray<FInputActionKeyMapping> Acts = Input->GetActionMappings();
	for (const FDefaultBinding& D : GetDefaults())
	{
		if (D.Entry.bAxis)
		{
			Axes.RemoveAll([&D](const FInputAxisKeyMapping& M) { return M.AxisName == D.Entry.Name && FMath::IsNearlyEqual(M.Scale, D.Entry.Scale); });
			for (const FKey& K : { D.Key1, D.Key2, D.Pad })
			{
				if (K.IsValid()) { Axes.Add(FInputAxisKeyMapping(D.Entry.Name, K, D.Entry.Scale)); }
			}
		}
		else
		{
			Acts.RemoveAll([&D](const FInputActionKeyMapping& M) { return M.ActionName == D.Entry.Name; });
			for (const FKey& K : { D.Key1, D.Key2, D.Pad })
			{
				if (K.IsValid()) { Acts.Add(FInputActionKeyMapping(D.Entry.Name, K)); }
			}
		}
	}
	MTAInputHelp::ReplaceAll(Input, Axes, Acts);
	Input->ForceRebuildKeymaps();
	if (Settings)
	{
		Settings->KeyBindingOverrides.Reset();
		Settings->SaveConfig();
	}
}

void UMTASettingsSubsystem::EnsureGamepadDefaults()
{
	UInputSettings* Input = UInputSettings::GetInputSettings();
	if (!Input)
	{
		return;
	}
	bool bChanged = false;
	for (const FDefaultBinding& D : GetDefaults())
	{
		if (!D.Pad.IsValid())
		{
			continue;
		}
		TArray<FKey> Keys;
		GetBindingKeys(D.Entry, true, Keys);
		if (Keys.Num() == 0)
		{
			if (D.Entry.bAxis) { Input->AddAxisMapping(FInputAxisKeyMapping(D.Entry.Name, D.Pad, D.Entry.Scale), false); }
			else               { Input->AddActionMapping(FInputActionKeyMapping(D.Entry.Name, D.Pad), false); }
			bChanged = true;
		}
	}
	if (bChanged)
	{
		Input->ForceRebuildKeymaps();
	}
}

void UMTASettingsSubsystem::ApplyControllerSettings()
{
	UInputSettings* Input = UInputSettings::GetInputSettings();
	if (!Input || !Settings)
	{
		return;
	}
	// Totzonen und Kamera-Empfindlichkeit ueber die Achsenkonfiguration (gilt fuer alle Spieler).
	FindOrAddAxisConfig(Input, TEXT("Gamepad_LeftX")).DeadZone = Settings->SteeringDeadzone;
	FindOrAddAxisConfig(Input, TEXT("Gamepad_LeftTriggerAxis")).DeadZone = Settings->TriggerDeadzone;
	FindOrAddAxisConfig(Input, TEXT("Gamepad_RightTriggerAxis")).DeadZone = Settings->TriggerDeadzone;

	FInputAxisProperties& RightX = FindOrAddAxisConfig(Input, TEXT("Gamepad_RightX"));
	RightX.Sensitivity = Settings->CameraSensitivity;
	FInputAxisProperties& RightY = FindOrAddAxisConfig(Input, TEXT("Gamepad_RightY"));
	RightY.Sensitivity = Settings->CameraSensitivity;
	RightY.bInvert = Settings->bInvertCameraY;
	FindOrAddAxisConfig(Input, TEXT("MouseX")).Sensitivity = Settings->CameraSensitivity;
	FInputAxisProperties& MouseY = FindOrAddAxisConfig(Input, TEXT("MouseY"));
	MouseY.Sensitivity = Settings->CameraSensitivity;
	MouseY.bInvert = Settings->bInvertCameraY;

	Input->ForceRebuildKeymaps();
}

float UMTASettingsSubsystem::ApplySteeringSensitivity(float Raw) const
{
	const float Sens = Settings ? Settings->SteeringSensitivity : 1.f;
	return FMath::Clamp(Raw * Sens, -1.f, 1.f);
}
