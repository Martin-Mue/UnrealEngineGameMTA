#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MTANPCTypes.h"
#include "MTANPCActor.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UMTANPCActor : public UInterface
{
	GENERATED_BODY()
};

/**
 * Schnittstelle fuer sichtbare NPC-Actors (Sim-LOD "Near"). Aussehen, Animation und Bewegung
 * liefern spaeter Charakter-/Animations-Agenten; das NPC-System ruft nur diese Funktionen.
 * Ein Actor meldet sich mit UMTANPCSubsystem::RegisterNPCActor an und wieder ab.
 */
class MTA6_API IMTANPCActor
{
	GENERATED_BODY()

public:
	/** ID der Schluesselfigur, die dieser Actor darstellt. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MTA6|NPC")
	FName GetNPCId() const;

	/** Aussehen anwenden (Archetyp-ID aus npcs.json). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MTA6|NPC")
	void ApplyAppearance(FName ArchetypeId);

	/** Neue Aktivitaet/Ort: Actor laeuft hin und spielt passende Animation. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MTA6|NPC")
	void OnActivityChanged(const FMTAScheduleState& NewState);

	/** Zuruf anzeigen/abspielen (Untertitel + spaeter Vertonung). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MTA6|NPC")
	void PlayBark(const FString& Text, FName Category);

	/** Sichtbare NPC-NPC-Interaktion (Gestik, Ambient-Satz). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MTA6|NPC")
	void PlayInteraction(const FMTANPCInteractionEvent& Event);

	/** Gespraech mit dem Spieler beginnt/endet (Kopf drehen, stehen bleiben). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MTA6|NPC")
	void OnConversationStateChanged(bool bInConversation);
};
