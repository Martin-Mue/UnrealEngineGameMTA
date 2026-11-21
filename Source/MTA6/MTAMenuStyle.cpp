#include "MTAMenuStyle.h"

#include "MTALocalizationSubsystem.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace MTAUI
{
	FMTAUiPalette Pal()
	{
		if (const UMTAAccessibilitySubsystem* A = UMTAAccessibilitySubsystem::GetActive())
		{
			return A->GetPalette();
		}
		FMTAUiPalette P;
		P.Background = FLinearColor(0.008f, 0.012f, 0.02f, 0.88f);
		P.Panel = FLinearColor(0.02f, 0.03f, 0.045f, 0.95f);
		P.PanelAlt = FLinearColor(0.035f, 0.05f, 0.07f, 0.95f);
		P.Button = FLinearColor(0.05f, 0.07f, 0.1f, 1.f);
		P.ButtonSelected = FLinearColor(0.9f, 0.45f, 0.08f, 1.f);
		P.Text = FLinearColor(0.92f, 0.94f, 0.96f, 1.f);
		P.TextDim = FLinearColor(0.55f, 0.6f, 0.66f, 1.f);
		P.Accent = FLinearColor(0.95f, 0.5f, 0.1f, 1.f);
		P.Warning = FLinearColor(0.9f, 0.15f, 0.12f, 1.f);
		P.Good = FLinearColor(0.25f, 0.8f, 0.45f, 1.f);
		return P;
	}

	FSlateFontInfo Font(float Size, bool bBold)
	{
		const UMTAAccessibilitySubsystem* A = UMTAAccessibilitySubsystem::GetActive();
		const float Scale = A ? A->GetUiFontScale() : 1.f;
		return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), FMath::RoundToInt(Size * Scale));
	}

	const FSlateBrush* White()
	{
		return FAppStyle::GetBrush("WhiteBrush");
	}

	TAttribute<FText> TrAttr(const FString& Key)
	{
		return TAttribute<FText>::CreateLambda([Key]() { return MTATr(Key); });
	}

	TSharedRef<SWidget> Label(TAttribute<FText> Text, float Size, bool bDim, bool bBold)
	{
		const FMTAUiPalette P = Pal();
		return SNew(STextBlock).Text(Text).Font(Font(Size, bBold))
			.ColorAndOpacity(FSlateColor(bDim ? P.TextDim : P.Text)).AutoWrapText(true);
	}

	TSharedRef<SWidget> Button(TAttribute<FText> Text, FOnClicked OnClicked, float MinWidth, bool bAccent, bool bEnabled)
	{
		const FMTAUiPalette P = Pal();
		return SNew(SBox).MinDesiredWidth(MinWidth)
		[
			SNew(SButton)
			.OnClicked(OnClicked)
			.IsEnabled(bEnabled)
			.ButtonColorAndOpacity(FSlateColor(bAccent ? P.ButtonSelected : P.Button))
			.ContentPadding(FMargin(16.f, 8.f))
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock).Text(Text).Font(Font(18.f, bAccent)).ColorAndOpacity(FSlateColor(bEnabled ? P.Text : P.TextDim))
			]
		];
	}

	TSharedRef<SWidget> Panel(TSharedRef<SWidget> Content, bool bAlt, float Pad)
	{
		const FMTAUiPalette P = Pal();
		return SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FSlateColor(bAlt ? P.PanelAlt : P.Panel)).Padding(Pad)[Content];
	}

	TSharedRef<SWidget> Row(const FString& LabelKey, TSharedRef<SWidget> Control)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center).Padding(0.f, 6.f)[Label(TrAttr(LabelKey), 16.f)]
			+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center).Padding(0.f, 6.f)[Control];
	}

	FText Percent(float Value01)
	{
		return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value01 * 100.f)));
	}

	TSharedRef<SWidget> SliderRow(const FString& LabelKey, float Min, float Max, float Step,
		TFunction<float()> Get, TFunction<void(float)> Set, TFunction<FText(float)> Format)
	{
		const FMTAUiPalette P = Pal();
		const float Range = FMath::Max(Max - Min, KINDA_SMALL_NUMBER);
		return Row(LabelKey,
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([Get, Min, Range]() { return (Get() - Min) / Range; })
				.StepSize(Step / Range)
				.MouseUsesStep(true)
				.SliderBarColor(FSlateColor(P.TextDim))
				.SliderHandleColor(FSlateColor(P.Accent))
				.OnValueChanged_Lambda([Set, Min, Range](float V) { Set(Min + V * Range); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(12.f, 0.f).VAlign(VAlign_Center)
			[
				SNew(SBox).MinDesiredWidth(64.f)
				[
					SNew(STextBlock).Font(Font(16.f)).ColorAndOpacity(FSlateColor(P.Text))
					.Text_Lambda([Get, Format]() { return Format(Get()); })
				]
			]);
	}

	TSharedRef<SWidget> ToggleRow(const FString& LabelKey, TFunction<bool()> Get, TFunction<void(bool)> Set)
	{
		return Row(LabelKey,
			SNew(SCheckBox)
			.IsChecked_Lambda([Get]() { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Set](ECheckBoxState S) { Set(S == ECheckBoxState::Checked); }));
	}

	TSharedRef<SWidget> ChoiceRow(const FString& LabelKey, TArray<FString> OptionKeys, TFunction<int32()> Get, TFunction<void(int32)> Set)
	{
		const FMTAUiPalette P = Pal();
		const int32 Num = OptionKeys.Num();
		auto Step = [Get, Set, Num](int32 Delta)
		{
			if (Num > 0) { Set((Get() + Delta + Num) % Num); }
			return FReply::Handled();
		};
		return Row(LabelKey,
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).ContentPadding(FMargin(10.f, 4.f)).ButtonColorAndOpacity(FSlateColor(P.Button))
				.OnClicked_Lambda([Step]() { return Step(-1); })
				[ SNew(STextBlock).Text(FText::FromString(TEXT("<"))).Font(Font(18.f, true)).ColorAndOpacity(FSlateColor(P.Text)) ]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).HAlign(HAlign_Center).Padding(8.f, 0.f)
			[
				SNew(STextBlock).Font(Font(16.f)).ColorAndOpacity(FSlateColor(P.Accent))
				.Text_Lambda([OptionKeys, Get]() { const int32 I = Get(); return OptionKeys.IsValidIndex(I) ? MTATr(OptionKeys[I]) : FText::GetEmpty(); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).ContentPadding(FMargin(10.f, 4.f)).ButtonColorAndOpacity(FSlateColor(P.Button))
				.OnClicked_Lambda([Step]() { return Step(1); })
				[ SNew(STextBlock).Text(FText::FromString(TEXT(">"))).Font(Font(18.f, true)).ColorAndOpacity(FSlateColor(P.Text)) ]
			]);
	}
}
