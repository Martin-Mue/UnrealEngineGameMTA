#include "MTACharacterCreator.h"
#include "MTAWardrobe.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformMath.h"
#include "Misc/Paths.h"

namespace
{
	/** Optionen werden als "Anzeigename|id" abgelegt. */
	FString OptLabel(const FString& S)
	{
		int32 Idx;
		return S.FindChar(TEXT('|'), Idx) ? S.Left(Idx) : S;
	}

	FString OptId(const FString& S)
	{
		int32 Idx;
		return S.FindChar(TEXT('|'), Idx) ? S.Mid(Idx + 1) : S;
	}

	const TCHAR* SlotLabel(EMTAWardrobeSlot S)
	{
		static const TCHAR* L[] = { TEXT("Kopfbedeckung"), TEXT("Brille"), TEXT("Maske"), TEXT("Halstuch"), TEXT("Oberteil"), TEXT("Jacke"),
			TEXT("Ganzkörper"), TEXT("Hose"), TEXT("Handschuhe"), TEXT("Schuhe"), TEXT("Tasche") };
		return L[static_cast<int32>(S)];
	}

	const TArray<EMTAWardrobeSlot>& SlotOrder()
	{
		static const TArray<EMTAWardrobeSlot> O = { EMTAWardrobeSlot::Fullbody, EMTAWardrobeSlot::Top, EMTAWardrobeSlot::Jacket, EMTAWardrobeSlot::Bottom,
			EMTAWardrobeSlot::Shoes, EMTAWardrobeSlot::Hat, EMTAWardrobeSlot::Glasses, EMTAWardrobeSlot::Mask, EMTAWardrobeSlot::Neck,
			EMTAWardrobeSlot::Gloves, EMTAWardrobeSlot::Bag };
		return O;
	}

	FSlateFontInfo HeaderFont() { return FCoreStyle::GetDefaultFontStyle("Bold", 13); }
}

void SMTACharacterCreator::Construct(const FArguments& InArgs)
{
	Appearances = InArgs._Appearances;
	Wardrobe = InArgs._Wardrobe;
	Origins = InArgs._OriginProvider;
	OnAppearanceChanged = InArgs._OnAppearanceChanged;
	OnConfirmed = InArgs._OnConfirmed;
	OnCancelled = InArgs._OnCancelled;
	Current = InArgs._InitialAppearance;

	for (EMTAWardrobeSlot S : SlotOrder())
	{
		SlotCombos.Add(S, MakeShared<FComboHandle>());
	}

	// Ohne Startwert: erstes Creator-Preset, sonst Zufall.
	if (Current.Id.IsNone())
	{
		if (UMTAAppearanceSubsystem* A = Appearances.Get())
		{
			if (A->GetCreatorPresets().Num() > 0) { Current = A->GetCreatorPresets()[0]; }
			else if (A->IsLoaded()) { Current = A->GenerateRandomPlayerLook(FMath::Rand()); }
		}
		if (Current.Id.IsNone()) { Current.Id = TEXT("player"); }
	}
	if (Origins.IsValid())
	{
		Current.OriginId = Origins->GetSelectedOrigin();
	}

	ChildSlot
	[
		BuildContent()
	];
	RefreshDependentOptions();
	RefreshMarksList();
}

void SMTACharacterCreator::SetAppearance(const FMTAAppearance& NewAppearance, bool bNotify)
{
	Current = NewAppearance;
	RefreshDependentOptions();
	RefreshMarksList();
	if (bNotify) { Notify(); }
}

void SMTACharacterCreator::Notify()
{
	OnAppearanceChanged.ExecuteIfBound(Current);
}

void SMTACharacterCreator::SetStatus(const FString& Text)
{
	StatusText = Text;
}

FString SMTACharacterCreator::GetFilePath() const
{
	FString Name = FPaths::MakeValidFileName(FileName);
	if (Name.IsEmpty()) { Name = TEXT("character"); }
	if (!Name.EndsWith(TEXT(".json"))) { Name += TEXT(".json"); }
	return FMTAAppearance::GetDefaultSaveDir() / Name;
}

bool SMTACharacterCreator::SaveCurrent()
{
	const bool bOk = Current.SaveToFile(GetFilePath());
	SetStatus(bOk ? FString::Printf(TEXT("Gespeichert: %s"), *GetFilePath()) : TEXT("Speichern fehlgeschlagen"));
	return bOk;
}

bool SMTACharacterCreator::LoadCurrent()
{
	FMTAAppearance Loaded;
	if (!Loaded.LoadFromFile(GetFilePath()))
	{
		SetStatus(TEXT("Laden fehlgeschlagen (Datei fehlt oder ungültig)"));
		return false;
	}
	SetAppearance(Loaded, true);
	SetStatus(FString::Printf(TEXT("Geladen: %s"), *GetFilePath()));
	return true;
}

void SMTACharacterCreator::ApplyRandom()
{
	UMTAAppearanceSubsystem* A = Appearances.Get();
	if (!A || !A->IsLoaded())
	{
		SetStatus(TEXT("Keine Aussehen-Daten geladen"));
		return;
	}
	FMTAAppearance R = A->GenerateRandomPlayerLook(FMath::Rand());
	R.DisplayName = Current.DisplayName;
	R.OriginId = Current.OriginId;
	SetAppearance(R, true);
	SetStatus(TEXT("Zufallsaussehen erzeugt"));
}

void SMTACharacterCreator::ApplyCreatorPreset(const FString& PresetId)
{
	UMTAAppearanceSubsystem* A = Appearances.Get();
	if (!A) { return; }
	for (const FMTAAppearance& P : A->GetCreatorPresets())
	{
		if (P.Id == FName(*PresetId))
		{
			FMTAAppearance N = P;
			N.DisplayName = Current.DisplayName.IsEmpty() ? P.DisplayName : Current.DisplayName;
			N.OriginId = Current.OriginId;
			SetAppearance(N, true);
			return;
		}
	}
}

void SMTACharacterCreator::ApplyOutfitPreset(const FString& PresetId)
{
	UMTAWardrobeSubsystem* W = Wardrobe.Get();
	if (!W) { return; }
	FRandomStream Stream(FMath::Rand());
	TMap<EMTAWardrobeSlot, FName> Items;
	TMap<EMTAWardrobeSlot, FLinearColor> Tints;
	if (W->BuildOutfitFromPreset(FName(*PresetId), Stream, Current.Frame, Current.GetLifeStage() == EMTALifeStage::Child, Items, Tints))
	{
		Current.Outfit = Items;
		Current.OutfitTints = Tints;
		Notify();
	}
}

void SMTACharacterCreator::AddMark(const FString& MarkId)
{
	UMTAAppearanceSubsystem* A = Appearances.Get();
	const FMTAMarkDef* D = A ? A->FindMarkDef(FName(*MarkId)) : nullptr;
	if (!D || Current.Marks.Num() >= 8) { return; }
	FMTABodyMark M;
	M.Id = D->Id; M.Kind = D->Kind; M.Color = D->Color;
	M.Region = D->Regions.Num() > 0 ? D->Regions[0] : EMTABodyRegion::Torso;
	Current.Marks.Add(M);
	RefreshMarksList();
	Notify();
}

void SMTACharacterCreator::FillOptions(FComboHandle& Handle, const TArray<TPair<FString, FString>>& IdLabel, bool bAddNone)
{
	Handle.Options->Reset();
	if (bAddNone) { Handle.Options->Add(MakeShared<FString>(TEXT("— keine —|"))); }
	for (const TPair<FString, FString>& P : IdLabel)
	{
		Handle.Options->Add(MakeShared<FString>(P.Value + TEXT("|") + P.Key));
	}
	if (Handle.Combo.IsValid()) { Handle.Combo->RefreshOptions(); }
}

void SMTACharacterCreator::RefreshDependentOptions()
{
	UMTAAppearanceSubsystem* A = Appearances.Get();
	UMTAWardrobeSubsystem* W = Wardrobe.Get();

	if (A)
	{
		TArray<TPair<FString, FString>> Hair;
		for (const FMTAHairStyleDef& H : A->GetHairStyles())
		{
			if (Current.Age >= H.AgeMin && Current.Age <= H.AgeMax && (H.Frames.Num() == 0 || H.Frames.Contains(Current.Frame)))
			{
				Hair.Emplace(H.Id.ToString(), H.DisplayName);
			}
		}
		FillOptions(*HairCombo, Hair, true);

		TArray<TPair<FString, FString>> Beards;
		for (const FName& B : A->GetBeardStyles()) { Beards.Emplace(B.ToString(), B.ToString().Replace(TEXT("beard_"), TEXT(""))); }
		FillOptions(*BeardCombo, Beards, false);

		TArray<TPair<FString, FString>> Marks;
		for (const FMTAMarkDef& D : A->GetMarkDefs())
		{
			const TCHAR* Kind = D.Kind == EMTAMarkKind::Scar ? TEXT("Narbe") : (D.Kind == EMTAMarkKind::Tattoo ? TEXT("Tattoo") : TEXT("Implantat"));
			Marks.Emplace(D.Id.ToString(), FString::Printf(TEXT("%s (%s)"), *D.DisplayName, Kind));
		}
		FillOptions(*MarkCombo, Marks, false);

		TArray<TPair<FString, FString>> Presets;
		for (const FMTAAppearance& P : A->GetCreatorPresets()) { Presets.Emplace(P.Id.ToString(), P.DisplayName); }
		FillOptions(*PresetCombo, Presets, false);
	}

	if (W)
	{
		const bool bChild = Current.GetLifeStage() == EMTALifeStage::Child;
		for (EMTAWardrobeSlot S : SlotOrder())
		{
			FMTAWardrobeQuery Q;
			Q.bFilterSlot = true; Q.Slot = S; Q.Frame = Current.Frame; Q.bChild = bChild; Q.bOnlyForSale = false;
			TArray<TPair<FString, FString>> List;
			for (const FName& Id : W->QueryItems(Q))
			{
				if (const FMTAWardrobeItem* I = W->FindItem(Id)) { List.Emplace(Id.ToString(), I->DisplayName); }
			}
			FillOptions(*SlotCombos.FindChecked(S), List, true);
		}
		TArray<TPair<FString, FString>> Outfits;
		for (const FMTAOutfitPreset& P : W->GetPresets())
		{
			if (!P.Tags.Contains(TEXT("overlay"))) { Outfits.Emplace(P.Id.ToString(), P.DisplayName); }
		}
		FillOptions(*OutfitPresetCombo, Outfits, false);
	}

	if (Origins.IsValid())
	{
		TArray<FMTAOriginChoice> Choices;
		Origins->GetOriginChoices(Choices);
		TArray<TPair<FString, FString>> List;
		for (const FMTAOriginChoice& C : Choices) { List.Emplace(C.Id.ToString(), C.DisplayName.ToString()); }
		FillOptions(*OriginCombo, List, true);
	}
}

void SMTACharacterCreator::RefreshMarksList()
{
	if (!MarksBox.IsValid()) { return; }
	MarksBox->ClearChildren();
	UMTAAppearanceSubsystem* A = Appearances.Get();
	for (int32 i = 0; i < Current.Marks.Num(); ++i)
	{
		const FMTAMarkDef* D = A ? A->FindMarkDef(Current.Marks[i].Id) : nullptr;
		const FString Name = D ? D->DisplayName : Current.Marks[i].Id.ToString();
		MarksBox->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(Name))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([this, i]() { return Current.Marks.IsValidIndex(i) ? Current.Marks[i].Intensity : 0.f; })
				.OnValueChanged_Lambda([this, i](float V) { if (Current.Marks.IsValidIndex(i)) { Current.Marks[i].Intensity = V; Notify(); } })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
			[
				SNew(SButton).Text(FText::FromString(TEXT("Entfernen")))
				.OnClicked_Lambda([this, i]()
				{
					if (Current.Marks.IsValidIndex(i)) { Current.Marks.RemoveAt(i); }
					RefreshMarksList();
					Notify();
					return FReply::Handled();
				})
			]
		];
	}
}

TSharedRef<SWidget> SMTACharacterCreator::MakeSection(const FString& Title, const TSharedRef<SWidget>& Body) const
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 4)
		[
			SNew(STextBlock).Text(FText::FromString(Title)).Font(HeaderFont())
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Body
		];
}

TSharedRef<SWidget> SMTACharacterCreator::MakeSlider(const FString& Label, float Min, float Max, TFunction<float()> Get, TFunction<void(float)> Set, int32 Decimals)
{
	const float Range = FMath::Max(KINDA_SMALL_NUMBER, Max - Min);
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(170.f)
			[
				SNew(STextBlock).Text(FText::FromString(Label))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0, 2)
		[
			SNew(SSlider)
			.Value_Lambda([Get, Min, Range]() { return FMath::Clamp((Get() - Min) / Range, 0.f, 1.f); })
			.OnValueChanged_Lambda([this, Set, Min, Range](float V) { Set(Min + V * Range); Notify(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
		[
			SNew(SBox).WidthOverride(48.f)
			[
				SNew(STextBlock).Text_Lambda([Get, Decimals]()
				{
					return FText::FromString(Decimals <= 0 ? FString::FromInt(FMath::RoundToInt(Get())) : FString::SanitizeFloat(FMath::RoundToFloat(Get() * FMath::Pow(10.f, Decimals)) / FMath::Pow(10.f, Decimals)));
				})
			]
		];
}

TSharedRef<SWidget> SMTACharacterCreator::MakeCombo(const FString& Label, const TSharedRef<FComboHandle>& Handle, TFunction<FString()> GetId, TFunction<void(const FString&)> SetId)
{
	TSharedRef<FOptionList> Options = Handle->Options;
	// Anzeige des aktuellen Eintrags: Label aus der Optionsliste, sonst die Id.
	auto CurrentLabel = [Options, GetId]() -> FText
	{
		const FString Id = GetId();
		for (const TSharedPtr<FString>& O : *Options)
		{
			if (O.IsValid() && OptId(*O) == Id) { return FText::FromString(OptLabel(*O)); }
		}
		return FText::FromString(Id.IsEmpty() ? TEXT("— keine —") : Id);
	};

	TSharedRef<FStringCombo> Combo = SNew(FStringCombo)
		.OptionsSource(static_cast<const FOptionList*>(&Options.Get()))
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
		{
			return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? OptLabel(*Item) : FString()));
		})
		.OnSelectionChanged_Lambda([this, SetId](TSharedPtr<FString> Item, ESelectInfo::Type)
		{
			if (Item.IsValid())
			{
				SetId(OptId(*Item));
			}
		})
		[
			SNew(STextBlock).Text_Lambda(CurrentLabel)
		];
	Handle->Combo = Combo;

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(170.f)
			[
				SNew(STextBlock).Text(FText::FromString(Label))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 2)
		[
			Combo
		];
}

TSharedRef<SWidget> SMTACharacterCreator::MakeSwatchRow(const FString& Label, TFunction<TArray<FLinearColor>()> GetColors, TFunction<void(const FLinearColor&)> OnPick)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (const FLinearColor& C : GetColors())
	{
		Row->AddSlot().AutoWidth().Padding(1)
		[
			SNew(SBox).WidthOverride(22.f).HeightOverride(22.f)
			[
				SNew(SButton)
				.ButtonColorAndOpacity(C)
				.ContentPadding(FMargin(0.f))
				.OnClicked_Lambda([this, OnPick, C]() { OnPick(C); Notify(); return FReply::Handled(); })
			]
		];
	}
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(170.f)
			[
				SNew(STextBlock).Text(FText::FromString(Label))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			Row
		];
}

TSharedRef<SWidget> SMTACharacterCreator::BuildContent()
{
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	auto Add = [&Body](const TSharedRef<SWidget>& W)
	{
		Body->AddSlot().AutoHeight()[W];
	};

	// --- Name / Herkunft ---
	{
		TSharedRef<SVerticalBox> S = SNew(SVerticalBox);
		S->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(170.f)[SNew(STextBlock).Text(FText::FromString(TEXT("Name")))]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(Current.DisplayName); })
				.OnTextChanged_Lambda([this](const FText& T) { Current.DisplayName = T.ToString(); Notify(); })
			]
		];
		if (Origins.IsValid())
		{
			S->AddSlot().AutoHeight()
			[
				MakeCombo(TEXT("Herkunft"), OriginCombo,
					[this]() { return Current.OriginId.IsNone() ? FString() : Current.OriginId.ToString(); },
					[this](const FString& Id)
					{
						Current.OriginId = FName(*Id);
						if (Origins.IsValid()) { Origins->SelectOrigin(Current.OriginId); }
						Notify();
					})
			];
		}
		Add(MakeSection(TEXT("Identität"), S));
	}

	// --- Presets / Zufall ---
	{
		TSharedRef<SVerticalBox> S = SNew(SVerticalBox);
		S->AddSlot().AutoHeight()
		[
			MakeCombo(TEXT("Vorgefertigtes Aussehen"), PresetCombo, []() { return FString(); }, [this](const FString& Id) { ApplyCreatorPreset(Id); })
		];
		S->AddSlot().AutoHeight().Padding(0, 4)
		[
			SNew(SButton).Text(FText::FromString(TEXT("Zufall")))
			.OnClicked_Lambda([this]() { ApplyRandom(); return FReply::Handled(); })
		];
		Add(MakeSection(TEXT("Presets"), S));
	}

	// --- Körper ---
	{
		TSharedRef<SVerticalBox> S = SNew(SVerticalBox);
		S->AddSlot().AutoHeight()
		[
			MakeCombo(TEXT("Körperrahmen"), FrameCombo, [this]() { return UMTAAppearanceLibrary::FrameToId(Current.Frame); },
				[this](const FString& Id) { UMTAAppearanceLibrary::FrameFromId(Id, Current.Frame); RefreshDependentOptions(); Notify(); })
		];
		S->AddSlot().AutoHeight()
		[
			MakeCombo(TEXT("Körpertyp"), BodyCombo, [this]() { return UMTAAppearanceLibrary::BodyTypeToId(Current.BodyType); },
				[this](const FString& Id) { UMTAAppearanceLibrary::BodyTypeFromId(Id, Current.BodyType); Notify(); })
		];
		S->AddSlot().AutoHeight()
		[
			MakeSlider(TEXT("Alter"), 3.f, 90.f, [this]() { return static_cast<float>(Current.Age); },
				[this](float V)
				{
					const EMTALifeStage Before = Current.GetLifeStage();
					Current.Age = FMath::RoundToInt(V);
					if (Current.GetLifeStage() != Before) { RefreshDependentOptions(); }
				}, 0)
		];
		S->AddSlot().AutoHeight()[MakeSlider(TEXT("Größe (cm)"), 95.f, 215.f, [this]() { return Current.HeightCm; }, [this](float V) { Current.HeightCm = V; }, 0)];
		S->AddSlot().AutoHeight()[MakeSlider(TEXT("Körperfülle"), 0.f, 1.f, [this]() { return Current.Build; }, [this](float V) { Current.Build = V; })];
		S->AddSlot().AutoHeight()[MakeSlider(TEXT("Muskeltonus"), 0.f, 1.f, [this]() { return Current.Muscle; }, [this](float V) { Current.Muscle = V; })];
		Add(MakeSection(TEXT("Körper"), S));
	}

	// --- Gesicht ---
	{
		TSharedRef<SVerticalBox> S = SNew(SVerticalBox);
		for (const FName& Name : UMTAAppearanceLibrary::GetFaceMorphNames())
		{
			S->AddSlot().AutoHeight()
			[
				MakeSlider(Name.ToString().Replace(TEXT("_"), TEXT(" ")), -1.f, 1.f,
					[this, Name]() { const float* V = Current.FaceMorphs.Find(Name); return V ? *V : 0.f; },
					[this, Name](float V) { Current.FaceMorphs.Add(Name, V); })
			];
		}
		Add(MakeSection(TEXT("Gesicht"), S));
	}

	// --- Haut / Augen ---
	{
		TSharedRef<SVerticalBox> S = SNew(SVerticalBox);
		S->AddSlot().AutoHeight()[MakeSlider(TEXT("Hautton"), 0.f, 1.f, [this]() { return Current.SkinTone; }, [this](float V) { Current.SkinTone = V; })];
		S->AddSlot().AutoHeight()[MakeSlider(TEXT("Sommersprossen"), 0.f, 1.f, [this]() { return Current.Freckles; }, [this](float V) { Current.Freckles = V; })];
		S->AddSlot().AutoHeight()
		[
			MakeSwatchRow(TEXT("Augenfarbe"),
				[this]() { UMTAAppearanceSubsystem* A = Appearances.Get(); return A ? A->GetEyeColorOptions() : TArray<FLinearColor>(); },
				[this](const FLinearColor& C) { Current.EyeColor = C; })
		];
		Add(MakeSection(TEXT("Haut und Augen"), S));
	}

	// --- Haare / Bart / Make-up ---
	{
		TSharedRef<SVerticalBox> S = SNew(SVerticalBox);
		S->AddSlot().AutoHeight()
		[
			MakeCombo(TEXT("Frisur"), HairCombo, [this]() { return Current.HairStyle.IsNone() ? FString() : Current.HairStyle.ToString(); },
				[this](const FString& Id) { Current.HairStyle = FName(*Id); Notify(); })
		];
		S->AddSlot().AutoHeight()
		[
			MakeSwatchRow(TEXT("Haarfarbe"),
				[this]() { UMTAAppearanceSubsystem* A = Appearances.Get(); return A ? A->GetHairColorOptions() : TArray<FLinearColor>(); },
				[this](const FLinearColor& C) { Current.HairColor = C; Current.BeardColor = C; })
		];
		S->AddSlot().AutoHeight()[MakeSlider(TEXT("Ergrauung"), 0.f, 1.f, [this]() { return Current.HairGray; }, [this](float V) { Current.HairGray = V; })];
		S->AddSlot().AutoHeight()
		[
			MakeCombo(TEXT("Bart"), BeardCombo, [this]() { return Current.BeardStyle.IsNone() ? FString() : Current.BeardStyle.ToString(); },
				[this](const FString& Id) { Current.BeardStyle = FName(*Id); Notify(); })
		];
		S->AddSlot().AutoHeight()[MakeSlider(TEXT("Make-up"), 0.f, 1.f, [this]() { return Current.Makeup.Intensity; }, [this](float V) { Current.Makeup.Intensity = V; })];
		S->AddSlot().AutoHeight()
		[
			MakeSwatchRow(TEXT("Lippenfarbe"),
				[]() { return TArray<FLinearColor>{ FLinearColor(0.35f, 0.05f, 0.07f), FLinearColor(0.2f, 0.02f, 0.05f), FLinearColor(0.45f, 0.12f, 0.1f), FLinearColor(0.08f, 0.02f, 0.12f), FLinearColor(0.02f, 0.2f, 0.18f) }; },
				[this](const FLinearColor& C) { Current.Makeup.LipColor = C; })
		];
		Add(MakeSection(TEXT("Haare, Bart, Make-up"), S));
	}

	// --- Marken ---
	{
		TSharedRef<SVerticalBox> S = SNew(SVerticalBox);
		S->AddSlot().AutoHeight()
		[
			MakeCombo(TEXT("Narbe/Tattoo/Implantat"), MarkCombo, [this]() { return MarkToAdd; }, [this](const FString& Id) { MarkToAdd = Id; })
		];
		S->AddSlot().AutoHeight().Padding(0, 4)
		[
			SNew(SButton).Text(FText::FromString(TEXT("Hinzufügen")))
			.OnClicked_Lambda([this]() { AddMark(MarkToAdd); return FReply::Handled(); })
		];
		MarksBox = SNew(SVerticalBox);
		S->AddSlot().AutoHeight()[MarksBox.ToSharedRef()];
		Add(MakeSection(TEXT("Narben, Tattoos, Implantate"), S));
	}

	// --- Kleidung ---
	if (Wardrobe.IsValid())
	{
		TSharedRef<SVerticalBox> S = SNew(SVerticalBox);
		S->AddSlot().AutoHeight()
		[
			MakeCombo(TEXT("Outfit-Preset"), OutfitPresetCombo, []() { return FString(); }, [this](const FString& Id) { ApplyOutfitPreset(Id); })
		];
		for (EMTAWardrobeSlot Slot : SlotOrder())
		{
			S->AddSlot().AutoHeight()
			[
				MakeCombo(SlotLabel(Slot), SlotCombos.FindChecked(Slot),
					[this, Slot]() { const FName* Id = Current.Outfit.Find(Slot); return Id ? Id->ToString() : FString(); },
					[this, Slot](const FString& Id)
					{
						if (Id.IsEmpty()) { Current.Outfit.Remove(Slot); Current.OutfitTints.Remove(Slot); }
						else { Current.Outfit.Add(Slot, FName(*Id)); }
						Notify();
					})
			];
		}
		Add(MakeSection(TEXT("Kleidung"), S));
	}

	// --- Speichern/Laden ---
	{
		TSharedRef<SVerticalBox> S = SNew(SVerticalBox);
		S->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(170.f)[SNew(STextBlock).Text(FText::FromString(TEXT("Dateiname (Saved/Characters)")))]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(FileName); })
				.OnTextChanged_Lambda([this](const FText& T) { FileName = T.ToString(); })
			]
		];
		S->AddSlot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				SNew(SButton).Text(FText::FromString(TEXT("Speichern")))
				.OnClicked_Lambda([this]() { SaveCurrent(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).Text(FText::FromString(TEXT("Laden")))
				.OnClicked_Lambda([this]() { LoadCurrent(); return FReply::Handled(); })
			]
		];
		Add(MakeSection(TEXT("Speichern und Laden (JSON)"), S));
	}

	return SNew(SBorder)
		.Padding(FMargin(16.f))
		.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.92f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Charaktererstellung"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
			]
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(0, 8)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					Body
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(StatusText); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton).Text(FText::FromString(TEXT("Bestätigen")))
					.OnClicked_Lambda([this]() { OnConfirmed.ExecuteIfBound(Current); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(FText::FromString(TEXT("Abbrechen")))
					.OnClicked_Lambda([this]() { OnCancelled.ExecuteIfBound(); return FReply::Handled(); })
				]
			]
		];
}
