#include "MTASettingsUser.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformMemory.h"

namespace
{
	/** Setzt eine CVar mit Prioritaet GameSetting (schlaegt Scalability, bleibt unter Konsole). */
	void SetCVarInt(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByGameSetting);
		}
	}

	void SetCVarFloat(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByGameSetting);
		}
	}

	/** Moegliche CVar-Namen des AMD-FSR-Plugins (je nach Pluginversion). */
	const TCHAR* const GFSRCVars[] = { TEXT("r.FidelityFX.FSR3.Enabled"), TEXT("r.FidelityFX.FSR4.Enabled") };
}

UMTAGameUserSettings::UMTAGameUserSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Standardwerte entsprechen dem Preset "Mittel" (Zielhardware RX 5700 XT).
	GraphicsPreset = EMTAGraphicsPreset::Medium;
	Upscaler = EMTAUpscaler::TSR;
	UpscaleQuality = EMTAUpscaleQuality::Balanced;
	ResolutionScalePct = 100.f;
	ShadowQualityLevel = 2;
	GIQualityLevel = 1;
	NaniteQualityLevel = 1;
	LumenQualityLevel = 1;
	ViewDistanceLevel = 2;
	TextureQualityLevel = 2;
	EffectsQualityLevel = 2;
	PostProcessQualityLevel = 2;
	bMotionBlur = true;
	bHardwareDetected = false;

	MasterVolume = 1.f;
	MusicVolume = 0.8f;
	RadioVolume = 0.8f;
	SfxVolume = 1.f;
	VoiceVolume = 1.f;

	SteeringSensitivity = 1.f;
	SteeringDeadzone = 0.08f;
	TriggerDeadzone = 0.05f;
	CameraSensitivity = 1.f;
	bInvertCameraY = false;
	bVibration = true;

	Difficulty = EMTADifficulty::Normal;
	WantedStrength = 1.f;
	bBrakeAssist = false;
	bRouteGuidance = true;
	bSubtitles = true;
	SubtitleSize = 1;
	bSubtitleBackground = true;
	bTutorialHints = true;
	Language = TEXT("de");

	ColorBlindMode = EMTAColorBlindMode::Off;
	ColorBlindStrength = 1.f;
	bLargeText = false;
	UiScale = 1.f;
	bHighContrast = false;
	bSteeringAssist = false;
	SteeringAssistStrength = 0.5f;
	bOneButtonMode = false;
	bReduceCameraMotion = false;
	bReduceFlashes = false;
	bShowFlashWarning = true;
}

UMTAGameUserSettings* UMTAGameUserSettings::Get()
{
	return GEngine ? Cast<UMTAGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UMTAGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();

	const FString KeepLanguage = Language;
	const TArray<FName> KeepSeen = SeenTutorialHints;
	// Alle MTA-Werte auf Standard (Preset Mittel) zuruecksetzen.
	GraphicsPreset = EMTAGraphicsPreset::Medium;
	ApplyGraphicsPreset(EMTAGraphicsPreset::Medium);
	bMotionBlur = true;
	MasterVolume = 1.f; MusicVolume = 0.8f; RadioVolume = 0.8f; SfxVolume = 1.f; VoiceVolume = 1.f;
	SteeringSensitivity = 1.f; SteeringDeadzone = 0.08f; TriggerDeadzone = 0.05f;
	CameraSensitivity = 1.f; bInvertCameraY = false; bVibration = true;
	Difficulty = EMTADifficulty::Normal; WantedStrength = 1.f;
	bBrakeAssist = false; bRouteGuidance = true;
	bSubtitles = true; SubtitleSize = 1; bSubtitleBackground = true; bTutorialHints = true;
	ColorBlindMode = EMTAColorBlindMode::Off; ColorBlindStrength = 1.f;
	bLargeText = false; UiScale = 1.f; bHighContrast = false;
	bSteeringAssist = false; SteeringAssistStrength = 0.5f; bOneButtonMode = false;
	bReduceCameraMotion = false; bReduceFlashes = false;
	Language = KeepLanguage;          // Sprache und gesehene Hinweise bleiben erhalten
	SeenTutorialHints = KeepSeen;
}

float UMTAGameUserSettings::GetUpscaleFactor(EMTAUpscaleQuality Quality)
{
	switch (Quality)
	{
	case EMTAUpscaleQuality::Quality:     return 0.77f;
	case EMTAUpscaleQuality::Balanced:    return 0.67f;
	case EMTAUpscaleQuality::Performance: return 0.50f;
	default:                              return 1.0f;
	}
}

float UMTAGameUserSettings::GetEffectiveScreenPercentage() const
{
	const float Factor = (Upscaler == EMTAUpscaler::NativeTAA) ? 1.f : GetUpscaleFactor(UpscaleQuality);
	return FMath::Clamp(Factor * (ResolutionScalePct / 100.f), 0.25f, 1.f);
}

bool UMTAGameUserSettings::IsFSRAvailable()
{
	for (const TCHAR* Name : GFSRCVars)
	{
		if (IConsoleManager::Get().FindConsoleVariable(Name))
		{
			return true;
		}
	}
	return false;
}

void UMTAGameUserSettings::MarkTutorialHintSeen(FName HintId)
{
	if (!SeenTutorialHints.Contains(HintId))
	{
		SeenTutorialHints.Add(HintId);
		SaveConfig();
	}
}

void UMTAGameUserSettings::ApplyGraphicsPreset(EMTAGraphicsPreset Preset)
{
	if (Preset == EMTAGraphicsPreset::Custom)
	{
		GraphicsPreset = Preset;
		return;
	}

	// Tabelle: Upscaler, Qualitaet, Skala, Schatten, GI, Nanite, Lumen, Sicht, Textur, Effekte, PP, FPS-Limit
	struct FPresetRow
	{
		EMTAUpscaler Upscaler; EMTAUpscaleQuality Quality; float Scale;
		int32 Shadow, GI, Nanite, Lumen, View, Tex, Fx, PP; float FpsLimit; bool bBlur;
	};
	static const FPresetRow Rows[4] =
	{
		// Niedrig: aggressiv hochskaliert, Lumen minimal
		{ EMTAUpscaler::FSR,       EMTAUpscaleQuality::Performance, 100.f, 0, 0, 0, 0, 1, 1, 1, 1, 60.f,  false },
		// Mittel: Zielhardware RX 5700 XT, 1440p mit Ausgewogen (~67 %)
		{ EMTAUpscaler::TSR,       EMTAUpscaleQuality::Balanced,    100.f, 2, 1, 1, 1, 2, 2, 2, 2, 60.f,  true  },
		// Hoch
		{ EMTAUpscaler::TSR,       EMTAUpscaleQuality::Quality,     100.f, 3, 2, 2, 2, 3, 3, 3, 3, 0.f,   true  },
		// Ultra
		{ EMTAUpscaler::NativeTAA, EMTAUpscaleQuality::Native,      100.f, 4, 3, 3, 3, 4, 4, 4, 4, 0.f,   true  }
	};

	const FPresetRow& R = Rows[FMath::Clamp(static_cast<int32>(Preset), 0, 3)];
	GraphicsPreset = Preset;
	Upscaler = R.Upscaler;
	UpscaleQuality = R.Quality;
	ResolutionScalePct = R.Scale;
	ShadowQualityLevel = R.Shadow;
	GIQualityLevel = R.GI;
	NaniteQualityLevel = R.Nanite;
	LumenQualityLevel = R.Lumen;
	ViewDistanceLevel = R.View;
	TextureQualityLevel = R.Tex;
	EffectsQualityLevel = R.Fx;
	PostProcessQualityLevel = R.PP;
	bMotionBlur = R.bBlur;
	SetFrameRateLimit(R.FpsLimit);
}

EMTAGraphicsPreset UMTAGameUserSettings::DetectHardwareAndApply()
{
	const FString Gpu = FPlatformMisc::GetPrimaryGPUBrand().ToUpper();
	const uint64 RamGb = FPlatformMemory::GetConstants().TotalPhysicalGB;

	EMTAGraphicsPreset Result = EMTAGraphicsPreset::Medium;

	auto Has = [&Gpu](const TCHAR* Token) { return Gpu.Contains(Token); };

	if (Has(TEXT("5700")) || Has(TEXT("5600")) || Has(TEXT("2060")) || Has(TEXT("2070")) || Has(TEXT("1080")) || Has(TEXT("3050")) || Has(TEXT("6600")) || Has(TEXT("6650")))
	{
		// Zielhardware RX 5700 XT (kein Hardware-Raytracing): Mittel + TSR/FSR Ausgewogen.
		Result = EMTAGraphicsPreset::Medium;
	}
	else if (Has(TEXT("4090")) || Has(TEXT("4080")) || Has(TEXT("5090")) || Has(TEXT("5080")) || Has(TEXT("7900")) || Has(TEXT("6900")) || Has(TEXT("6950")) || Has(TEXT("9070")))
	{
		Result = EMTAGraphicsPreset::Ultra;
	}
	else if (Has(TEXT("RTX")) || Has(TEXT("RX 7")) || Has(TEXT("RX 6")) || Has(TEXT("RX 9")) || Has(TEXT("ARC A7")) || Has(TEXT("ARC B5")))
	{
		Result = EMTAGraphicsPreset::High;
	}
	else if (Has(TEXT("INTEL")) || Has(TEXT("UHD")) || Has(TEXT("IRIS")) || Has(TEXT("VEGA")) || Has(TEXT("GTX 9")) || Has(TEXT("GTX 10")) || Has(TEXT("RX 5")) || Has(TEXT("RX 4")) || Has(TEXT("GTX 16")))
	{
		// "RX 5" trifft RX 580 (Polaris); die 5700 wurde oben schon abgefangen.
		Result = EMTAGraphicsPreset::Low;
	}

	if (RamGb > 0 && RamGb < 12 && Result > EMTAGraphicsPreset::Low)
	{
		// Zu wenig Arbeitsspeicher fuer Nanite/Lumen-Szenen: eine Stufe tiefer.
		Result = static_cast<EMTAGraphicsPreset>(static_cast<uint8>(Result) - 1);
	}

	ApplyGraphicsPreset(Result);
	bHardwareDetected = true;
	UE_LOG(LogTemp, Log, TEXT("MTA6: Hardware '%s' (%llu GB RAM) -> Grafikpreset %d"), *Gpu, RamGb, static_cast<int32>(Result));
	return Result;
}

void UMTAGameUserSettings::ApplyMTAGraphics()
{
	// Scalability-Gruppen ueber die Basisklasse setzen (werden von ApplyNonResolutionSettings angewendet).
	SetShadowQuality(ShadowQualityLevel);
	SetGlobalIlluminationQuality(GIQualityLevel);
	SetViewDistanceQuality(ViewDistanceLevel);
	SetTextureQuality(TextureQualityLevel);
	SetVisualEffectQuality(EffectsQualityLevel);
	SetPostProcessingQuality(PostProcessQualityLevel);
	SetReflectionQuality(FMath::Clamp(LumenQualityLevel + 1, 0, 4));

	// Upscaler: TSR (4) fuer TSR und FSR, TAA (2) fuer Nativ.
	const bool bWantsFSR = (Upscaler == EMTAUpscaler::FSR) && IsFSRAvailable();
	SetCVarInt(TEXT("r.AntiAliasingMethod"), Upscaler == EMTAUpscaler::NativeTAA ? 2 : 4);
	for (const TCHAR* Name : GFSRCVars)
	{
		SetCVarInt(Name, bWantsFSR ? 1 : 0);
	}
	SetCVarFloat(TEXT("r.ScreenPercentage"), GetEffectiveScreenPercentage() * 100.f);

	// Nanite: weniger Dreiecke pro Pixel bei niedriger Stufe.
	static const float NanitePixelsPerEdge[4] = { 4.f, 2.f, 1.25f, 1.f };
	SetCVarFloat(TEXT("r.Nanite.MaxPixelsPerEdge"), NanitePixelsPerEdge[FMath::Clamp(NaniteQualityLevel, 0, 3)]);

	// Lumen: Strahlendichte der Screen-Probes und Reflexionen.
	static const int32 LumenTraceRes[4] = { 4, 6, 8, 12 };
	SetCVarInt(TEXT("r.Lumen.ScreenProbeGather.TracingOctahedronResolution"), LumenTraceRes[FMath::Clamp(LumenQualityLevel, 0, 3)]);
	SetCVarInt(TEXT("r.Lumen.Reflections.Allow"), LumenQualityLevel >= 1 ? 1 : 0);

	// Bewegungsunschaerfe aus, wenn Spieler es wuenscht oder reduzierte Kamerabewegung aktiv ist.
	SetCVarInt(TEXT("r.MotionBlurQuality"), (bMotionBlur && !bReduceCameraMotion) ? 4 : 0);
}

void UMTAGameUserSettings::ApplySettings(bool bCheckForCommandLineOverrides)
{
	ApplyMTAGraphics();            // setzt die Scalability-Werte vor dem Anwenden
	Super::ApplySettings(bCheckForCommandLineOverrides);
	ApplyMTAGraphics();            // CVars erneut, falls die Basisklasse sie ueberschrieben hat
}
