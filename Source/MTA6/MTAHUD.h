#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MTADialogueSubsystem.h"
#include "MTAHUD.generated.h"

class UFont;
class AMTA6GameMode;
struct FCanvasUVTri;

/**
 * Canvas-HUD (Port von _build_hud/_process aus godot/main.gd und godot/minimap.gd).
 * Zeichnet Statusblock, Toast, Dialog, WRECKED/FESTGENOMMEN und eine rotierende Minimap.
 * Nur ASCII-Sonderzeichen, damit der Standard-Font alles darstellen kann.
 */
UCLASS(Config = Game)
class MTA6_API AMTAHUD : public AHUD
{
	GENERATED_BODY()

public:
	AMTAHUD();

	virtual void DrawHUD() override;

	/** Optionaler Font; leer = GEngine->GetLargeFont(). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|HUD")
	TObjectPtr<UFont> HudFont;

	/** Referenzhoehe fuer die Skalierung (bei dieser Hoehe ist der Faktor 1; kleiner = groessere Schrift). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|HUD")
	float ReferenceHeight = 640.f;

	/** Kantenlaenge der Minimap in Pixeln (bei Referenzhoehe). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|HUD|Minimap")
	float MinimapSize = 210.f;

	/** Sichtradius der Minimap in cm (Godot: 150 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|HUD|Minimap")
	float MinimapWorldRadius = 15000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|HUD|Minimap")
	bool bShowTrafficOnMinimap = true;

	/** Fraktionen in der Statuszeile: Story-Id und Anzeigename. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|HUD")
	TArray<FString> FactionIds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|HUD")
	TArray<FString> FactionNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|HUD")
	bool bShowHelpLine = true;

private:
	// --- Einfuehrung, Kodex, Auftragsbriefing ----------------------------------------------------
	void UpdateStoryPageInput(AMTA6GameMode* GM);
	void UpdateInputLock(AMTA6GameMode* GM);
	void DrawStoryPage(AMTA6GameMode* GM);

	// --- Auftragsliste (Tab) und Zielanzeige ------------------------------------------------------
	void UpdateMissionListInput(AMTA6GameMode* GM);
	void DrawMissionList(AMTA6GameMode* GM);
	void DrawMissionObjective(AMTA6GameMode* GM, float& Y);

	// --- NPC-Gespraeche und Interaktions-Prompt -------------------------------------------------
	void DrawInteractionPrompt();
	class AActor* FindNearbyNPC() const;
	void DrawConversation();
	void UpdateConversationInput();

	UFUNCTION()
	void OnDialogueViewChanged(const FMTADialogueView& View);

	FMTADialogueView DialogueView;
	bool bDialogueBound = false;
	bool bMoveLocked = false;

	void DrawStatus(AMTA6GameMode* GM);
	void DrawToastAndOverlays(AMTA6GameMode* GM);
	void DrawDialog(AMTA6GameMode* GM);
	void DrawMinimap(AMTA6GameMode* GM);

	/** Text mit Schatten; liefert die Hoehe der Zeile. */
	float DrawShadowText(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale);
	void DrawCenteredText(const FString& Text, float Y, const FLinearColor& Color, float Scale);
	void WrapText(const FString& Text, float MaxWidth, float Scale, TArray<FString>& OutLines);
	UFont* GetHudFont() const;

	/** Polygon an einem Rechteck abschneiden und als Dreiecksfaecher anhaengen. */
	static void AddClippedPolygon(const TArray<FVector2D>& Poly, const FVector2D& Min, const FVector2D& Max,
		const FLinearColor& Color, TArray<FCanvasUVTri>& OutTris);
	static void AddTriangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color,
		TArray<FCanvasUVTri>& OutTris);
	static void AddCircle(const FVector2D& Center, float Radius, const FLinearColor& Color, TArray<FCanvasUVTri>& OutTris);

	float UIScale = 1.f;
};
