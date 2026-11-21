#include "MTAAccessibilitySubsystem.h"

#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/PackageName.h"
#include "Engine/GameInstance.h"
#include "MTASettingsSubsystem.h"
#include "MTASettingsUser.h"

UMTAAccessibilitySubsystem* UMTAAccessibilitySubsystem::ActiveInstance = nullptr;

namespace
{
	using FMat3 = float[3][3];

	void Identity(FMat3 M)
	{
		for (int32 r = 0; r < 3; ++r) { for (int32 c = 0; c < 3; ++c) { M[r][c] = (r == c) ? 1.f : 0.f; } }
	}

	void Multiply(const FMat3 A, const FMat3 B, FMat3 Out)
	{
		for (int32 r = 0; r < 3; ++r)
		{
			for (int32 c = 0; c < 3; ++c)
			{
				Out[r][c] = A[r][0] * B[0][c] + A[r][1] * B[1][c] + A[r][2] * B[2][c];
			}
		}
	}

	/**
	 * Daltonisierung: Ausgabe = C + Shift * (C - Sim * C) = (I + Shift * (I - Sim)) * C.
	 * Sim = Simulationsmatrix nach Machado et al. 2009 (Schweregrad 1.0), Shift verlagert den
	 * nicht sichtbaren Farbanteil in die erkennbaren Kanaele.
	 */
	void BuildCorrection(EMTAColorBlindMode Mode, float Strength, FMat3 Out)
	{
		static const FMat3 SimProtan = { {0.152286f, 1.052583f, -0.204868f}, {0.114503f, 0.786281f, 0.099216f}, {-0.003882f, -0.048116f, 1.051998f} };
		static const FMat3 SimDeutan = { {0.367322f, 0.860646f, -0.227968f}, {0.280085f, 0.672501f, 0.047413f}, {-0.011820f, 0.042940f, 0.968881f} };
		static const FMat3 SimTritan = { {1.255528f, -0.076749f, -0.178779f}, {-0.078411f, 0.930809f, 0.147602f}, {0.004733f, 0.691367f, 0.303900f} };
		static const FMat3 ShiftRG   = { {0.f, 0.f, 0.f}, {0.7f, 1.f, 0.f}, {0.7f, 0.f, 1.f} };
		static const FMat3 ShiftB    = { {1.f, 0.f, 0.7f}, {0.f, 1.f, 0.7f}, {0.f, 0.f, 0.f} };

		Identity(Out);
		if (Mode == EMTAColorBlindMode::Off)
		{
			return;
		}
		const float (*Sim)[3] = (Mode == EMTAColorBlindMode::Protanopia) ? SimProtan : (Mode == EMTAColorBlindMode::Deuteranopia ? SimDeutan : SimTritan);
		const float (*Shift)[3] = (Mode == EMTAColorBlindMode::Tritanopia) ? ShiftB : ShiftRG;

		FMat3 I, IminusSim, Delta, Full;
		Identity(I);
		for (int32 r = 0; r < 3; ++r) { for (int32 c = 0; c < 3; ++c) { IminusSim[r][c] = I[r][c] - Sim[r][c]; } }
		Multiply(Shift, IminusSim, Delta);
		for (int32 r = 0; r < 3; ++r) { for (int32 c = 0; c < 3; ++c) { Full[r][c] = I[r][c] + Delta[r][c]; } }
		for (int32 r = 0; r < 3; ++r) { for (int32 c = 0; c < 3; ++c) { Out[r][c] = FMath::Lerp(I[r][c], Full[r][c], Strength); } }
	}
}

void UMTAAccessibilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ActiveInstance = this;
	SettingsSub = Collection.InitializeDependency<UMTASettingsSubsystem>();
	if (SettingsSub)
	{
		SettingsHandle = SettingsSub->OnSettingsChanged.AddUObject(this, &UMTAAccessibilitySubsystem::Refresh);
	}
	WorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddWeakLambda(this, [this](UWorld* World, const UWorld::InitializationValues)
	{
		PushColorBlindToWorld(World);
	});
	Refresh();
}

void UMTAAccessibilitySubsystem::Deinitialize()
{
	if (SettingsSub)
	{
		SettingsSub->OnSettingsChanged.Remove(SettingsHandle);
	}
	FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitHandle);
	if (ActiveInstance == this)
	{
		ActiveInstance = nullptr;
	}
	Super::Deinitialize();
}

void UMTAAccessibilitySubsystem::Refresh()
{
	const UMTAGameUserSettings* S = SettingsSub ? SettingsSub->GetSettings() : nullptr;
	if (S)
	{
		FontScale = FMath::Clamp(S->UiScale, 0.75f, 1.5f) * (S->bLargeText ? 1.3f : 1.f);
		static const float SubScale[3] = { 0.85f, 1.f, 1.3f };
		SubtitleScale = SubScale[FMath::Clamp(S->SubtitleSize, 0, 2)] * (S->bLargeText ? 1.2f : 1.f);
		bHighContrast = S->bHighContrast;
		bSubtitleBackground = S->bSubtitleBackground;
		bReduceCameraMotion = S->bReduceCameraMotion;
		bReduceFlashes = S->bReduceFlashes;
		bOneButtonMode = S->bOneButtonMode;
		bSteeringAssist = S->bSteeringAssist;
		AssistStrength = FMath::Clamp(S->SteeringAssistStrength, 0.f, 1.f);
		ColorBlindMode = S->ColorBlindMode;
		ColorBlindStrength = FMath::Clamp(S->ColorBlindStrength, 0.f, 1.f);
	}

	// Palette: Standard dunkel-blaugrau mit Bernstein-Akzent, Hoher Kontrast schwarz/weiss/gelb.
	if (bHighContrast)
	{
		Palette.Background    = FLinearColor(0.f, 0.f, 0.f, 0.96f);
		Palette.Panel         = FLinearColor(0.f, 0.f, 0.f, 1.f);
		Palette.PanelAlt      = FLinearColor(0.05f, 0.05f, 0.05f, 1.f);
		Palette.Button        = FLinearColor(0.f, 0.f, 0.f, 1.f);
		Palette.ButtonSelected = FLinearColor(1.f, 0.9f, 0.f, 1.f);
		Palette.Text          = FLinearColor::White;
		Palette.TextDim       = FLinearColor(0.85f, 0.85f, 0.85f, 1.f);
		Palette.Accent        = FLinearColor(1.f, 0.9f, 0.f, 1.f);
		Palette.Warning       = FLinearColor(1.f, 0.35f, 0.1f, 1.f);
		Palette.Good          = FLinearColor(0.2f, 1.f, 0.5f, 1.f);
	}
	else
	{
		Palette.Background    = FLinearColor(0.008f, 0.012f, 0.02f, 0.88f);
		Palette.Panel         = FLinearColor(0.02f, 0.03f, 0.045f, 0.95f);
		Palette.PanelAlt      = FLinearColor(0.035f, 0.05f, 0.07f, 0.95f);
		Palette.Button        = FLinearColor(0.05f, 0.07f, 0.1f, 1.f);
		Palette.ButtonSelected = FLinearColor(0.9f, 0.45f, 0.08f, 1.f);
		Palette.Text          = FLinearColor(0.92f, 0.94f, 0.96f, 1.f);
		Palette.TextDim       = FLinearColor(0.55f, 0.6f, 0.66f, 1.f);
		Palette.Accent        = FLinearColor(0.95f, 0.5f, 0.1f, 1.f);
		Palette.Warning       = FLinearColor(0.9f, 0.15f, 0.12f, 1.f);
		Palette.Good          = FLinearColor(0.25f, 0.8f, 0.45f, 1.f);
	}
	// Farbige Signalfarben durch den Farbenblind-Filter schicken.
	Palette.Accent  = TransformColor(Palette.Accent);
	Palette.Warning = TransformColor(Palette.Warning);
	Palette.Good    = TransformColor(Palette.Good);

	if (const UGameInstance* GI = GetGameInstance())
	{
		PushColorBlindToWorld(GI->GetWorld());
	}
	OnAccessibilityChanged.Broadcast();
}

void UMTAAccessibilitySubsystem::GetColorBlindMatrixRows(FVector3f& OutRow0, FVector3f& OutRow1, FVector3f& OutRow2) const
{
	float M[3][3];
	BuildCorrection(ColorBlindMode, ColorBlindStrength, M);
	OutRow0 = FVector3f(M[0][0], M[0][1], M[0][2]);
	OutRow1 = FVector3f(M[1][0], M[1][1], M[1][2]);
	OutRow2 = FVector3f(M[2][0], M[2][1], M[2][2]);
}

FLinearColor UMTAAccessibilitySubsystem::TransformColor(const FLinearColor& Color) const
{
	if (ColorBlindMode == EMTAColorBlindMode::Off)
	{
		return Color;
	}
	float M[3][3];
	BuildCorrection(ColorBlindMode, ColorBlindStrength, M);
	return FLinearColor(
		FMath::Clamp(M[0][0] * Color.R + M[0][1] * Color.G + M[0][2] * Color.B, 0.f, 1.f),
		FMath::Clamp(M[1][0] * Color.R + M[1][1] * Color.G + M[1][2] * Color.B, 0.f, 1.f),
		FMath::Clamp(M[2][0] * Color.R + M[2][1] * Color.G + M[2][2] * Color.B, 0.f, 1.f),
		Color.A);
}

void UMTAAccessibilitySubsystem::PushColorBlindToWorld(UWorld* World) const
{
	if (!World)
	{
		return;
	}
	static const FSoftObjectPath CollectionPath(TEXT("/Game/Materials/MPC_Accessibility.MPC_Accessibility"));
	// Nur laden, wenn das Asset existiert (kein Fehler im Log, wenn es fehlt).
	UMaterialParameterCollection* Collection = Cast<UMaterialParameterCollection>(CollectionPath.ResolveObject());
	if (!Collection)
	{
		if (!FPackageName::DoesPackageExist(CollectionPath.GetLongPackageName()))
		{
			return;
		}
		Collection = Cast<UMaterialParameterCollection>(CollectionPath.TryLoad());
	}
	if (!Collection)
	{
		return;
	}
	FVector3f R0, R1, R2;
	GetColorBlindMatrixRows(R0, R1, R2);
	if (UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection))
	{
		Instance->SetVectorParameterValue(TEXT("CB_Row0"), FLinearColor(R0.X, R0.Y, R0.Z, 0.f));
		Instance->SetVectorParameterValue(TEXT("CB_Row1"), FLinearColor(R1.X, R1.Y, R1.Z, 0.f));
		Instance->SetVectorParameterValue(TEXT("CB_Row2"), FLinearColor(R2.X, R2.Y, R2.Z, 0.f));
		Instance->SetScalarParameterValue(TEXT("CB_Enabled"), ColorBlindMode == EMTAColorBlindMode::Off ? 0.f : 1.f);
	}
}

float UMTAAccessibilitySubsystem::ApplySteeringAssist(float Steer, float SpeedKmh) const
{
	if (!bSteeringAssist)
	{
		return Steer;
	}
	const float SpeedAlpha = FMath::Clamp(SpeedKmh / 150.f, 0.f, 1.f);
	const float Reduction = 1.f - AssistStrength * 0.6f * SpeedAlpha;
	// Kleine Ausschlaege werden weicher (Exponent 1.0 -> 1.4 je nach Staerke).
	const float Exponent = 1.f + 0.4f * AssistStrength;
	return FMath::Sign(Steer) * FMath::Pow(FMath::Abs(Steer), Exponent) * Reduction;
}

bool UMTAAccessibilitySubsystem::ShouldShowFlashWarning() const
{
	const UMTAGameUserSettings* S = SettingsSub ? SettingsSub->GetSettings() : nullptr;
	return S && S->bShowFlashWarning;
}

void UMTAAccessibilitySubsystem::AcknowledgeFlashWarning()
{
	if (UMTAGameUserSettings* S = SettingsSub ? SettingsSub->GetSettings() : nullptr)
	{
		S->bShowFlashWarning = false;
		S->SaveConfig();
	}
}
