#pragma once

#include "CoreMinimal.h"

/** Vorschau-Daten eines Speicherplatzes (vom Hauptagenten aus MTASaveGame gefuellt). */
struct FMTASaveSlotInfo
{
	/** Technischer Slot-Name (z. B. "Slot1", "Auto"). */
	FString SlotName;
	/** Anzeigename (z. B. "Slot 1"). */
	FString DisplayName;
	bool bExists = false;
	bool bAutosave = false;
	float PlaytimeSeconds = 0.f;
	FString Mission;
	FString Location;
	FDateTime SavedAt = FDateTime(0);
};

/** Speichern/Laden laufen nur ueber diese Delegates; der Hauptagent bindet sie an MTASaveGame. */
DECLARE_MULTICAST_DELEGATE_OneParam(FMTASaveRequested, FString /*Slot*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FMTALoadRequested, FString /*Slot*/);
DECLARE_MULTICAST_DELEGATE(FMTANewGameRequested);
DECLARE_MULTICAST_DELEGATE(FMTAQuitRequested);
/** Der Hauptagent fuellt die Slot-Liste (Autosave + Slots) fuer die Auswahl. */
DECLARE_DELEGATE_OneParam(FMTAQuerySlots, TArray<FMTASaveSlotInfo>& /*OutSlots*/);
DECLARE_DELEGATE_OneParam(FMTASlotChosen, const FString& /*SlotName*/);

/** Eintrag eines Button-Menues. */
struct FMTAMenuItem
{
	FString LabelKey;
	FSimpleDelegate OnClick;
	bool bEnabled = true;
};
