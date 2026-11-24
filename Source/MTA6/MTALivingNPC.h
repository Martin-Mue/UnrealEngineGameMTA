#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MTAInteractionInterface.h"
#include "MTANPCActor.h"
#include "MTALivingNPC.generated.h"

class UTextRenderComponent;
class AMTALivingWorldManager;

/**
 * Sichtbare, laufende Figur der lebendigen Welt (Nachfolger von AMTADialogueNPC).
 *
 * Der Actor ist nur die Darstellung eines Agenten aus AMTALivingWorldManager: Der Manager haelt
 * Tagesablauf, Weg und Gespraeche als Daten und bindet Actors aus einem Pool an die naechsten
 * Agenten (Sim-LOD, Budget). Bewegung ohne NavMesh: der Manager ruft AddMovementInput Richtung
 * Wegpunkte entlang der Gehwege; die Mannequin-AnimBP reagiert auf die Geschwindigkeit.
 *
 * Ueber dem Kopf: Namensschild, Aktivitaet ("liest Zeitung", "isst") und Sprechblase
 * (UTextRenderComponent, klein, nur in der Naehe sichtbar).
 *
 * Schnittstellen: IMTAInteractable (Taste E: Gespraech bzw. Gruss), IMTANPCActor (Registrierung
 * beim UMTANPCSubsystem: Barks, sichtbare NPC-NPC-Interaktionen, Aktivitaetswechsel).
 */
UCLASS(NotBlueprintable)
class MTA6_API AMTALivingNPC : public ACharacter, public IMTAInteractable, public IMTANPCActor
{
	GENERATED_BODY()

public:
	AMTALivingNPC();

	virtual void Tick(float DeltaSeconds) override;

	/** Schluessel-ID aus npcs.json oder street_NN fuer Strassenfiguren. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|NPC")
	FName NPCId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|NPC")
	FString DisplayName;

	/** Profil aus npcs.json (Schluesselfigur oder Nebenfigur). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|NPC")
	bool bKeyNPC = false;

	/** Hat einen Dialog (dialogues/*.json). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|NPC")
	bool bHasDialogue = false;

	/** Index des Agenten im Manager; INDEX_NONE = im Pool (unsichtbar). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|NPC")
	int32 AgentIndex = INDEX_NONE;

	/** Aussehen-Archetyp (vorerst nur gespeichert; die Figur bleibt das Mannequin). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|NPC")
	FName AppearanceArchetype;

	/** Bindet den Actor an einen Agenten und macht ihn sichtbar. */
	void BindAgent(AMTALivingWorldManager* InManager, int32 InAgentIndex, FName InId, const FString& InName,
		bool bInKeyNPC, bool bInHasDialogue, float InWalkSpeed, float InScale);

	/** Loest die Bindung, versteckt den Actor und schaltet Tick/Kollision ab (zurueck in den Pool). */
	void UnbindAgent();

	bool IsPooled() const { return AgentIndex == INDEX_NONE; }

	/** Text des Namensschilds (z. B. "Name (Beruf)"); DisplayName bleibt der reine Name. */
	void SetNameLabel(const FString& Text);

	/** Aktivitaetszeile unter dem Namen. */
	void SetActivityText(const FString& Text);

	/** Sprechblase fuer Duration Sekunden (lange Texte werden umgebrochen). */
	void ShowSpeech(const FString& Text, float Duration, FColor Color = FColor::White);
	void ClearSpeech();

	bool IsSpeaking() const { return SpeechTimeLeft > 0.f && !SpeechText.IsEmpty(); }
	const FString& GetSpeechText() const { return SpeechText; }

	/** Sichtbarkeit der Schilder nach Entfernung (vom Manager gesetzt). */
	void SetLabelRange(bool bInNameVisible, bool bInSpeechVisible);

	/** Gehgeschwindigkeit (cm/s). */
	void SetWalkSpeed(float Speed);

	// IMTAInteractable
	virtual FMTAInteractionInfo GetInteractionInfo_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;

	// IMTANPCActor
	virtual FName GetNPCId_Implementation() const override;
	virtual void ApplyAppearance_Implementation(FName ArchetypeId) override;
	virtual void OnActivityChanged_Implementation(const FMTAScheduleState& NewState) override;
	virtual void PlayBark_Implementation(const FString& Text, FName Category) override;
	virtual void PlayInteraction_Implementation(const FMTANPCInteractionEvent& Event) override;
	virtual void OnConversationStateChanged_Implementation(bool bInConversation) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "MTA6|NPC")
	TObjectPtr<UTextRenderComponent> NameLabel;

	UPROPERTY(VisibleAnywhere, Category = "MTA6|NPC")
	TObjectPtr<UTextRenderComponent> ActivityLabel;

	UPROPERTY(VisibleAnywhere, Category = "MTA6|NPC")
	TObjectPtr<UTextRenderComponent> SpeechLabel;

private:
	void UpdateLabelVisibility();
	static FString WrapSpeech(const FString& Text, int32 MaxLineChars);

	TWeakObjectPtr<AMTALivingWorldManager> LivingManager;
	FString ActivityText;
	FString SpeechText;
	float SpeechTimeLeft = 0.f;
	bool bNameInRange = false;
	bool bSpeechInRange = false;
};
