#include "MTAPhotoMode.h"

#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Containers/Ticker.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "MTALocalizationSubsystem.h"
#include "MTAMenuStyle.h"

namespace
{
	struct FFilterPreset { float Contrast, Saturation, Grain, Vignette, Temp; };
	// None, Vivid, Noir, Vintage, Cold, Warm, Cinematic
	const FFilterPreset GFilters[] = {
		{ 1.00f, 1.00f, 0.00f, 0.20f, 6500.f },
		{ 1.15f, 1.40f, 0.00f, 0.15f, 6500.f },
		{ 1.30f, 0.00f, 0.35f, 0.40f, 6500.f },
		{ 0.90f, 0.70f, 0.30f, 0.45f, 5200.f },
		{ 1.05f, 0.90f, 0.05f, 0.20f, 8500.f },
		{ 1.05f, 1.10f, 0.05f, 0.25f, 4800.f },
		{ 1.20f, 0.85f, 0.15f, 0.50f, 6000.f },
	};
	const TCHAR* const GFilterKeys[] = { TEXT("photo.filter.none"), TEXT("photo.filter.vivid"), TEXT("photo.filter.noir"), TEXT("photo.filter.vintage"), TEXT("photo.filter.cold"), TEXT("photo.filter.warm"), TEXT("photo.filter.cinematic") };
}

// ----------------------------------------------------------------------------- Kamera

AMTAPhotoCamera::AMTAPhotoCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetTickableWhenPaused(true);
}

void AMTAPhotoCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UMTAPhotoModeSubsystem* Sys = Owner.Get();
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!Sys || !PC) { return; }

	const float Dt = FMath::Min(FApp::GetDeltaTime(), 0.1f); // echte Zeit, unabhaengig von Pause

	// Sperre waehrend des Fotomodus erneut pruefen (z. B. wenn die Zeit weiterlaeuft).
	LockCheckTimer += Dt;
	if (LockCheckTimer > 0.5f)
	{
		LockCheckTimer = 0.f;
		FString Reason;
		if (!Sys->IsTimeFrozen() && Sys->IsLocked(Reason)) { Sys->ExitPhotoMode(); return; }
	}

	// Hotkeys
	if (PC->WasInputKeyJustPressed(EKeys::Escape) || PC->WasInputKeyJustPressed(EKeys::F6) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right))
	{
		Sys->ExitPhotoMode(); return;
	}
	if (PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::P) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom))
	{
		Sys->TakePhoto();
	}
	if (PC->WasInputKeyJustPressed(EKeys::H) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top))
	{
		Sys->SetPanelVisible(!Sys->IsPanelVisible());
	}
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp)) { Sys->Params.Fov = FMath::Clamp(Sys->Params.Fov - 3.f, 20.f, 110.f); Sys->ApplyParams(); }
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown)) { Sys->Params.Fov = FMath::Clamp(Sys->Params.Fov + 3.f, 20.f, 110.f); Sys->ApplyParams(); }

	// Drehen: Pfeiltasten, rechte Maustaste + Maus, rechter Stick
	FRotator Rot = GetActorRotation();
	float Yaw = 0.f, Pitch = 0.f;
	const float LookSpeed = 70.f * (Sys->Params.Fov / 70.f);
	if (PC->IsInputKeyDown(EKeys::Left))  { Yaw -= LookSpeed * Dt; }
	if (PC->IsInputKeyDown(EKeys::Right)) { Yaw += LookSpeed * Dt; }
	if (PC->IsInputKeyDown(EKeys::Up))    { Pitch += LookSpeed * Dt; }
	if (PC->IsInputKeyDown(EKeys::Down))  { Pitch -= LookSpeed * Dt; }
	Yaw += PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX) * 110.f * Dt;
	Pitch += PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY) * 90.f * Dt;
	if (PC->IsInputKeyDown(EKeys::RightMouseButton))
	{
		float Dx = 0.f, Dy = 0.f;
		PC->GetInputMouseDelta(Dx, Dy);
		Yaw += Dx * 0.15f * (Sys->Params.Fov / 70.f);
		Pitch += Dy * 0.15f * (Sys->Params.Fov / 70.f);
	}
	Rot.Yaw += Yaw;
	Rot.Pitch = FMath::Clamp(Rot.Pitch + Pitch, -89.f, 89.f);
	Rot.Roll = 0.f;
	SetActorRotation(Rot);

	// Bewegen
	FVector Move = FVector::ZeroVector;
	if (PC->IsInputKeyDown(EKeys::W)) { Move.X += 1.f; }
	if (PC->IsInputKeyDown(EKeys::S)) { Move.X -= 1.f; }
	if (PC->IsInputKeyDown(EKeys::D)) { Move.Y += 1.f; }
	if (PC->IsInputKeyDown(EKeys::A)) { Move.Y -= 1.f; }
	if (PC->IsInputKeyDown(EKeys::E)) { Move.Z += 1.f; }
	if (PC->IsInputKeyDown(EKeys::Q)) { Move.Z -= 1.f; }
	Move.X += PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	Move.Y += PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	Move.Z += PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) - PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis);
	if (!Move.IsNearlyZero())
	{
		float Speed = MoveSpeed;
		if (PC->IsInputKeyDown(EKeys::LeftShift)) { Speed *= 4.f; }
		if (PC->IsInputKeyDown(EKeys::LeftControl)) { Speed *= 0.2f; }
		const FRotator YawOnly(0.f, Rot.Yaw, 0.f);
		FVector World = Rot.RotateVector(FVector(Move.X, 0.f, 0.f)) + FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y) * Move.Y + FVector::UpVector * Move.Z;
		SetActorLocation(GetActorLocation() + World.GetClampedToMaxSize(1.f) * Speed * Dt);
	}
}

// ------------------------------------------------------------------------------ Panel

class SMTAPhotoPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMTAPhotoPanel) {}
		SLATE_ARGUMENT(UMTAPhotoModeSubsystem*, Sys)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		UMTAPhotoModeSubsystem* Sys = InArgs._Sys;
		using namespace MTAUI;
		const FMTAUiPalette P = Pal();
		auto Fmt = [](const FString& S) { return FText::FromString(S); };
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		auto Add = [&V](TSharedRef<SWidget> W) { V->AddSlot().AutoHeight()[ W ]; };

		Add(SliderRow(TEXT("photo.fov"), 20.f, 110.f, 1.f, [Sys]() { return Sys->Params.Fov; }, [Sys](float X) { Sys->Params.Fov = X; Sys->ApplyParams(); }, [Fmt](float X) { return Fmt(FString::Printf(TEXT("%.0f deg"), X)); }));
		Add(ToggleRow(TEXT("photo.dof"), [Sys]() { return Sys->Params.bDepthOfField; }, [Sys](bool B) { Sys->Params.bDepthOfField = B; Sys->ApplyParams(); }));
		Add(SliderRow(TEXT("photo.aperture"), 1.4f, 22.f, 0.1f, [Sys]() { return Sys->Params.Aperture; }, [Sys](float X) { Sys->Params.Aperture = X; Sys->ApplyParams(); }, [Fmt](float X) { return Fmt(FString::Printf(TEXT("f/%.1f"), X)); }));
		Add(SliderRow(TEXT("photo.focus"), 100.f, 50000.f, 50.f, [Sys]() { return Sys->Params.FocusDistance; }, [Sys](float X) { Sys->Params.FocusDistance = X; Sys->ApplyParams(); }, [Fmt](float X) { return Fmt(FString::Printf(TEXT("%.1f m"), X / 100.f)); }));
		Add(SliderRow(TEXT("photo.exposure"), -3.f, 3.f, 0.1f, [Sys]() { return Sys->Params.Exposure; }, [Sys](float X) { Sys->Params.Exposure = X; Sys->ApplyParams(); }, [Fmt](float X) { return Fmt(FString::Printf(TEXT("%+.1f EV"), X)); }));
		Add(ChoiceRow(TEXT("photo.filter"), { GFilterKeys[0], GFilterKeys[1], GFilterKeys[2], GFilterKeys[3], GFilterKeys[4], GFilterKeys[5], GFilterKeys[6] },
			[Sys]() { return (int32)Sys->Params.Filter; }, [Sys](int32 I) { Sys->SetFilter((EMTAPhotoFilter)I); }));
		Add(SliderRow(TEXT("photo.contrast"), 0.5f, 1.5f, 0.05f, [Sys]() { return Sys->Params.Contrast; }, [Sys](float X) { Sys->Params.Contrast = X; Sys->ApplyParams(); }, [Fmt](float X) { return Fmt(FString::Printf(TEXT("%.2f"), X)); }));
		Add(SliderRow(TEXT("photo.saturation"), 0.f, 2.f, 0.05f, [Sys]() { return Sys->Params.Saturation; }, [Sys](float X) { Sys->Params.Saturation = X; Sys->ApplyParams(); }, [Fmt](float X) { return Fmt(FString::Printf(TEXT("%.2f"), X)); }));
		Add(SliderRow(TEXT("photo.grain"), 0.f, 1.f, 0.05f, [Sys]() { return Sys->Params.Grain; }, [Sys](float X) { Sys->Params.Grain = X; Sys->ApplyParams(); }, [Fmt](float X) { return Fmt(FString::Printf(TEXT("%.2f"), X)); }));
		Add(SliderRow(TEXT("photo.vignette"), 0.f, 1.f, 0.05f, [Sys]() { return Sys->Params.Vignette; }, [Sys](float X) { Sys->Params.Vignette = X; Sys->ApplyParams(); }, [Fmt](float X) { return Fmt(FString::Printf(TEXT("%.2f"), X)); }));
		Add(SliderRow(TEXT("photo.temperature"), 3000.f, 10000.f, 100.f, [Sys]() { return Sys->Params.Temperature; }, [Sys](float X) { Sys->Params.Temperature = X; Sys->ApplyParams(); }, [Fmt](float X) { return Fmt(FString::Printf(TEXT("%.0f K"), X)); }));
		Add(SliderRow(TEXT("photo.timeofday"), 0.f, 24.f, 0.25f, [Sys]() { return Sys->Params.TimeOfDay; }, [Sys](float X) { Sys->Params.TimeOfDay = X; Sys->ApplyParams(); },
			[](float X) { const int32 M = FMath::RoundToInt(X * 60.f); return FText::FromString(FString::Printf(TEXT("%02d:%02d"), (M / 60) % 24, M % 60)); }));
		Add(ChoiceRow(TEXT("photo.weather"), { TEXT("photo.weather.clear"), TEXT("photo.weather.cloudy"), TEXT("photo.weather.rain"), TEXT("photo.weather.fog"), TEXT("photo.weather.storm") },
			[Sys]() { return Sys->Params.Weather; }, [Sys](int32 I) { Sys->Params.Weather = I; Sys->ApplyParams(); }));
		Add(ToggleRow(TEXT("photo.freeze"), [Sys]() { return Sys->IsTimeFrozen(); }, [Sys](bool B) { Sys->SetFreezeTime(B); }));
		Add(ToggleRow(TEXT("photo.hide_hud"), [Sys]() { return Sys->IsHudHidden(); }, [Sys](bool B) { Sys->SetHideHud(B); }));

		ChildSlot
		[
			SNew(SBox).HAlign(HAlign_Right).Padding(FMargin(0.f, 24.f, 24.f, 24.f))
			[
				SNew(SBox).WidthOverride(520.f)
				[
					Panel(
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ Label(TrAttr(TEXT("photo.title")), 26.f, false, true) ]
						+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 8.f)[ SNew(SScrollBox) + SScrollBox::Slot()[ V ] ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)[ Button(TrAttr(TEXT("photo.take")), FOnClicked::CreateLambda([Sys]() { Sys->TakePhoto(); return FReply::Handled(); }), 120.f, true) ]
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)[ Button(TrAttr(TEXT("photo.hide_panel")), FOnClicked::CreateLambda([Sys]() { Sys->SetPanelVisible(false); return FReply::Handled(); }), 120.f) ]
							+ SHorizontalBox::Slot().FillWidth(1.f)[ Button(TrAttr(TEXT("photo.exit")), FOnClicked::CreateLambda([Sys]() { Sys->ExitPhotoMode(); return FReply::Handled(); }), 120.f) ]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)[ Label(TrAttr(TEXT("photo.controls_hint")), 12.f, true) ],
						false, 16.f)
				]
			]
		];
	}
};

// ------------------------------------------------------------------------- Subsystem

UMTAPhotoModeSubsystem* UMTAPhotoModeSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UMTAPhotoModeSubsystem>() : nullptr;
}

void UMTAPhotoModeSubsystem::Deinitialize()
{
	if (bActive) { ExitPhotoMode(); }
	Super::Deinitialize();
}

bool UMTAPhotoModeSubsystem::IsLocked(FString& OutReasonKey) const
{
	return LockQuery.IsBound() && LockQuery.Execute(OutReasonKey);
}

void UMTAPhotoModeSubsystem::ShowMessage(const FString& Key)
{
	if (!GEngine || !GEngine->GameViewport) { return; }
	if (MessageWidget.IsValid()) { GEngine->GameViewport->RemoveViewportWidgetContent(MessageWidget.ToSharedRef()); }
	using namespace MTAUI;
	MessageWidget = SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0.f, 0.f, 0.f, 140.f))
		[ Panel(Label(TrAttr(Key), 18.f, false, true), false, 16.f) ];
	GEngine->GameViewport->AddViewportWidgetContent(MessageWidget.ToSharedRef(), 96);
	TWeakPtr<SWidget> Weak = MessageWidget;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
	{
		if (TSharedPtr<SWidget> W = Weak.Pin())
		{
			if (GEngine && GEngine->GameViewport) { GEngine->GameViewport->RemoveViewportWidgetContent(W.ToSharedRef()); }
		}
		return false;
	}), 3.f);
}

bool UMTAPhotoModeSubsystem::EnterPhotoMode()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	if (bActive || !PC) { return false; }

	FString Reason;
	if (IsLocked(Reason))
	{
		ShowMessage(Reason.IsEmpty() ? TEXT("photo.locked") : Reason);
		return false;
	}

	FVector Loc; FRotator Rot;
	PC->GetPlayerViewPoint(Loc, Rot);
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Camera = World->SpawnActor<AMTAPhotoCamera>(Loc, FRotator(Rot.Pitch, Rot.Yaw, 0.f), SP);
	if (!Camera) { return false; }
	Camera->Owner = this;
	Camera->GetCameraComponent()->bConstrainAspectRatio = false;

	PreviousViewTarget = PC->GetViewTarget();
	PC->SetViewTargetWithBlend(Camera, 0.f);

	// Fahrzeug darf waehrend des Fotos keine Eingaben bekommen.
	if (APawn* Pawn = PC->GetPawn()) { Pawn->DisableInput(PC); DisabledPawn = Pawn; }

	bFrozen = true;
	bPausedByUs = false;
	if (!UGameplayStatics::IsGamePaused(World)) { UGameplayStatics::SetGamePaused(World, true); bPausedByUs = true; }

	bHudWasVisible = PC->GetHUD() ? PC->GetHUD()->bShowHUD : true;
	bHideHud = true;                // HUD ist im Fotomodus standardmaessig aus
	if (PC->GetHUD()) { PC->GetHUD()->bShowHUD = false; }

	Params = FMTAPhotoParams();
	Params.Fov = Camera->GetCameraComponent()->FieldOfView;
	bPanelVisible = true;
	bActive = true;

	if (GEngine && GEngine->GameViewport)
	{
		PanelWidget = SNew(SMTAPhotoPanel).Sys(this);
		GEngine->GameViewport->AddViewportWidgetContent(PanelWidget.ToSharedRef(), 97);
	}
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);

	ApplyParams();
	OnPhotoModeChanged.Broadcast(true);
	return true;
}

void UMTAPhotoModeSubsystem::ExitPhotoMode()
{
	if (!bActive) { return; }
	bActive = false;
	UWorld* World = GetWorld();
	APlayerController* PC = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;

	if (PanelWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(PanelWidget.ToSharedRef());
	}
	PanelWidget.Reset();

	if (PC)
	{
		if (AActor* Prev = PreviousViewTarget.Get()) { PC->SetViewTargetWithBlend(Prev, 0.f); }
		if (APawn* Pawn = DisabledPawn.Get()) { Pawn->EnableInput(PC); }
		if (PC->GetHUD()) { PC->GetHUD()->bShowHUD = bHudWasVisible; }
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
	if (World && bPausedByUs) { UGameplayStatics::SetGamePaused(World, false); }
	bPausedByUs = false;

	if (Camera) { Camera->Destroy(); Camera = nullptr; }
	// Hooks: urspruengliche Tageszeit und Wetter wiederherstellen.
	OnTimeOfDayRequested.Broadcast(-1.f);
	OnWeatherRequested.Broadcast(-1);
	OnPhotoModeChanged.Broadcast(false);
}

void UMTAPhotoModeSubsystem::SetFilter(EMTAPhotoFilter NewFilter)
{
	Params.Filter = NewFilter;
	const FFilterPreset& F = GFilters[FMath::Clamp((int32)NewFilter, 0, (int32)EMTAPhotoFilter::Count - 1)];
	Params.Contrast = F.Contrast; Params.Saturation = F.Saturation; Params.Grain = F.Grain; Params.Vignette = F.Vignette; Params.Temperature = F.Temp;
	ApplyParams();
}

void UMTAPhotoModeSubsystem::SetFreezeTime(bool bFreeze)
{
	bFrozen = bFreeze;
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGamePaused(World, bFreeze);
		bPausedByUs = bFreeze || bPausedByUs;
	}
}

void UMTAPhotoModeSubsystem::SetPanelVisible(bool bVisible)
{
	bPanelVisible = bVisible;
	if (PanelWidget.IsValid()) { PanelWidget->SetVisibility(bVisible ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed); }
}

void UMTAPhotoModeSubsystem::SetHideHud(bool bHide)
{
	bHideHud = bHide;
	if (APlayerController* PC = GetWorld() ? UGameplayStatics::GetPlayerController(GetWorld(), 0) : nullptr)
	{
		if (PC->GetHUD()) { PC->GetHUD()->bShowHUD = !bHide; }
	}
}

void UMTAPhotoModeSubsystem::ApplyParams()
{
	if (!Camera) { return; }
	UCameraComponent* Cam = Camera->GetCameraComponent();
	Cam->SetFieldOfView(Params.Fov);
	Cam->PostProcessBlendWeight = 1.f;
	FPostProcessSettings& PP = Cam->PostProcessSettings;

	PP.bOverride_DepthOfFieldFocalDistance = true;
	PP.DepthOfFieldFocalDistance = Params.bDepthOfField ? Params.FocusDistance : 0.f;
	PP.bOverride_DepthOfFieldFstop = true;
	// Ohne Tiefenschaerfe: sehr grosse Blende (f/32) = praktisch alles scharf.
	PP.DepthOfFieldFstop = Params.bDepthOfField ? Params.Aperture : 32.f;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = Params.Exposure;
	PP.bOverride_ColorContrast = true;
	PP.ColorContrast = FVector4(Params.Contrast, Params.Contrast, Params.Contrast, 1.0);
	PP.bOverride_ColorSaturation = true;
	PP.ColorSaturation = FVector4(Params.Saturation, Params.Saturation, Params.Saturation, 1.0);
	PP.bOverride_FilmGrainIntensity = true;
	PP.FilmGrainIntensity = Params.Grain;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = Params.Vignette;
	PP.bOverride_WhiteTemp = true;
	PP.WhiteTemp = Params.Temperature;

	OnTimeOfDayRequested.Broadcast(Params.TimeOfDay);
	OnWeatherRequested.Broadcast(Params.Weather);
}

void UMTAPhotoModeSubsystem::TakePhoto()
{
	const FString Name = FString::Printf(TEXT("MTA6_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MTA6_Photos"), Name));
	LastPhotoPath = Path;
	// bShowUI = false: Slate-Panel und (per bShowHUD) das HUD fehlen auf dem Bild.
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	ShowMessage(TEXT("photo.saved"));
}
