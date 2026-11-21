#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MTAInteractionInterface.h"
#include "MTADialogueNPC.generated.h"

class UTextRenderComponent;

/**
 * Ansprechbarer Schluessel-NPC der Stadt (Mannequin als Platzhalter-Figur).
 * Taste E (Interaktion) startet das Gespraech ueber UMTADialogueSubsystem::StartConversation(NPCId).
 * Die Daten (Name, Dialog) kommen aus Content/Data/npcs.json und dialogues/*.json.
 */
UCLASS()
class MTA6_API AMTADialogueNPC : public ACharacter, public IMTAInteractable
{
	GENERATED_BODY()

public:
	AMTADialogueNPC();

	virtual void Tick(float DeltaSeconds) override;

	/** Kennung aus npcs.json (z. B. "brann"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|NPC")
	FName NPCId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|NPC")
	FString DisplayName;

	/** Setzt Kennung und Namensschild. */
	void SetupNPC(FName InNPCId, const FString& InDisplayName);

	// IMTAInteractable
	virtual FMTAInteractionInfo GetInteractionInfo_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "MTA6|NPC")
	TObjectPtr<UTextRenderComponent> NameLabel;
};
