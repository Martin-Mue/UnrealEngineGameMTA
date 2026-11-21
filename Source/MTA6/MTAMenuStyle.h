#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Styling/SlateTypes.h"
#include "Input/Reply.h"
#include "MTAAccessibilitySubsystem.h"
#include "MTALocalizationSubsystem.h"

/**
 * Gemeinsame Bausteine aller MTA-Menues (nur Standard-Slate-Styles, keine Assets).
 * Farben, Schriftgroessen und Kontrast kommen aus UMTAAccessibilitySubsystem; Texte aus der
 * Lokalisierung. Die Bausteine lesen Werte beim Bau; bei Aenderung baut der
 * AMTAMenuController die Ansicht neu auf.
 */
namespace MTAUI
{
	/** Aktuelle Palette (Fallback-Palette, solange das Subsystem fehlt). */
	MTA6_API FMTAUiPalette Pal();

	/** Skalierte Standardschrift (Groesse in Punkt bei Skalierung 1.0). */
	MTA6_API FSlateFontInfo Font(float Size, bool bBold = false);

	MTA6_API const FSlateBrush* White();

	/** Text-Attribut, das bei Sprachwechsel automatisch neu aufgeloest wird. */
	MTA6_API TAttribute<FText> TrAttr(const FString& Key);

	MTA6_API TSharedRef<SWidget> Label(TAttribute<FText> Text, float Size = 16.f, bool bDim = false, bool bBold = false);
	MTA6_API TSharedRef<SWidget> Button(TAttribute<FText> Text, FOnClicked OnClicked, float MinWidth = 240.f, bool bAccent = false, bool bEnabled = true);
	MTA6_API TSharedRef<SWidget> Panel(TSharedRef<SWidget> Content, bool bAlt = false, float Pad = 16.f);

	/** Beschriftung links, Steuerelement rechts. */
	MTA6_API TSharedRef<SWidget> Row(const FString& LabelKey, TSharedRef<SWidget> Control);

	MTA6_API TSharedRef<SWidget> SliderRow(const FString& LabelKey, float Min, float Max, float Step,
		TFunction<float()> Get, TFunction<void(float)> Set, TFunction<FText(float)> Format);
	MTA6_API TSharedRef<SWidget> ToggleRow(const FString& LabelKey, TFunction<bool()> Get, TFunction<void(bool)> Set);
	/** Auswahl durch Pfeiltasten; OptionKeys sind Lokalisierungsschluessel. */
	MTA6_API TSharedRef<SWidget> ChoiceRow(const FString& LabelKey, TArray<FString> OptionKeys, TFunction<int32()> Get, TFunction<void(int32)> Set);

	/** Prozentanzeige "73%". */
	MTA6_API FText Percent(float Value01);
}
