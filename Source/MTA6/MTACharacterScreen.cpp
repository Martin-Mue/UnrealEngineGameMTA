#include "MTACharacterScreen.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#include "MTAAccessibilitySubsystem.h"
#include "MTACharacterOriginSubsystem.h"
#include "MTADialogueSubsystem.h"
#include "MTAEconomySubsystem.h"
#include "MTAGarageSubsystem.h"
#include "MTAHeatSubsystem.h"
#include "MTAInventorySubsystem.h"
#include "MTAMenuController.h"
#include "MTAMenuStyle.h"
#include "MTAPhotoMode.h"
#include "MTAProgressionSubsystem.h"
#include "MTAStorySubsystem.h"
#include "MTAVehicleCatalog.h"

using namespace MTAUI;

namespace MTACharUI
{
	// Schriftgroessen in Punkt bei 1080p und UI-Skalierung 1.0 (Mindestgroesse 22, siehe Nutzerwunsch).
	constexpr float FBody = 22.f;
	constexpr float FHead = 30.f;
	constexpr float FTitle = 44.f;

	const TCHAR* const TabNames[] = { TEXT("Fähigkeiten"), TEXT("Garage"), TEXT("Inventar"), TEXT("Konto"), TEXT("Fraktionen"), TEXT("Herkunft") };

	/** "$1.234" (Punkt als Tausendertrenner). */
	FString Money(int64 Value)
	{
		const bool bNegative = Value < 0;
		const uint64 Magnitude = bNegative ? (uint64)(-Value) : (uint64)Value;
		const FString Digits = FString::Printf(TEXT("%llu"), Magnitude);
		FString Out;
		int32 Count = 0;
		for (int32 i = Digits.Len() - 1; i >= 0; --i)
		{
			Out.InsertAt(0, Digits[i]);
			if (++Count % 3 == 0 && i > 0)
			{
				Out.InsertAt(0, TEXT('.'));
			}
		}
		return FString(bNegative ? TEXT("-$") : TEXT("$")) + Out;
	}

	TSharedRef<SWidget> Txt(const FString& Text, const FLinearColor& Color, float Size = FBody, bool bBold = false, bool bWrap = true)
	{
		return SNew(STextBlock)
			.Text(FText::FromString(Text))
			.Font(Font(Size, bBold))
			.ColorAndOpacity(FSlateColor(Color))
			.AutoWrapText(bWrap);
	}

	TSharedRef<SWidget> Btn(const FString& Text, TFunction<void()> OnClick, bool bAccent = false, bool bEnabled = true, float MinWidth = 0.f)
	{
		const FMTAUiPalette Pal_ = Pal();
		return SNew(SBox).MinDesiredWidth(MinWidth)
		[
			SNew(SButton)
			.IsEnabled(bEnabled)
			.ButtonColorAndOpacity(FSlateColor(bAccent ? Pal_.ButtonSelected : Pal_.Button))
			.ContentPadding(FMargin(18.f, 10.f))
			.HAlign(HAlign_Center)
			.OnClicked_Lambda([OnClick]()
			{
				OnClick();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(Text))
				.Font(Font(FBody, bAccent))
				.ColorAndOpacity(FSlateColor(bEnabled ? Pal_.Text : Pal_.TextDim))
			]
		];
	}

	/** Waagerechter Balken (Anteil 0..1). */
	TSharedRef<SWidget> Bar(float Fraction, const FLinearColor& Fill, const FLinearColor& Back)
	{
		const float F = FMath::Clamp(Fraction, 0.f, 1.f);
		return SNew(SBox).HeightOverride(14.f)
		[
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FSlateColor(Back)).Padding(0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(FMath::Max(F, 0.0001f))
				[
					SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FSlateColor(Fill))
				]
				+ SHorizontalBox::Slot().FillWidth(FMath::Max(1.f - F, 0.0001f))
				[
					SNew(SSpacer)
				]
			]
		];
	}

	/** Zeile aus farbigen Bausteinen, die bei Platzmangel umbricht (Voraussetzungen, Eigenschaften). */
	TSharedRef<SWidget> Chips(const TArray<TPair<FString, FLinearColor>>& Items)
	{
		TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).UseAllottedSize(true);
		for (const TPair<FString, FLinearColor>& Item : Items)
		{
			Wrap->AddSlot().Padding(FMargin(0.f, 0.f, 18.f, 0.f))
			[
				Txt(Item.Key, Item.Value, FBody, false, false)
			];
		}
		return Wrap;
	}

	FString ModifierText(const FMTAStatModifier& Mod)
	{
		FString Name = Mod.Stat.ToString();
		int32 DotIndex = INDEX_NONE;
		if (Name.FindLastChar(TEXT('.'), DotIndex))
		{
			Name = Name.Mid(DotIndex + 1);
		}
		FString Value;
		if (!FMath::IsNearlyEqual(Mod.Mul, 1.f, 0.0001f))
		{
			Value = FString::Printf(TEXT("%+.0f%%"), (Mod.Mul - 1.f) * 100.f);
		}
		if (!FMath::IsNearlyZero(Mod.Add, 0.0001f))
		{
			Value += (Value.IsEmpty() ? FString() : FString(TEXT(" "))) + FString::Printf(TEXT("%+g"), Mod.Add);
		}
		return Name + TEXT(" ") + Value;
	}

	FString EffectsText(const TArray<FMTAStatModifier>& Effects)
	{
		TArray<FString> Parts;
		for (const FMTAStatModifier& Effect : Effects)
		{
			Parts.Add(ModifierText(Effect));
		}
		return FString::Join(Parts, TEXT(", "));
	}

	FString TuningCategoryName(FName Category)
	{
		const FString Id = Category.ToString();
		if (Id == TEXT("engine")) { return TEXT("Motor"); }
		if (Id == TEXT("transmission")) { return TEXT("Getriebe"); }
		if (Id == TEXT("tires")) { return TEXT("Reifen"); }
		if (Id == TEXT("brakes")) { return TEXT("Bremsen"); }
		if (Id == TEXT("suspension")) { return TEXT("Fahrwerk"); }
		if (Id == TEXT("armor")) { return TEXT("Panzerung"); }
		if (Id == TEXT("paint")) { return TEXT("Lack"); }
		if (Id == TEXT("underglow")) { return TEXT("Unterbodenlicht"); }
		if (Id == TEXT("module")) { return TEXT("Module"); }
		return Id;
	}

	FString TransactionCategoryName(EMTATransactionCategory Category)
	{
		static const TCHAR* const Names[] =
		{
			TEXT("Sonstiges"), TEXT("Mission"), TEXT("Gig"), TEXT("Aktivität"), TEXT("Verkauf"), TEXT("Einkauf"), TEXT("Fahrzeug"),
			TEXT("Tuning"), TEXT("Reparatur"), TEXT("Treibstoff"), TEXT("Bußgeld"), TEXT("Bestechung"), TEXT("Kredit"), TEXT("Zinsen"),
			TEXT("Versicherung"), TEXT("Garage"), TEXT("Schwarzmarkt"), TEXT("Kennzeichen"), TEXT("Abschleppen"), TEXT("Herkunft"),
			TEXT("Belohnung"), TEXT("Beschlagnahmung")
		};
		const int32 Index = (int32)Category;
		return (Index >= 0 && Index < UE_ARRAY_COUNT(Names)) ? FString(Names[Index]) : FString(TEXT("Sonstiges"));
	}

	FString TierName(FName Tier)
	{
		const FString Id = Tier.ToString();
		if (Id == TEXT("enemy")) { return TEXT("Feind"); }
		if (Id == TEXT("suspicious")) { return TEXT("Misstrauisch"); }
		if (Id == TEXT("neutral")) { return TEXT("Neutral"); }
		if (Id == TEXT("allied")) { return TEXT("Verbündet"); }
		if (Id == TEXT("trusted")) { return TEXT("Vertraut"); }
		return Id;
	}

	FString VehicleName(const UMTAVehicleCatalog* Catalog, const FMTAOwnedVehicle& Vehicle)
	{
		if (Catalog)
		{
			if (const FMTAVehicleModel* Model = Catalog->FindModel(Vehicle.ModelId))
			{
				return Model->BrandName + TEXT(" ") + Model->ModelName;
			}
		}
		return Vehicle.ModelId.ToString();
	}
}

using namespace MTACharUI;

// ================================================================================ Widget

void SMTACharScreen::Construct(const FArguments& InArgs)
{
	GI = InArgs._GameInstance;
	OnCloseDelegate = InArgs._OnClose;
	Tab = FMath::Clamp(InArgs._InitialTab, 0, (int32)EMTACharTab::Count - 1);
	const FMTAUiPalette Colors = Pal();

	TSharedRef<SWrapBox> Tabs = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(12.0, 12.0));
	for (int32 i = 0; i < (int32)EMTACharTab::Count; ++i)
	{
		Tabs->AddSlot()
		[
			SNew(SButton)
			.ButtonColorAndOpacity_Lambda([this, i, Colors]() { return FSlateColor(Tab == i ? Colors.ButtonSelected : Colors.Button); })
			.ContentPadding(FMargin(22.f, 10.f))
			.OnClicked_Lambda([this, i]()
			{
				SetTab(i);
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(FText::FromString(TabNames[i])).Font(Font(FBody + 2.f, true)).ColorAndOpacity(FSlateColor(Colors.Text))
			]
		];
	}

	ChildSlot
	[
		SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FSlateColor(Colors.Background)).Padding(FMargin(48.f, 32.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Txt(TEXT("CHARAKTER"), Colors.Text, FTitle, true, false)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(32.f, 0.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(Font(FBody, false))
					.ColorAndOpacity(FSlateColor(Colors.TextDim))
					.AutoWrapText(true)
					.Text_Lambda([this]() -> FText
					{
						FString Line;
						if (UGameInstance* G = GI.Get())
						{
							if (const UMTACharacterOriginSubsystem* Origin = G->GetSubsystem<UMTACharacterOriginSubsystem>())
							{
								Line = Origin->GetCharacterName();
							}
							if (const UMTAProgressionSubsystem* Prog = G->GetSubsystem<UMTAProgressionSubsystem>())
							{
								Line += FString::Printf(TEXT("%sStufe %d   Perk-Punkte %d"), Line.IsEmpty() ? TEXT("") : TEXT("   |   "), Prog->GetLevel(), Prog->GetPerkPoints());
							}
						}
						return FText::FromString(Line);
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 14.f)
			[
				Tabs
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				Panel(SAssignNew(ContentBox, SBox), true, 20.f)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Font(Font(FBody, true))
				.AutoWrapText(true)
				.ColorAndOpacity_Lambda([this, Colors]() { return FSlateColor(bStatusGood ? Colors.Good : Colors.Warning); })
				.Text_Lambda([this]() { return FText::FromString(StatusText); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				Txt(TEXT("Q/E oder LB/RB: Reiter wechseln   |   Pfeile/Steuerkreuz: auswählen   |   Enter/A: bestätigen   |   Esc/B/I: schließen"),
					Colors.TextDim, FBody, false, true)
			]
		]
	];

	RebuildContent(false);
}

void SMTACharScreen::SetStatus(const FString& Text, bool bGood)
{
	StatusText = Text;
	bStatusGood = bGood;
}

void SMTACharScreen::SetTab(int32 NewTab)
{
	NewTab = FMath::Clamp(NewTab, 0, (int32)EMTACharTab::Count - 1);
	if (NewTab == Tab)
	{
		return;
	}
	Tab = NewTab;
	KeepScrollOffset = 0.f;
	PendingRespecTree = NAME_None;
	StatusText.Reset();
	RequestRefresh();
}

void SMTACharScreen::StepTab(int32 Delta)
{
	const int32 Count = (int32)EMTACharTab::Count;
	SetTab((Tab + Delta + Count) % Count);
}

void SMTACharScreen::RequestRefresh()
{
	if (bRefreshQueued)
	{
		return;
	}
	bRefreshQueued = true;
	if (ContentScroll.IsValid())
	{
		KeepScrollOffset = ContentScroll->GetScrollOffset();
	}
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		bRefreshQueued = false;
		RebuildContent();
		return EActiveTimerReturnType::Stop;
	}));
}

namespace MTACharUI
{
	/** Erste aktive Schaltflaeche im Baum (fuer den Starttfokus nach einem Neuaufbau). */
	TSharedPtr<SWidget> FindFirstButton(const TSharedRef<SWidget>& Root)
	{
		if (Root->GetType() == FName(TEXT("SButton")) && Root->IsEnabled())
		{
			return Root;
		}
		if (FChildren* Children = Root->GetChildren())
		{
			for (int32 i = 0; i < Children->Num(); ++i)
			{
				if (TSharedPtr<SWidget> Found = FindFirstButton(Children->GetChildAt(i)))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}
}

void SMTACharScreen::RebuildContent(bool bRestoreFocus)
{
	if (!ContentBox.IsValid())
	{
		return;
	}
	ContentScroll.Reset();
	TSharedRef<SWidget> Inner = SNullWidget::NullWidget;
	switch ((EMTACharTab)Tab)
	{
	case EMTACharTab::Skills: Inner = BuildSkills(); break;
	case EMTACharTab::Garage: Inner = BuildGarage(); break;
	case EMTACharTab::Inventory: Inner = BuildInventory(); break;
	case EMTACharTab::Account: Inner = BuildAccount(); break;
	case EMTACharTab::Factions: Inner = BuildFactions(); break;
	case EMTACharTab::Origin: Inner = BuildOrigin(); break;
	default: break;
	}
	ContentBox->SetContent(Inner);
	if (ContentScroll.IsValid())
	{
		ContentScroll->SetScrollOffset(KeepScrollOffset);
	}
	if (bRestoreFocus && FSlateApplication::IsInitialized())
	{
		// Fokus zurueck: erste Schaltflaeche des Inhalts, sonst der Bildschirm selbst. So arbeiten
		// Tasten und Steuerkreuz nach einem Neuaufbau weiter, und Q/E/Esc kommen immer an.
		TSharedPtr<SWidget> FocusTarget = FindFirstButton(Inner);
		if (!FocusTarget.IsValid())
		{
			FocusTarget = AsShared();
		}
		FSlateApplication::Get().SetAllUserFocus(FocusTarget, EFocusCause::SetDirectly);
	}
}

FReply SMTACharScreen::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& E)
{
	const FKey Key = E.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::I || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right)
	{
		OnCloseDelegate.ExecuteIfBound();
		return FReply::Handled();
	}
	if (Key == EKeys::Q || Key == EKeys::PageUp || Key == EKeys::Gamepad_LeftShoulder)
	{
		StepTab(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::E || Key == EKeys::PageDown || Key == EKeys::Gamepad_RightShoulder)
	{
		StepTab(1);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SMTACharScreen::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& E)
{
	// Rechter Stick scrollt die Liste (Textzeilen sind nicht fokussierbar).
	if (E.GetKey() == EKeys::Gamepad_RightY && ContentScroll.IsValid() && FMath::Abs(E.GetAnalogValue()) > 0.25f)
	{
		const float Target = ContentScroll->GetScrollOffset() - E.GetAnalogValue() * 60.f;
		ContentScroll->SetScrollOffset(FMath::Clamp(Target, 0.f, ContentScroll->GetScrollOffsetOfEnd()));
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FName SMTACharScreen::CurrentDistrict() const
{
	if (UGameInstance* G = GI.Get())
	{
		if (const UWorld* World = G->GetWorld())
		{
			if (const UMTAHeatSubsystem* Heat = World->GetSubsystem<UMTAHeatSubsystem>())
			{
				const FName District = Heat->GetCurrentDistrict(0);
				if (!District.IsNone())
				{
					return District;
				}
			}
		}
	}
	return TEXT("core");
}

TSharedRef<SWidget> SMTACharScreen::BuildMissing(const FString& SystemName) const
{
	const FMTAUiPalette Colors = Pal();
	return Txt(FString::Printf(TEXT("%s ist nicht verfügbar."), *SystemName), Colors.Warning, FHead, true);
}

// ------------------------------------------------------------------------------ Faehigkeiten

TSharedRef<SWidget> SMTACharScreen::BuildSkills()
{
	UGameInstance* G = GI.Get();
	UMTAProgressionSubsystem* Prog = G ? G->GetSubsystem<UMTAProgressionSubsystem>() : nullptr;
	if (!Prog)
	{
		return BuildMissing(TEXT("Das Fortschrittssystem"));
	}
	const FMTAUiPalette Colors = Pal();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);

	const int32 Level = Prog->GetLevel();
	List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
	[
		Txt(FString::Printf(TEXT("Stufe %d     Perk-Punkte: %d     Erfahrung: %lld / %lld"),
			Level, Prog->GetPerkPoints(), Prog->GetXp(), Prog->GetXpForLevel(Level + 1)), Colors.Accent, FHead, true)
	];
	List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
	[
		Bar(Prog->GetLevelProgress(), Colors.Accent, Colors.Button)
	];

	for (const FMTASkillTreeDef& Tree : Prog->GetSkillTrees())
	{
		const FName TreeId = Tree.Id;
		const bool bConfirm = (PendingRespecTree == TreeId);
		List->AddSlot().AutoHeight().Padding(0.f, 18.f, 0.f, 4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				Txt(FString::Printf(TEXT("%s     Meisterschaft %d"), *Tree.Name, Prog->GetTreeMastery(TreeId)), Colors.Text, FHead, true)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Btn(bConfirm ? TEXT("Wirklich zurücksetzen? (kostet Geld)") : TEXT("Baum zurücksetzen"),
					[this, Prog, TreeId, bConfirm]()
					{
						if (!bConfirm)
						{
							PendingRespecTree = TreeId;
							SetStatus(TEXT("Zum Bestätigen noch einmal klicken. Die Punkte werden erstattet, das Zurücksetzen kostet Geld."), false);
						}
						else
						{
							PendingRespecTree = NAME_None;
							if (Prog->RespecTree(TreeId))
							{
								SetStatus(TEXT("Baum zurückgesetzt, Perk-Punkte erstattet."), true);
							}
							else
							{
								SetStatus(TEXT("Zurücksetzen nicht möglich (nichts gelernt oder zu wenig Geld)."), false);
							}
						}
						RequestRefresh();
					}, bConfirm)
			]
		];
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			Txt(Tree.Description, Colors.TextDim)
		];

		for (const FName& SkillId : Tree.SkillIds)
		{
			FMTASkillDef Def;
			if (!Prog->GetSkill(SkillId, Def))
			{
				continue;
			}
			const int32 Rank = Prog->GetSkillRank(SkillId);
			const bool bMax = Rank >= Def.MaxRank;
			FString Reason;
			const bool bCan = Prog->CanUnlockSkill(SkillId, Reason);
			int32 Cost = 0;
			if (!bMax && Def.CostPerRank.Num() > 0)
			{
				Cost = Def.CostPerRank[FMath::Clamp(Rank, 0, Def.CostPerRank.Num() - 1)];
			}

			// Voraussetzungen mit Erfuellt-Farbe
			TArray<TPair<FString, FLinearColor>> Needs;
			for (int32 r = 0; r < Def.Requires.Num(); ++r)
			{
				const int32 NeededRank = Def.RequiredRanks.IsValidIndex(r) ? Def.RequiredRanks[r] : 1;
				FMTASkillDef ReqDef;
				const FString ReqName = Prog->GetSkill(Def.Requires[r], ReqDef) ? ReqDef.Name : Def.Requires[r].ToString();
				const bool bOk = Prog->GetSkillRank(Def.Requires[r]) >= NeededRank;
				Needs.Add(TPair<FString, FLinearColor>(FString::Printf(TEXT("%s Rang %d"), *ReqName, NeededRank), bOk ? Colors.Good : Colors.Warning));
			}
			if (Def.RequiredLevel > 1)
			{
				Needs.Add(TPair<FString, FLinearColor>(FString::Printf(TEXT("Stufe %d"), Def.RequiredLevel), Level >= Def.RequiredLevel ? Colors.Good : Colors.Warning));
			}
			if (Def.RequiredMastery > 0)
			{
				Needs.Add(TPair<FString, FLinearColor>(FString::Printf(TEXT("Meisterschaft %d"), Def.RequiredMastery),
					Prog->GetTreeMastery(Def.TreeId) >= Def.RequiredMastery ? Colors.Good : Colors.Warning));
			}

			TSharedRef<SVerticalBox> Info = SNew(SVerticalBox);
			Info->AddSlot().AutoHeight()
			[
				Txt(FString::Printf(TEXT("%s     Rang %d / %d"), *Def.Name, Rank, Def.MaxRank), bMax ? Colors.Good : Colors.Text, FBody + 2.f, true)
			];
			Info->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)[ Txt(Def.Description, Colors.TextDim) ];
			if (Needs.Num() > 0)
			{
				Info->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 14.f, 0.f)[ Txt(TEXT("Benötigt:"), Colors.TextDim, FBody, false, false) ]
					+ SHorizontalBox::Slot().FillWidth(1.f)[ Chips(Needs) ]
				];
			}
			if (!bMax && !bCan && !Reason.IsEmpty())
			{
				Info->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)[ Txt(Reason, Colors.Warning) ];
			}

			TSharedRef<SWidget> Action = bMax
				? Txt(TEXT("Maximal"), Colors.Good, FBody, true, false)
				: Btn(FString::Printf(TEXT("Lernen (%d P)"), Cost),
					[this, Prog, SkillId, Reason, Name = Def.Name]()
					{
						if (Prog->UnlockSkill(SkillId, false))
						{
							SetStatus(FString::Printf(TEXT("%s gelernt."), *Name), true);
						}
						else
						{
							SetStatus(Reason.IsEmpty() ? FString(TEXT("Fähigkeit kann nicht gelernt werden.")) : Reason, false);
						}
						RequestRefresh();
					}, true, bCan, 200.f);

			const float Indent = FMath::Max(Def.Tier - 1, 0) * 28.f;
			List->AddSlot().AutoHeight().Padding(Indent, 0.f, 0.f, 8.f)
			[
				Panel(
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[ Info ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(18.f, 0.f, 0.f, 0.f)[ Action ],
					false, 14.f)
			];
		}
	}

	return SAssignNew(ContentScroll, SScrollBox) + SScrollBox::Slot().Padding(0.f, 0.f, 16.f, 0.f)[ List ];
}

// ------------------------------------------------------------------------------ Garage

TSharedRef<SWidget> SMTACharScreen::BuildGarage()
{
	UGameInstance* G = GI.Get();
	UMTAGarageSubsystem* Garage = G ? G->GetSubsystem<UMTAGarageSubsystem>() : nullptr;
	UMTAVehicleCatalog* Catalog = G ? G->GetSubsystem<UMTAVehicleCatalog>() : nullptr;
	if (!Garage)
	{
		return BuildMissing(TEXT("Die Garage"));
	}
	const FMTAUiPalette Colors = Pal();
	const TArray<FMTAOwnedVehicle> Vehicles = Garage->GetVehicles();
	if (Vehicles.Num() == 0)
	{
		return Txt(TEXT("Du besitzt noch kein Fahrzeug. Kaufe eines beim Händler."), Colors.TextDim, FHead);
	}

	// Auswahl gueltig halten
	int32 SelectedIndex = Vehicles.IndexOfByPredicate([this](const FMTAOwnedVehicle& V) { return V.InstanceId == SelectedVehicle; });
	if (SelectedIndex == INDEX_NONE)
	{
		const FName Active = Garage->GetActiveVehicleId();
		SelectedIndex = Vehicles.IndexOfByPredicate([Active](const FMTAOwnedVehicle& V) { return V.InstanceId == Active; });
		if (SelectedIndex == INDEX_NONE) { SelectedIndex = 0; }
		SelectedVehicle = Vehicles[SelectedIndex].InstanceId;
	}
	const FMTAOwnedVehicle Veh = Vehicles[SelectedIndex];
	const FName VehId = Veh.InstanceId;
	const FName District = Veh.District.IsNone() ? CurrentDistrict() : Veh.District;
	const bool bActive = (Garage->GetActiveVehicleId() == VehId);

	// Linke Spalte: Fahrzeugliste
	TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
	Left->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[ Txt(TEXT("Deine Fahrzeuge"), Colors.Accent, FHead, true) ];
	for (const FMTAOwnedVehicle& Owned : Vehicles)
	{
		const FName Id = Owned.InstanceId;
		FString Label = VehicleName(Catalog, Owned);
		if (Garage->GetActiveVehicleId() == Id) { Label += TEXT("  [aktiv]"); }
		Left->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			Btn(Label, [this, Id]() { SelectedVehicle = Id; StatusText.Reset(); RequestRefresh(); }, Id == VehId)
		];
	}

	// Rechte Spalte: Details
	TSharedRef<SVerticalBox> Right = SNew(SVerticalBox);
	Right->AddSlot().AutoHeight()[ Txt(VehicleName(Catalog, Veh), Colors.Text, FTitle - 6.f, true) ];

	const int64 Value = Garage->GetVehicleValue(VehId);
	FString Location = Veh.bImpounded ? FString::Printf(TEXT("in der Verwahrstelle (%d Tage)"), Veh.ImpoundDays)
		: (Veh.bInGarage ? FString::Printf(TEXT("in der Garage (%s)"), *Veh.District.ToString()) : FString(TEXT("auf der Straße")));
	Right->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
	[
		Txt(FString::Printf(TEXT("Kennzeichen %s     Schaden %d %%     Tank %d %%     Wert %s"),
			*Veh.Plate, FMath::RoundToInt(Veh.Damage * 100.f), FMath::RoundToInt(Veh.Fuel * 100.f), *Money(Value)), Colors.Text)
	];
	Right->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
	[
		Txt(FString::Printf(TEXT("Standort: %s     Kilometer %.0f     Versicherung: %s"), *Location, Veh.OdometerKm,
			Veh.InsuranceTier < 0 ? TEXT("keine") : (Veh.InsuranceTier == 0 ? TEXT("Basis") : (Veh.InsuranceTier == 1 ? TEXT("Komfort") : TEXT("Voll")))),
			Veh.bImpounded ? Colors.Warning : Colors.TextDim)
	];
	if (Veh.bStolen || Veh.bHot)
	{
		Right->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			Txt(Veh.bHot ? TEXT("Achtung: Dieses Fahrzeug ist heiß (Kennzeichen bei Aegis bekannt).") : TEXT("Dieses Fahrzeug ist gestohlen."), Colors.Warning, FBody, true)
		];
	}

	const FMTAVehicleStats Stats = Garage->ComputeStats(VehId);
	Right->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
	[
		Txt(FString::Printf(TEXT("Höchstgeschwindigkeit %.0f km/h     0-100 in %.1f s     Leistungsindex %d     Haftung %.2f     Haltbarkeit %.0f     Kofferraum %d Plätze, %.0f kg"),
			Stats.TopSpeedKmh, Stats.Accel0To100s, Stats.PerformanceIndex, Stats.Grip, Stats.Durability, Stats.CargoSlots, Stats.CargoKg), Colors.Accent)
	];

	// Aktionen
	const int64 RepairCost = Garage->GetRepairCost(VehId, District);
	const bool bNeedsRepair = Veh.Damage > 0.01f && !Veh.bImpounded;
	Right->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
	[
		SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(12.0, 12.0))
		+ SWrapBox::Slot()
		[
			Btn(bActive ? TEXT("Aktives Fahrzeug") : TEXT("Als aktives Fahrzeug setzen"),
				[this, Garage, VehId]()
				{
					SetStatus(Garage->SetActiveVehicle(VehId) ? TEXT("Fahrzeug ist jetzt aktiv.") : TEXT("Fahrzeug kann nicht aktiviert werden."), true);
					RequestRefresh();
				}, true, !bActive && !Veh.bImpounded)
		]
		+ SWrapBox::Slot()
		[
			Btn(bNeedsRepair ? FString::Printf(TEXT("Reparieren (%s)"), *Money(RepairCost)) : FString(TEXT("Kein Schaden")),
				[this, Garage, VehId, District]()
				{
					if (Garage->RepairVehicle(VehId, District)) { SetStatus(TEXT("Fahrzeug repariert."), true); }
					else { SetStatus(TEXT("Reparatur nicht möglich (zu wenig Geld?)."), false); }
					RequestRefresh();
				}, false, bNeedsRepair)
		]
	];

	// Tuning
	Right->AddSlot().AutoHeight().Padding(0.f, 22.f, 0.f, 6.f)[ Txt(TEXT("Tuning"), Colors.Accent, FHead, true) ];
	TArray<FName> Categories;
	if (Catalog)
	{
		if (const FMTAVehicleModel* Model = Catalog->FindModel(Veh.ModelId))
		{
			Categories = Model->TuningSlots;
		}
	}
	if (Categories.Num() == 0)
	{
		Right->AddSlot().AutoHeight()[ Txt(TEXT("Für dieses Modell gibt es keine Tuning-Teile."), Colors.TextDim) ];
	}
	for (const FName& Category : Categories)
	{
		const TArray<FMTATuningPart> Parts = Catalog->GetTuningParts(Category);
		if (Parts.Num() == 0)
		{
			continue;
		}
		Right->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)[ Txt(TuningCategoryName(Category), Colors.Text, FBody + 4.f, true) ];
		for (const FMTATuningPart& Part : Parts)
		{
			const FName PartId = Part.Id;
			const bool bInstalled = Veh.InstalledParts.Contains(PartId);
			FString WhyNot;
			const bool bCan = !bInstalled && Garage->CanInstallPart(VehId, PartId, WhyNot);

			TSharedRef<SVerticalBox> PartInfo = SNew(SVerticalBox);
			PartInfo->AddSlot().AutoHeight()
			[
				Txt(FString::Printf(TEXT("%s     Stufe %d     %s%s"), *Part.Name, Part.Level, *Money(Part.Price), bInstalled ? TEXT("     eingebaut") : TEXT("")),
					bInstalled ? Colors.Good : Colors.Text, FBody, bInstalled)
			];
			if (Part.Effects.Num() > 0)
			{
				PartInfo->AddSlot().AutoHeight()[ Txt(EffectsText(Part.Effects), Colors.TextDim) ];
			}
			if (Part.IllegalIn.Num() > 0)
			{
				TArray<FString> Names;
				for (const FName& Illegal : Part.IllegalIn) { Names.Add(Illegal.ToString()); }
				PartInfo->AddSlot().AutoHeight()[ Txt(TEXT("Illegal in: ") + FString::Join(Names, TEXT(", ")), Colors.Warning) ];
			}
			if (!bInstalled && !bCan && !WhyNot.IsEmpty())
			{
				PartInfo->AddSlot().AutoHeight()[ Txt(WhyNot, Colors.TextDim) ];
			}

			TSharedRef<SWidget> PartAction = bInstalled
				? Btn(TEXT("Ausbauen"), [this, Garage, VehId, Category]()
					{
						SetStatus(Garage->RemovePart(VehId, Category) ? TEXT("Teil ausgebaut.") : TEXT("Teil kann nicht ausgebaut werden."), true);
						RequestRefresh();
					}, false, true, 200.f)
				: Btn(TEXT("Kaufen und einbauen"), [this, Garage, VehId, PartId, District, PartName = Part.Name]()
					{
						FString Why;
						if (Garage->InstallPart(VehId, PartId, District, Why)) { SetStatus(FString::Printf(TEXT("%s eingebaut."), *PartName), true); }
						else { SetStatus(Why.IsEmpty() ? FString(TEXT("Einbau nicht möglich.")) : Why, false); }
						RequestRefresh();
					}, true, bCan, 200.f);

			Right->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				Panel(
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[ PartInfo ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(18.f, 0.f, 0.f, 0.f)[ PartAction ],
					false, 12.f)
			];
		}
	}

	TSharedRef<SWidget> Body =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.32f).Padding(0.f, 0.f, 24.f, 0.f)[ Left ]
		+ SHorizontalBox::Slot().FillWidth(0.68f)[ Right ];
	return SAssignNew(ContentScroll, SScrollBox) + SScrollBox::Slot().Padding(0.f, 0.f, 16.f, 0.f)[ Body ];
}

// ------------------------------------------------------------------------------ Inventar

TSharedRef<SWidget> SMTACharScreen::BuildInventory()
{
	UGameInstance* G = GI.Get();
	UMTAInventorySubsystem* Inv = G ? G->GetSubsystem<UMTAInventorySubsystem>() : nullptr;
	if (!Inv)
	{
		return BuildMissing(TEXT("Das Inventar"));
	}
	const FMTAUiPalette Colors = Pal();
	const FName District = CurrentDistrict();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);

	const EMTAInventoryContainer Containers[] = { EMTAInventoryContainer::Personal, EMTAInventoryContainer::Trunk };
	for (const EMTAInventoryContainer Container : Containers)
	{
		const bool bPersonal = (Container == EMTAInventoryContainer::Personal);
		const EMTAInventoryContainer Other = bPersonal ? EMTAInventoryContainer::Trunk : EMTAInventoryContainer::Personal;
		const int32 MaxSlots = Inv->GetMaxSlots(Container);
		const float Weight = Inv->GetWeight(Container);
		const float MaxWeight = Inv->GetMaxWeight(Container);

		List->AddSlot().AutoHeight().Padding(0.f, bPersonal ? 0.f : 22.f, 0.f, 4.f)
		[
			Txt(bPersonal ? TEXT("Tasche") : TEXT("Kofferraum des aktiven Fahrzeugs"), Colors.Accent, FHead, true)
		];
		if (MaxSlots <= 0)
		{
			List->AddSlot().AutoHeight()[ Txt(TEXT("Kein Kofferraum verfügbar (kein aktives Fahrzeug)."), Colors.TextDim) ];
			continue;
		}
		const bool bOverweight = MaxWeight > 0.f && Weight > MaxWeight;
		List->AddSlot().AutoHeight()
		[
			Txt(FString::Printf(TEXT("Plätze %d / %d     Gewicht %.1f / %.1f kg"), Inv->GetUsedSlots(Container), MaxSlots, Weight, MaxWeight),
				bOverweight ? Colors.Warning : Colors.Text)
		];
		List->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 10.f)
		[
			Bar(MaxWeight > 0.f ? Weight / MaxWeight : 0.f, bOverweight ? Colors.Warning : Colors.Good, Colors.Button)
		];

		const TArray<FMTAItemStack> Stacks = Inv->GetItems(Container);
		if (Stacks.Num() == 0)
		{
			List->AddSlot().AutoHeight()[ Txt(TEXT("Leer."), Colors.TextDim) ];
		}
		for (const FMTAItemStack& Stack : Stacks)
		{
			FMTAItemDef Def;
			const bool bHasDef = Inv->GetItemDef(Stack.ItemId, Def);
			const FString Name = bHasDef ? Def.Name : Stack.ItemId.ToString();
			const float StackWeight = bHasDef ? Def.WeightKg * Stack.Count : 0.f;
			const FName ItemId = Stack.ItemId;
			const int32 Count = Stack.Count;

			TSharedRef<SVerticalBox> Info = SNew(SVerticalBox);
			Info->AddSlot().AutoHeight()
			[
				Txt(FString::Printf(TEXT("%dx %s%s"), Count, *Name, Stack.Charges > 0 ? *FString::Printf(TEXT("     Ladungen %d"), Stack.Charges) : TEXT("")), Colors.Text, FBody, true)
			];
			TArray<TPair<FString, FLinearColor>> Facts;
			Facts.Add(TPair<FString, FLinearColor>(FString::Printf(TEXT("%.1f kg"), StackWeight), Colors.TextDim));
			if (bHasDef && !Def.bLegal) { Facts.Add(TPair<FString, FLinearColor>(TEXT("illegal"), Colors.Warning)); }
			if (bHasDef && Def.bStoryItem) { Facts.Add(TPair<FString, FLinearColor>(TEXT("Story-Gegenstand"), Colors.Accent)); }
			Info->AddSlot().AutoHeight()[ Chips(Facts) ];

			TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
			Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
			[
				Btn(TEXT("Benutzen"), [this, Inv, ItemId, District]()
				{
					FString Message;
					const bool bOk = Inv->UseItem(ItemId, District, Message);
					SetStatus(Message.IsEmpty() ? FString(bOk ? TEXT("Benutzt.") : TEXT("Das lässt sich nicht benutzen.")) : Message, bOk);
					RequestRefresh();
				}, false, true, 160.f)
			];
			Actions->AddSlot().AutoWidth()
			[
				Btn(bPersonal ? TEXT("In Kofferraum") : TEXT("In Tasche"), [this, Inv, ItemId, Count, Container, Other]()
				{
					const int32 Moved = Inv->MoveItem(ItemId, Count, Container, Other);
					SetStatus(Moved > 0 ? FString::Printf(TEXT("%d Stück umgelagert."), Moved) : FString(TEXT("Umlagern nicht möglich (voll oder zu schwer).")), Moved > 0);
					RequestRefresh();
				}, false, true, 200.f)
			];

			List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				Panel(
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[ Info ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(18.f, 0.f, 0.f, 0.f)[ Actions ],
					false, 12.f)
			];
		}
	}
	return SAssignNew(ContentScroll, SScrollBox) + SScrollBox::Slot().Padding(0.f, 0.f, 16.f, 0.f)[ List ];
}

// ------------------------------------------------------------------------------ Konto

TSharedRef<SWidget> SMTACharScreen::BuildAccount()
{
	UGameInstance* G = GI.Get();
	UMTAEconomySubsystem* Eco = G ? G->GetSubsystem<UMTAEconomySubsystem>() : nullptr;
	if (!Eco)
	{
		return BuildMissing(TEXT("Die Wirtschaft"));
	}
	const FMTAUiPalette Colors = Pal();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);

	List->AddSlot().AutoHeight()[ Txt(FString::Printf(TEXT("Kontostand: %s"), *Money(Eco->GetMoney())), Colors.Accent, FTitle - 4.f, true) ];
	List->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
	[
		Txt(FString::Printf(TEXT("Insgesamt verdient %s     Insgesamt ausgegeben %s"), *Money(Eco->GetTotalEarned()), *Money(Eco->GetTotalSpent())), Colors.TextDim)
	];

	// Kredit
	const FMTALoanState Loan = Eco->GetLoanState();
	List->AddSlot().AutoHeight().Padding(0.f, 22.f, 0.f, 6.f)[ Txt(TEXT("Kredit bei Aurelia Holdings"), Colors.Accent, FHead, true) ];
	TSharedRef<SVerticalBox> LoanBox = SNew(SVerticalBox);
	LoanBox->AddSlot().AutoHeight()
	[
		Txt(Loan.Outstanding > 0
			? FString::Printf(TEXT("Offener Kredit: %s     Tageszins %.2f %%     Tage offen %d"), *Money(Loan.Outstanding), Eco->GetLoanDailyRate() * 100.f, Loan.DaysOpen)
			: FString(TEXT("Kein offener Kredit.")),
			Loan.Outstanding > 0 ? Colors.Warning : Colors.Text, FBody, Loan.Outstanding > 0)
	];
	LoanBox->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
	[
		Txt(FString::Printf(TEXT("Noch verfügbarer Kreditrahmen: %s     Bisher geliehen %s     Zinsen bezahlt %s"),
			*Money(Eco->GetLoanLimit()), *Money(Loan.TotalBorrowed), *Money(Loan.InterestPaid)), Colors.TextDim)
	];
	if (Loan.UnpaidFines > 0)
	{
		LoanBox->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			Txt(FString::Printf(TEXT("Unbezahlte Bußgelder: %s (sperren neue Kredite)"), *Money(Loan.UnpaidFines)), Colors.Warning, FBody, true)
		];
	}
	if (Loan.bDefaulted || Loan.MissedDays > 0)
	{
		LoanBox->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			Txt(Loan.bDefaulted ? FString(TEXT("Der Kredit ist ausgefallen.")) : FString::Printf(TEXT("Verpasste Zahltage: %d"), Loan.MissedDays), Colors.Warning, FBody, true)
		];
	}
	List->AddSlot().AutoHeight()[ Panel(LoanBox, false, 14.f) ];

	// Buchungen
	List->AddSlot().AutoHeight().Padding(0.f, 22.f, 0.f, 6.f)[ Txt(TEXT("Letzte Buchungen"), Colors.Accent, FHead, true) ];
	const TArray<FMTATransaction> Transactions = Eco->GetRecentTransactions(25);
	if (Transactions.Num() == 0)
	{
		List->AddSlot().AutoHeight()[ Txt(TEXT("Noch keine Buchungen."), Colors.TextDim) ];
	}
	for (const FMTATransaction& Tx : Transactions)
	{
		const int32 Hours = FMath::FloorToInt(Tx.Hour);
		const int32 Minutes = FMath::FloorToInt((Tx.Hour - Hours) * 60.f);
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			Panel(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.2f).VAlign(VAlign_Center)[ Txt(FString::Printf(TEXT("Tag %d, %02d:%02d"), Tx.Day, Hours, Minutes), Colors.TextDim, FBody, false, false) ]
				+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)
				[
					Txt(Tx.Note.IsEmpty() ? TransactionCategoryName(Tx.Category) : FString::Printf(TEXT("%s (%s)"), *Tx.Note, *TransactionCategoryName(Tx.Category)), Colors.Text)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
				[
					Txt((Tx.Amount >= 0 ? FString(TEXT("+")) : FString()) + Money(Tx.Amount), Tx.Amount >= 0 ? Colors.Good : Colors.Warning, FBody, true, false)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(24.f, 0.f, 0.f, 0.f)
				[
					Txt(TEXT("Saldo ") + Money(Tx.BalanceAfter), Colors.TextDim, FBody, false, false)
				],
				true, 10.f)
		];
	}
	return SAssignNew(ContentScroll, SScrollBox) + SScrollBox::Slot().Padding(0.f, 0.f, 16.f, 0.f)[ List ];
}

// ------------------------------------------------------------------------------ Fraktionen

TSharedRef<SWidget> SMTACharScreen::BuildFactions()
{
	UGameInstance* G = GI.Get();
	UMTAStorySubsystem* Story = G ? G->GetSubsystem<UMTAStorySubsystem>() : nullptr;
	UMTAProgressionSubsystem* Prog = G ? G->GetSubsystem<UMTAProgressionSubsystem>() : nullptr;
	if (!Story)
	{
		return BuildMissing(TEXT("Die Story"));
	}
	const FMTAUiPalette Colors = Pal();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);

	struct FFactionInfo { const TCHAR* Id; const TCHAR* Name; const TCHAR* Text; };
	static const FFactionInfo Factions[] =
	{
		{ TEXT("aurelia"), TEXT("Aurelia Holdings"), TEXT("Der Konzern hinter dem Überwachungsnetz Aegis.") },
		{ TEXT("union"), TEXT("Dockers' Union"), TEXT("Hafenarbeiter in Saltmarket, organisiert und kampfbereit.") },
		{ TEXT("collective"), TEXT("Lantern Collective"), TEXT("Hacker und Aktivisten um den Glockenturm.") },
		{ TEXT("wrenchers"), TEXT("Rustline Wrenchers"), TEXT("Schrauber und Fahrer aus Rustline.") },
		{ TEXT("kcg"), TEXT("Kestrel Civil Guard"), TEXT("Die Stadtpolizei von Kestrel Bay.") }
	};

	const float Aegis = MTAProgression::GetStoryAegis(G);
	List->AddSlot().AutoHeight()
	[
		Txt(FString::Printf(TEXT("Aegis-Profil: %.0f / 100"), Aegis), Aegis >= 66.f ? Colors.Warning : Colors.Accent, FHead, true)
	];
	List->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 4.f)[ Bar(Aegis / 100.f, Aegis >= 66.f ? Colors.Warning : Colors.Accent, Colors.Button) ];
	List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
	[
		Txt(TEXT("Je höher das Profil, desto schneller erkennen dich die Kameras und desto teurer wird jeder Kredit."), Colors.TextDim)
	];

	for (const FFactionInfo& Info : Factions)
	{
		const float Standing = Story->GetStanding(Info.Id);
		const FName Tier = Prog ? Prog->GetReputationTier(FName(Info.Id)) : FName(TEXT("neutral"));
		const bool bBad = (Tier == TEXT("enemy") || Tier == TEXT("suspicious"));
		const bool bGood = (Tier == TEXT("allied") || Tier == TEXT("trusted"));

		TSharedRef<SVerticalBox> Card = SNew(SVerticalBox);
		Card->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[ Txt(Info.Name, Colors.Text, FHead, true) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Txt(FString::Printf(TEXT("%s     %+.0f"), *TierName(Tier), Standing), bBad ? Colors.Warning : (bGood ? Colors.Good : Colors.Text), FBody + 2.f, true, false)
			]
		];
		Card->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 4.f)[ Bar((Standing + 100.f) / 200.f, bBad ? Colors.Warning : (bGood ? Colors.Good : Colors.Accent), Colors.Button) ];
		Card->AddSlot().AutoHeight()[ Txt(Info.Text, Colors.TextDim) ];

		if (Prog)
		{
			for (const FMTAReputationReward& Reward : Prog->GetRewardsForFaction(FName(Info.Id)))
			{
				const bool bReached = Reward.bPenalty ? (Standing <= Reward.Threshold) : (Standing >= Reward.Threshold);
				FString Line = FString::Printf(TEXT("%s %s (ab %+.0f): %s"), Reward.bPenalty ? TEXT("Strafe") : TEXT("Belohnung"), *Reward.Name, Reward.Threshold,
					bReached ? (Reward.bPenalty ? TEXT("aktiv") : TEXT("erhalten")) : TEXT("noch nicht erreicht"));
				Card->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					Txt(Line, bReached ? (Reward.bPenalty ? Colors.Warning : Colors.Good) : Colors.TextDim)
				];
			}
		}
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[ Panel(Card, false, 14.f) ];
	}
	return SAssignNew(ContentScroll, SScrollBox) + SScrollBox::Slot().Padding(0.f, 0.f, 16.f, 0.f)[ List ];
}

// ------------------------------------------------------------------------------ Herkunft

TSharedRef<SWidget> SMTACharScreen::BuildOrigin()
{
	UGameInstance* G = GI.Get();
	UMTACharacterOriginSubsystem* Origin = G ? G->GetSubsystem<UMTACharacterOriginSubsystem>() : nullptr;
	UMTAProgressionSubsystem* Prog = G ? G->GetSubsystem<UMTAProgressionSubsystem>() : nullptr;
	if (!Origin)
	{
		return BuildMissing(TEXT("Die Herkunft"));
	}
	const FMTAUiPalette Colors = Pal();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);

	const FString CharName = Origin->GetCharacterName();
	List->AddSlot().AutoHeight()[ Txt(CharName.IsEmpty() ? FString(TEXT("Unbenannt")) : CharName, Colors.Text, FTitle - 4.f, true) ];

	FMTAOrigin Def;
	if (!Origin->GetSelectedOrigin().IsNone() && Origin->GetOrigin(Origin->GetSelectedOrigin(), Def))
	{
		List->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 6.f)[ Txt(Def.Name, Colors.Accent, FHead, true) ];
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[ Txt(Def.Summary, Colors.Text) ];
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)[ Txt(Def.Backstory, Colors.TextDim) ];

		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[ Txt(TEXT("Startvorteile und -nachteile"), Colors.Accent, FHead, true) ];
		if (Def.Traits.Num() == 0)
		{
			List->AddSlot().AutoHeight()[ Txt(TEXT("Keine."), Colors.TextDim) ];
		}
		for (const FMTAOriginTrait& Trait : Def.Traits)
		{
			TSharedRef<SVerticalBox> Card = SNew(SVerticalBox);
			Card->AddSlot().AutoHeight()[ Txt(Trait.Name, Trait.bPositive ? Colors.Good : Colors.Warning, FBody + 2.f, true) ];
			if (Trait.Effects.Num() > 0)
			{
				Card->AddSlot().AutoHeight()[ Txt(EffectsText(Trait.Effects), Colors.TextDim) ];
			}
			List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[ Panel(Card, false, 12.f) ];
		}
	}
	else
	{
		List->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)[ Txt(TEXT("Noch keine Herkunft gewählt."), Colors.TextDim, FHead) ];
	}

	if (Prog)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 18.f, 0.f, 6.f)[ Txt(TEXT("Aktive Boni und Mali"), Colors.Accent, FHead, true) ];
		const TArray<FMTAStatModifier> Mods = Prog->GetAllModifiers();
		if (Mods.Num() == 0)
		{
			List->AddSlot().AutoHeight()[ Txt(TEXT("Keine aktiven Modifikatoren."), Colors.TextDim) ];
		}
		else
		{
			TArray<TPair<FString, FLinearColor>> Chip;
			for (const FMTAStatModifier& Mod : Mods)
			{
				Chip.Add(TPair<FString, FLinearColor>(ModifierText(Mod), Colors.Text));
			}
			List->AddSlot().AutoHeight()[ Panel(Chips(Chip), false, 12.f) ];
		}
	}
	return SAssignNew(ContentScroll, SScrollBox) + SScrollBox::Slot().Padding(0.f, 0.f, 16.f, 0.f)[ List ];
}

// ================================================================================ Controller

AMTACharacterScreenController::AMTACharacterScreenController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// Auch bei pausiertem Spiel ticken (Neuaufbau nach Schriftwechsel).
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AMTACharacterScreenController::BeginPlay()
{
	Super::BeginPlay();
	if (UMTAAccessibilitySubsystem* Acc = UMTAAccessibilitySubsystem::GetActive())
	{
		LastFontScale = Acc->GetUiFontScale();
		bLastHighContrast = Acc->IsHighContrast();
		AccessHandle = Acc->OnAccessibilityChanged.AddUObject(this, &AMTACharacterScreenController::OnAccessibilityChanged);
	}
}

void AMTACharacterScreenController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UMTAAccessibilitySubsystem* Acc = UMTAAccessibilitySubsystem::GetActive())
	{
		Acc->OnAccessibilityChanged.Remove(AccessHandle);
	}
	CloseScreen();
	Super::EndPlay(Reason);
}

APlayerController* AMTACharacterScreenController::GetPC() const
{
	return UGameplayStatics::GetPlayerController(this, 0);
}

void AMTACharacterScreenController::SetMenuController(AMTAMenuController* InMenu)
{
	Menu = InMenu;
}

void AMTACharacterScreenController::OnAccessibilityChanged()
{
	const UMTAAccessibilitySubsystem* Acc = UMTAAccessibilitySubsystem::GetActive();
	if (Acc && (!FMath::IsNearlyEqual(Acc->GetUiFontScale(), LastFontScale) || Acc->IsHighContrast() != bLastHighContrast))
	{
		LastFontScale = Acc->GetUiFontScale();
		bLastHighContrast = Acc->IsHighContrast();
		bRebuildPending = true; // im naechsten Tick neu aufbauen
	}
}

void AMTACharacterScreenController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bRebuildPending)
	{
		bRebuildPending = false;
		if (Widget.IsValid())
		{
			const int32 Tab = Widget->GetTab();
			CloseScreen();
			OpenScreen(Tab);
		}
		return;
	}

	APlayerController* PC = GetPC();
	if (Widget.IsValid() || !bPollHotkeys || bSuppressHotkeys || !PC)
	{
		return;
	}
	if (!PC->WasInputKeyJustPressed(OpenKey) && !PC->WasInputKeyJustPressed(OpenGamepadKey))
	{
		return;
	}

	// Nicht oeffnen, wenn ein anderer Bildschirm oder ein Spezialmodus aktiv ist.
	if (!Menu.IsValid())
	{
		Menu = Cast<AMTAMenuController>(UGameplayStatics::GetActorOfClass(this, AMTAMenuController::StaticClass()));
	}
	if (Menu.IsValid() && Menu->IsMenuOpen())
	{
		return;
	}
	if (const UMTAPhotoModeSubsystem* Photo = UMTAPhotoModeSubsystem::Get(this))
	{
		if (Photo->IsActive()) { return; }
	}
	if (const UGameInstance* GameInst = GetGameInstance())
	{
		const UMTADialogueSubsystem* Dialog = GameInst->GetSubsystem<UMTADialogueSubsystem>();
		if (Dialog && Dialog->IsInConversation()) { return; }
	}
	if (UGameplayStatics::IsGamePaused(this))
	{
		return;
	}
	OpenScreen(SavedTab);
}

void AMTACharacterScreenController::OpenScreen(int32 InitialTab)
{
	if (Widget.IsValid())
	{
		return;
	}
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	APlayerController* PC = GetPC();
	if (!Viewport || !PC)
	{
		return;
	}
	SavedTab = InitialTab;
	const TSharedRef<SMTACharScreen> NewWidget = SNew(SMTACharScreen)
		.GameInstance(TWeakObjectPtr<UGameInstance>(GetGameInstance()))
		.InitialTab(InitialTab)
		.OnClose(FSimpleDelegate::CreateUObject(this, &AMTACharacterScreenController::CloseScreen));
	Widget = NewWidget;
	Viewport->AddViewportWidgetContent(NewWidget, 90);

	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Widget);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	if (!UGameplayStatics::IsGamePaused(this))
	{
		UGameplayStatics::SetGamePaused(this, true);
		bPausedByUs = true;
	}
}

void AMTACharacterScreenController::CloseScreen()
{
	if (!Widget.IsValid())
	{
		return;
	}
	SavedTab = Widget->GetTab();
	if (UWorld* World = GetWorld())
	{
		if (UGameViewportClient* Viewport = World->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
		}
	}
	Widget.Reset();
	if (APlayerController* PC = GetPC())
	{
		if (bPausedByUs) { UGameplayStatics::SetGamePaused(this, false); }
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
	bPausedByUs = false;
}

void AMTACharacterScreenController::ToggleScreen()
{
	if (Widget.IsValid()) { CloseScreen(); }
	else { OpenScreen(SavedTab); }
}
