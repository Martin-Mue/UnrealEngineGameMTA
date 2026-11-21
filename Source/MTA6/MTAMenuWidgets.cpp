#include "MTAMenuWidgets.h"

#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "MTALocalizationSubsystem.h"
#include "MTAMenuStyle.h"
#include "MTASettingsSubsystem.h"
#include "MTASettingsUser.h"

using namespace MTAUI;

// ------------------------------------------------------------------------- Rahmen

void SMTAScreenFrame::Construct(const FArguments& InArgs)
{
	OnBack = InArgs._OnBack;
	ChildSlot[ InArgs._Content.Widget ];
}

FReply SMTAScreenFrame::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& E)
{
	if (E.GetKey() == EKeys::Escape || E.GetKey() == EKeys::Gamepad_FaceButton_Right)
	{
		if (OnBack.IsBound()) { OnBack.Execute(); return FReply::Handled(); }
	}
	return FReply::Unhandled();
}

// ---------------------------------------------------------------------- Buttonmenue

void SMTAButtonMenu::Construct(const FArguments& InArgs)
{
	const FMTAUiPalette P = Pal();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FMTAMenuItem& Item : InArgs._Items)
	{
		FSimpleDelegate Click = Item.OnClick;
		List->AddSlot().AutoHeight().Padding(0.f, 5.f)
		[
			Button(TrAttr(Item.LabelKey), FOnClicked::CreateLambda([Click]() { Click.ExecuteIfBound(); return FReply::Handled(); }), 320.f, false, Item.bEnabled)
		];
	}

	ChildSlot
	[
		SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FSlateColor(InArgs._bShowBackdrop ? P.Background : FLinearColor(0, 0, 0, 0.55f)))
		.Padding(0.f).HAlign(HAlign_Left)
		[
			SNew(SBox).MinDesiredWidth(480.f)
			[
				SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FSlateColor(P.Panel)).Padding(FMargin(48.f, 64.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ Label(TrAttr(InArgs._TitleKey), 44.f, false, true) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 32.f)[ Label(TrAttr(InArgs._SubtitleKey), 18.f, true) ]
					+ SVerticalBox::Slot().AutoHeight()[ List ]
					+ SVerticalBox::Slot().FillHeight(1.f)[ SNew(SSpacer) ]
					+ SVerticalBox::Slot().AutoHeight()[ Label(TrAttr(TEXT("menu.footer")), 13.f, true) ]
				]
			]
		]
	];
}

// ------------------------------------------------------------------------ Slotwahl

bool SMTASlotSelect::IsSelectable(const FMTASaveSlotInfo& Slot) const
{
	return bSaveMode ? !Slot.bAutosave : Slot.bExists;
}

void SMTASlotSelect::Construct(const FArguments& InArgs)
{
	bSaveMode = InArgs._bSaveMode;
	Slots = InArgs._Slots;
	OnChosen = InArgs._OnChosen;
	FSimpleDelegate OnBack = InArgs._OnBack;
	const FMTAUiPalette P = Pal();

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FMTASaveSlotInfo& Info = Slots[i];
		const bool bOk = IsSelectable(Info);
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(SButton).IsEnabled(bOk)
			.ButtonColorAndOpacity_Lambda([this, i, P]() { return FSlateColor(Selected == i ? P.ButtonSelected : P.Button); })
			.ContentPadding(FMargin(14.f, 10.f))
			.OnClicked_Lambda([this, i]() { Selected = i; bConfirmOverwrite = false; RebuildDetails(); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[ Label(FText::FromString(Info.bAutosave ? MTATr(TEXT("slots.autosave")).ToString() : Info.DisplayName), 18.f, false, true) ]
				+ SHorizontalBox::Slot().AutoWidth()[ Label(Info.bExists ? FText::FromString(Info.SavedAt.ToString(TEXT("%d.%m.%Y %H:%M"))) : MTATr(TEXT("slots.empty")), 15.f, true) ]
			]
		];
	}

	ChildSlot
	[
		SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FSlateColor(P.Background)).Padding(48.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 20.f)[ Label(TrAttr(bSaveMode ? TEXT("slots.title_save") : TEXT("slots.title_load")), 36.f, false, true) ]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.5f).Padding(0.f, 0.f, 24.f, 0.f)[ SNew(SScrollBox) + SScrollBox::Slot()[ List ] ]
				+ SHorizontalBox::Slot().FillWidth(0.5f)[ Panel(SAssignNew(Details, SVerticalBox), true, 24.f) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 20.f, 0.f, 0.f).HAlign(HAlign_Left)
			[ Button(TrAttr(TEXT("common.back")), FOnClicked::CreateLambda([OnBack]() { OnBack.ExecuteIfBound(); return FReply::Handled(); }), 200.f) ]
		]
	];
	RebuildDetails();
}

void SMTASlotSelect::RebuildDetails()
{
	Details->ClearChildren();
	const FMTAUiPalette P = Pal();
	if (!Slots.IsValidIndex(Selected))
	{
		Details->AddSlot().AutoHeight()[ Label(TrAttr(TEXT("slots.select_hint")), 17.f, true) ];
		return;
	}
	const FMTASaveSlotInfo& S = Slots[Selected];
	auto Line = [&](const TCHAR* Key, const FString& Value)
	{
		Details->AddSlot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.4f)[ Label(TrAttr(Key), 16.f, true) ]
			+ SHorizontalBox::Slot().FillWidth(0.6f)[ Label(FText::FromString(Value), 16.f, false, true) ]
		];
	};
	if (S.bExists)
	{
		const int32 Total = FMath::FloorToInt(S.PlaytimeSeconds);
		Line(TEXT("slots.playtime"), FString::Printf(TEXT("%d h %02d min"), Total / 3600, (Total / 60) % 60));
		Line(TEXT("slots.mission"), S.Mission.IsEmpty() ? TEXT("-") : S.Mission);
		Line(TEXT("slots.location"), S.Location.IsEmpty() ? TEXT("-") : S.Location);
		Line(TEXT("slots.date"), S.SavedAt.ToString(TEXT("%d.%m.%Y %H:%M")));
	}
	else
	{
		Details->AddSlot().AutoHeight()[ Label(TrAttr(TEXT("slots.empty")), 18.f, true) ];
	}

	if (bSaveMode && S.bExists && bConfirmOverwrite)
	{
		Details->AddSlot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)[ Label(TrAttr(TEXT("slots.confirm_overwrite")), 16.f, false, true) ];
	}
	FString SlotName = S.SlotName;
	const bool bNeedsConfirm = bSaveMode && S.bExists && !bConfirmOverwrite;
	Details->AddSlot().AutoHeight().Padding(0.f, 20.f, 0.f, 0.f).HAlign(HAlign_Left)
	[
		Button(TrAttr(bSaveMode ? (bNeedsConfirm ? TEXT("slots.overwrite") : TEXT("slots.save_here")) : TEXT("slots.load_here")),
			FOnClicked::CreateLambda([this, SlotName, bNeedsConfirm]()
			{
				if (bNeedsConfirm) { bConfirmOverwrite = true; RebuildDetails(); }
				else { OnChosen.ExecuteIfBound(SlotName); }
				return FReply::Handled();
			}), 260.f, true, IsSelectable(S))
	];
}

// ---------------------------------------------------------------------------- Optionen

int32 SMTAOptionsMenu::LastTab = 0;

namespace
{
	const FIntPoint GResolutions[] = { {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160} };
	const float GFpsLimits[] = { 30.f, 60.f, 90.f, 120.f, 144.f, 0.f };
	const float GUiScales[] = { 0.75f, 1.f, 1.25f, 1.5f };

	TArray<FString> Keys(const TCHAR* Prefix, int32 Num)
	{
		TArray<FString> Out;
		for (int32 i = 0; i < Num; ++i) { Out.Add(FString::Printf(TEXT("%s%d"), Prefix, i)); }
		return Out;
	}

	int32 NearestIndex(const float* Values, int32 Num, float V)
	{
		int32 Best = 0;
		for (int32 i = 1; i < Num; ++i) { if (FMath::Abs(Values[i] - V) < FMath::Abs(Values[Best] - V)) { Best = i; } }
		return Best;
	}

	FText Fixed(float V, int32 Decimals = 2)
	{
		return FText::FromString(FString::Printf(TEXT("%.*f"), Decimals, V));
	}
}

void SMTAOptionsMenu::NotifyChanged()
{
	if (Sub) { Sub->OnSettingsChanged.Broadcast(); }
}

void SMTAOptionsMenu::Construct(const FArguments& InArgs)
{
	Sub = UMTASettingsSubsystem::GetActive();
	S = Sub ? Sub->GetSettings() : nullptr;
	OnRebuild = InArgs._OnRebuildRequested;
	FSimpleDelegate OnBack = InArgs._OnBack;
	const FMTAUiPalette P = Pal();

	if (!S)
	{
		ChildSlot[ Panel(Label(FText::FromString(TEXT("Settings subsystem missing")), 18.f), false, 24.f) ];
		return;
	}

	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	static const TCHAR* const TabKeys[] = { TEXT("opt.tab.graphics"), TEXT("opt.tab.audio"), TEXT("opt.tab.controls"), TEXT("opt.tab.gameplay"), TEXT("opt.tab.access") };
	for (int32 i = 0; i < 5; ++i)
	{
		Tabs->AddSlot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SButton)
			.ButtonColorAndOpacity_Lambda([i, P]() { return FSlateColor(LastTab == i ? P.ButtonSelected : P.Button); })
			.ContentPadding(FMargin(16.f, 8.f))
			.OnClicked_Lambda([this, i]() { LastTab = i; Switcher->SetActiveWidgetIndex(i); return FReply::Handled(); })
			[ SNew(STextBlock).Text(TrAttr(TabKeys[i])).Font(Font(17.f, true)).ColorAndOpacity(FSlateColor(P.Text)) ]
		];
	}

	auto Scroll = [](TSharedRef<SWidget> W) -> TSharedRef<SWidget> { return SNew(SScrollBox) + SScrollBox::Slot().Padding(0.f, 0.f, 16.f, 0.f)[ W ]; };

	ChildSlot
	[
		SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FSlateColor(P.Background)).Padding(40.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)[ Label(TrAttr(TEXT("opt.title")), 36.f, false, true) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)[ Tabs ]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				Panel(
					SAssignNew(Switcher, SWidgetSwitcher).WidgetIndex(LastTab)
					+ SWidgetSwitcher::Slot()[ Scroll(BuildGraphics()) ]
					+ SWidgetSwitcher::Slot()[ Scroll(BuildAudio()) ]
					+ SWidgetSwitcher::Slot()[ Scroll(BuildControls()) ]
					+ SWidgetSwitcher::Slot()[ Scroll(BuildGameplay()) ]
					+ SWidgetSwitcher::Slot()[ Scroll(BuildAccessibility()) ],
					true, 20.f)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
				[ Button(TrAttr(TEXT("opt.apply_save")), FOnClicked::CreateLambda([this]() { Sub->ApplyAndSave(); return FReply::Handled(); }), 260.f, true) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
				[ Button(TrAttr(TEXT("opt.defaults")), FOnClicked::CreateLambda([this]() { Sub->ResetAllToDefaults(); OnRebuild.ExecuteIfBound(); return FReply::Handled(); }), 220.f) ]
				+ SHorizontalBox::Slot().AutoWidth()
				[ Button(TrAttr(TEXT("common.back")), FOnClicked::CreateLambda([this, OnBack]() { Sub->ApplyAndSave(); OnBack.ExecuteIfBound(); return FReply::Handled(); }), 200.f) ]
			]
		]
	];
}

TSharedRef<SWidget> SMTAOptionsMenu::BuildGraphics()
{
	UMTASettingsSubsystem* Su = Sub; UMTAGameUserSettings* St = S;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	auto Add = [&V](TSharedRef<SWidget> W) { V->AddSlot().AutoHeight()[ W ]; };
	auto Custom = [Su]() { Su->MarkCustomPreset(); };
	const TArray<FString> Q5 = Keys(TEXT("opt.q5."), 5);
	const TArray<FString> Q4 = Keys(TEXT("opt.q4."), 4);

	Add(ChoiceRow(TEXT("opt.gfx.preset"), { TEXT("opt.preset.low"), TEXT("opt.preset.medium"), TEXT("opt.preset.high"), TEXT("opt.preset.ultra"), TEXT("opt.preset.custom") },
		[St]() { return (int32)St->GraphicsPreset; }, [Su](int32 I) { Su->ApplyPreset((EMTAGraphicsPreset)I); }));
	Add(Row(TEXT("opt.gfx.hardware"), SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[ Label(FText::FromString(Su->GetDetectedHardwareName()), 13.f, true) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Button(TrAttr(TEXT("opt.gfx.detect")), FOnClicked::CreateLambda([Su]() { Su->AutoDetectHardware(); return FReply::Handled(); }), 140.f) ]));

	Add(ChoiceRow(TEXT("opt.gfx.windowmode"), { TEXT("opt.wm.fullscreen"), TEXT("opt.wm.borderless"), TEXT("opt.wm.windowed") },
		[St]() { const EWindowMode::Type M = St->GetFullscreenMode(); return M == EWindowMode::Fullscreen ? 0 : (M == EWindowMode::WindowedFullscreen ? 1 : 2); },
		[St](int32 I) { St->SetFullscreenMode(I == 0 ? EWindowMode::Fullscreen : (I == 1 ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed)); }));

	TArray<FString> ResKeys;
	for (const FIntPoint& R : GResolutions) { ResKeys.Add(FString::Printf(TEXT("res.%dx%d"), R.X, R.Y)); }
	Add(ChoiceRow(TEXT("opt.gfx.resolution"), ResKeys,
		[St]() { const FIntPoint Cur = St->GetScreenResolution(); for (int32 i = 0; i < UE_ARRAY_COUNT(GResolutions); ++i) { if (GResolutions[i] == Cur) { return i; } } return 2; },
		[St](int32 I) { St->SetScreenResolution(GResolutions[I]); }));

	Add(ToggleRow(TEXT("opt.gfx.vsync"), [St]() { return St->IsVSyncEnabled(); }, [St](bool B) { St->SetVSyncEnabled(B); }));
	Add(ChoiceRow(TEXT("opt.gfx.fpslimit"), { TEXT("opt.fps.30"), TEXT("opt.fps.60"), TEXT("opt.fps.90"), TEXT("opt.fps.120"), TEXT("opt.fps.144"), TEXT("opt.fps.unlimited") },
		[St]() { const float L = St->GetFrameRateLimit(); return L <= 0.f ? 5 : NearestIndex(GFpsLimits, 5, L); },
		[St](int32 I) { St->SetFrameRateLimit(GFpsLimits[I]); }));

	Add(ChoiceRow(TEXT("opt.gfx.upscaler"), { TEXT("opt.up.native"), TEXT("opt.up.tsr"), TEXT("opt.up.fsr") },
		[St]() { return (int32)St->Upscaler; }, [St, Custom](int32 I) { St->Upscaler = (EMTAUpscaler)I; Custom(); }));
	Add(ChoiceRow(TEXT("opt.gfx.upscale_quality"), { TEXT("opt.uq.native"), TEXT("opt.uq.quality"), TEXT("opt.uq.balanced"), TEXT("opt.uq.performance") },
		[St]() { return (int32)St->UpscaleQuality; }, [St, Custom](int32 I) { St->UpscaleQuality = (EMTAUpscaleQuality)I; Custom(); }));
	if (!UMTAGameUserSettings::IsFSRAvailable())
	{
		Add(Label(TrAttr(TEXT("opt.gfx.fsr_missing")), 13.f, true));
	}
	Add(SliderRow(TEXT("opt.gfx.resscale"), 50.f, 100.f, 5.f, [St]() { return St->ResolutionScalePct; },
		[St, Custom](float V) { St->ResolutionScalePct = V; Custom(); }, [](float V) { return Fixed(V, 0); }));

	Add(ChoiceRow(TEXT("opt.gfx.shadows"), Q5, [St]() { return St->ShadowQualityLevel; }, [St, Custom](int32 I) { St->ShadowQualityLevel = I; Custom(); }));
	Add(ChoiceRow(TEXT("opt.gfx.gi"), Q5, [St]() { return St->GIQualityLevel; }, [St, Custom](int32 I) { St->GIQualityLevel = I; Custom(); }));
	Add(ChoiceRow(TEXT("opt.gfx.lumen"), Q4, [St]() { return St->LumenQualityLevel; }, [St, Custom](int32 I) { St->LumenQualityLevel = I; Custom(); }));
	Add(ChoiceRow(TEXT("opt.gfx.nanite"), Q4, [St]() { return St->NaniteQualityLevel; }, [St, Custom](int32 I) { St->NaniteQualityLevel = I; Custom(); }));
	Add(ChoiceRow(TEXT("opt.gfx.viewdistance"), Q5, [St]() { return St->ViewDistanceLevel; }, [St, Custom](int32 I) { St->ViewDistanceLevel = I; Custom(); }));
	Add(ChoiceRow(TEXT("opt.gfx.textures"), Q5, [St]() { return St->TextureQualityLevel; }, [St, Custom](int32 I) { St->TextureQualityLevel = I; Custom(); }));
	Add(ChoiceRow(TEXT("opt.gfx.effects"), Q5, [St]() { return St->EffectsQualityLevel; }, [St, Custom](int32 I) { St->EffectsQualityLevel = I; Custom(); }));
	Add(ChoiceRow(TEXT("opt.gfx.postprocess"), Q5, [St]() { return St->PostProcessQualityLevel; }, [St, Custom](int32 I) { St->PostProcessQualityLevel = I; Custom(); }));
	Add(ToggleRow(TEXT("opt.gfx.motionblur"), [St]() { return St->bMotionBlur; }, [St, Custom](bool B) { St->bMotionBlur = B; Custom(); }));
	return V;
}

TSharedRef<SWidget> SMTAOptionsMenu::BuildAudio()
{
	UMTASettingsSubsystem* Su = Sub; UMTAGameUserSettings* St = S;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	auto Add = [&V, Su, St](const TCHAR* Key, EMTAAudioBus Bus, float* Member)
	{
		(void)St;
		V->AddSlot().AutoHeight()
		[
			SliderRow(Key, 0.f, 1.f, 0.05f, [Member]() { return *Member; }, [Su, Bus](float X) { Su->SetBusVolume(Bus, X); }, [](float X) { return Percent(X); })
		];
	};
	Add(TEXT("opt.audio.master"), EMTAAudioBus::Master, &St->MasterVolume);
	Add(TEXT("opt.audio.music"), EMTAAudioBus::Music, &St->MusicVolume);
	Add(TEXT("opt.audio.radio"), EMTAAudioBus::Radio, &St->RadioVolume);
	Add(TEXT("opt.audio.sfx"), EMTAAudioBus::Sfx, &St->SfxVolume);
	Add(TEXT("opt.audio.voice"), EMTAAudioBus::Voice, &St->VoiceVolume);
	return V;
}

TSharedRef<SWidget> SMTAOptionsMenu::BuildControls()
{
	UMTAGameUserSettings* St = S; UMTASettingsSubsystem* Su = Sub;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	auto Add = [&V](TSharedRef<SWidget> W) { V->AddSlot().AutoHeight()[ W ]; };
	Add(SliderRow(TEXT("opt.ctrl.steer_sens"), 0.2f, 2.f, 0.05f, [St]() { return St->SteeringSensitivity; }, [St](float X) { St->SteeringSensitivity = X; }, [](float X) { return Fixed(X); }));
	Add(SliderRow(TEXT("opt.ctrl.steer_dead"), 0.f, 0.4f, 0.01f, [St]() { return St->SteeringDeadzone; }, [St, Su](float X) { St->SteeringDeadzone = X; Su->ApplyControllerSettings(); }, [](float X) { return Percent(X); }));
	Add(SliderRow(TEXT("opt.ctrl.trigger_dead"), 0.f, 0.4f, 0.01f, [St]() { return St->TriggerDeadzone; }, [St, Su](float X) { St->TriggerDeadzone = X; Su->ApplyControllerSettings(); }, [](float X) { return Percent(X); }));
	Add(SliderRow(TEXT("opt.ctrl.cam_sens"), 0.2f, 3.f, 0.05f, [St]() { return St->CameraSensitivity; }, [St, Su](float X) { St->CameraSensitivity = X; Su->ApplyControllerSettings(); }, [](float X) { return Fixed(X); }));
	Add(ToggleRow(TEXT("opt.ctrl.invert_y"), [St]() { return St->bInvertCameraY; }, [St, Su](bool B) { St->bInvertCameraY = B; Su->ApplyControllerSettings(); }));
	Add(ToggleRow(TEXT("opt.ctrl.vibration"), [St]() { return St->bVibration; }, [St](bool B) { St->bVibration = B; }));

	Add(SNew(SBox).Padding(0.f, 16.f, 0.f, 4.f)[ Label(TrAttr(TEXT("opt.ctrl.bindings")), 22.f, false, true) ]);
	Add(Label(TrAttr(TEXT("opt.ctrl.bindings_hint")), 13.f, true));
	Add(SAssignNew(BindingList, SVerticalBox));
	Add(SNew(SBox).Padding(0.f, 8.f)[ Button(TrAttr(TEXT("opt.ctrl.reset_bindings")), FOnClicked::CreateLambda([this]() { Sub->ResetBindingsToDefault(); StatusKey.Reset(); RebuildBindings(); return FReply::Handled(); }), 260.f) ]);
	RebuildBindings();
	return V;
}

void SMTAOptionsMenu::RebuildBindings()
{
	if (!BindingList.IsValid()) { return; }
	BindingList->ClearChildren();
	const FMTAUiPalette P = Pal();
	const TArray<FMTAKeyBindingEntry>& Entries = UMTASettingsSubsystem::GetBindingEntries();

	if (bCapturing)
	{
		BindingList->AddSlot().AutoHeight().Padding(0.f, 4.f)[ Label(TrAttr(TEXT("opt.ctrl.press_key")), 16.f, false, true) ];
	}
	else if (!StatusKey.IsEmpty())
	{
		BindingList->AddSlot().AutoHeight().Padding(0.f, 4.f)[ Label(MTATrF(StatusKey, { StatusArg }), 15.f, false, true) ];
	}

	for (int32 e = 0; e < Entries.Num(); ++e)
	{
		const FMTAKeyBindingEntry& Entry = Entries[e];
		TSharedRef<SHorizontalBox> Cells = SNew(SHorizontalBox);
		auto AddCell = [&](bool bPad, int32 Slot)
		{
			TArray<FKey> Keys;
			Sub->GetBindingKeys(Entry, bPad, Keys);
			const bool bActive = bCapturing && CaptureEntry == e && bCaptureGamepad == bPad && CaptureSlot == Slot;
			const FText Txt = bActive ? FText::FromString(TEXT("...")) : (Keys.IsValidIndex(Slot) ? Keys[Slot].GetDisplayName() : FText::FromString(TEXT("-")));
			Cells->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SButton).ButtonColorAndOpacity(FSlateColor(bActive ? P.ButtonSelected : P.Button)).HAlign(HAlign_Center)
				.OnClicked_Lambda([this, e, bPad, Slot]()
				{
					bCapturing = true; CaptureEntry = e; bCaptureGamepad = bPad; CaptureSlot = Slot; StatusKey.Reset();
					FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));
					RebuildBindings();
					return FReply::Handled();
				})
				[ SNew(STextBlock).Text(Txt).Font(Font(15.f)).ColorAndOpacity(FSlateColor(P.Text)) ]
			];
		};
		AddCell(false, 0);
		AddCell(false, 1);
		if (Entry.bGamepad) { AddCell(true, 0); }
		else { Cells->AddSlot().FillWidth(1.f)[ SNew(SSpacer) ]; }

		BindingList->AddSlot().AutoHeight()[ Row(Entry.LabelKey, Cells) ];
	}
}

void SMTAOptionsMenu::TryRebind(const FKey& Key)
{
	const TArray<FMTAKeyBindingEntry>& Entries = UMTASettingsSubsystem::GetBindingEntries();
	if (!Entries.IsValidIndex(CaptureEntry)) { bCapturing = false; return; }
	if (Key.IsGamepadKey() != bCaptureGamepad) { return; } // falsche Geraeteart: weiter warten
	const FString Displaced = Sub->Rebind(Entries[CaptureEntry], bCaptureGamepad, CaptureSlot, Key);
	bCapturing = false;
	if (!Displaced.IsEmpty()) { StatusKey = TEXT("opt.ctrl.displaced"); StatusArg = MTATr(Displaced).ToString(); }
	else { StatusKey = TEXT("opt.ctrl.rebound"); StatusArg = Key.GetDisplayName().ToString(); }
	RebuildBindings();
}

FReply SMTAOptionsMenu::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	if (bCapturing)
	{
		if (E.GetKey() == EKeys::Escape || E.GetKey() == EKeys::Gamepad_Special_Right)
		{
			bCapturing = false; RebuildBindings();
		}
		else
		{
			TryRebind(E.GetKey());
		}
		return FReply::Handled(); // Escape beendet nur die Aufnahme, nicht das Menue
	}
	return FReply::Unhandled();
}

FReply SMTAOptionsMenu::OnAnalogValueChanged(const FGeometry& G, const FAnalogInputEvent& E)
{
	// Trigger und Sticks melden nur Analogwerte: bei der Aufnahme ab 60 % Ausschlag uebernehmen.
	if (bCapturing && bCaptureGamepad && FMath::Abs(E.GetAnalogValue()) > 0.6f)
	{
		TryRebind(E.GetKey());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SMTAOptionsMenu::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (bCapturing && !bCaptureGamepad)
	{
		TryRebind(E.GetEffectingButton());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

TSharedRef<SWidget> SMTAOptionsMenu::BuildGameplay()
{
	UMTAGameUserSettings* St = S;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	auto Add = [&V](TSharedRef<SWidget> W) { V->AddSlot().AutoHeight()[ W ]; };

	// Sprache: alle gefundenen text.<code>.json
	if (UMTALocalizationSubsystem* Loc = UMTALocalizationSubsystem::GetActive())
	{
		const TArray<FString> Codes = Loc->GetAvailableLanguages();
		TArray<FString> LangKeys;
		for (const FString& C : Codes) { LangKeys.Add(TEXT("lang.") + C); }
		Add(ChoiceRow(TEXT("opt.game.language"), LangKeys,
			[Loc, Codes]() { return FMath::Max(0, Codes.IndexOfByKey(Loc->GetLanguage())); },
			[Loc, Codes](int32 I) { if (Codes.IsValidIndex(I)) { Loc->SetLanguage(Codes[I]); } }));
	}
	Add(ChoiceRow(TEXT("opt.game.difficulty"), { TEXT("difficulty.easy"), TEXT("difficulty.normal"), TEXT("difficulty.hard") },
		[St]() { return (int32)St->Difficulty; }, [St](int32 I) { St->Difficulty = (EMTADifficulty)I; }));
	Add(SliderRow(TEXT("opt.game.wanted"), 0.5f, 2.f, 0.1f, [St]() { return St->WantedStrength; }, [St](float X) { St->WantedStrength = X; }, [](float X) { return Fixed(X, 1); }));
	Add(ToggleRow(TEXT("opt.game.brake_assist"), [St]() { return St->bBrakeAssist; }, [St](bool B) { St->bBrakeAssist = B; }));
	Add(ToggleRow(TEXT("opt.game.route_guidance"), [St]() { return St->bRouteGuidance; }, [St](bool B) { St->bRouteGuidance = B; }));
	Add(ToggleRow(TEXT("opt.game.subtitles"), [St]() { return St->bSubtitles; }, [St](bool B) { St->bSubtitles = B; }));
	Add(ChoiceRow(TEXT("opt.game.subtitle_size"), { TEXT("opt.size.small"), TEXT("opt.size.medium"), TEXT("opt.size.large") },
		[St]() { return St->SubtitleSize; }, [this, St](int32 I) { St->SubtitleSize = I; NotifyChanged(); }));
	Add(ToggleRow(TEXT("opt.game.subtitle_bg"), [St]() { return St->bSubtitleBackground; }, [this, St](bool B) { St->bSubtitleBackground = B; NotifyChanged(); }));
	Add(ToggleRow(TEXT("opt.game.tutorial_hints"), [St]() { return St->bTutorialHints; }, [St](bool B) { St->bTutorialHints = B; }));
	return V;
}

TSharedRef<SWidget> SMTAOptionsMenu::BuildAccessibility()
{
	UMTAGameUserSettings* St = S;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	auto Add = [&V](TSharedRef<SWidget> W) { V->AddSlot().AutoHeight()[ W ]; };
	auto Live = [this]() { NotifyChanged(); };
	auto Rebuild = [this]() { NotifyChanged(); OnRebuild.ExecuteIfBound(); };

	Add(ChoiceRow(TEXT("opt.acc.colorblind"), { TEXT("opt.cb.off"), TEXT("opt.cb.protanopia"), TEXT("opt.cb.deuteranopia"), TEXT("opt.cb.tritanopia") },
		[St]() { return (int32)St->ColorBlindMode; }, [St, Rebuild](int32 I) { St->ColorBlindMode = (EMTAColorBlindMode)I; Rebuild(); }));
	Add(SliderRow(TEXT("opt.acc.cb_strength"), 0.f, 1.f, 0.1f, [St]() { return St->ColorBlindStrength; }, [St, Live](float X) { St->ColorBlindStrength = X; Live(); }, [](float X) { return Percent(X); }));
	Add(ToggleRow(TEXT("opt.acc.large_text"), [St]() { return St->bLargeText; }, [St, Rebuild](bool B) { St->bLargeText = B; Rebuild(); }));
	Add(ChoiceRow(TEXT("opt.acc.ui_scale"), { TEXT("opt.scale.75"), TEXT("opt.scale.100"), TEXT("opt.scale.125"), TEXT("opt.scale.150") },
		[St]() { return NearestIndex(GUiScales, 4, St->UiScale); }, [St, Rebuild](int32 I) { St->UiScale = GUiScales[I]; Rebuild(); }));
	Add(ToggleRow(TEXT("opt.acc.high_contrast"), [St]() { return St->bHighContrast; }, [St, Rebuild](bool B) { St->bHighContrast = B; Rebuild(); }));
	Add(ToggleRow(TEXT("opt.acc.steer_assist"), [St]() { return St->bSteeringAssist; }, [St, Live](bool B) { St->bSteeringAssist = B; Live(); }));
	Add(SliderRow(TEXT("opt.acc.steer_assist_strength"), 0.f, 1.f, 0.1f, [St]() { return St->SteeringAssistStrength; }, [St, Live](float X) { St->SteeringAssistStrength = X; Live(); }, [](float X) { return Percent(X); }));
	Add(ToggleRow(TEXT("opt.acc.one_button"), [St]() { return St->bOneButtonMode; }, [St, Live](bool B) { St->bOneButtonMode = B; Live(); }));
	Add(ToggleRow(TEXT("opt.acc.reduce_camera"), [St]() { return St->bReduceCameraMotion; }, [St, Live](bool B) { St->bReduceCameraMotion = B; Live(); }));
	Add(ToggleRow(TEXT("opt.acc.reduce_flashes"), [St]() { return St->bReduceFlashes; }, [St, Live](bool B) { St->bReduceFlashes = B; Live(); }));
	Add(ToggleRow(TEXT("opt.acc.flash_warning"), [St]() { return St->bShowFlashWarning; }, [St](bool B) { St->bShowFlashWarning = B; }));
	Add(Label(TrAttr(TEXT("opt.acc.hint")), 13.f, true));
	return V;
}
