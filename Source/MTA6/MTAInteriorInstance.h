#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MTAInteriorTypes.h"
#include "MTAInteriorBuilder.h"
#include "MTAInteriorInstance.generated.h"

class UInstancedStaticMeshComponent;
class ULocalLightComponent;
class UStaticMesh;
class UMaterialInterface;
class UMTAShopComponent;
class AMTAInteriorInteractive;
class AMTABuildingEntry;
struct FMTAInteriorTemplate;

/**
 * Eine gebaute Innenraum-Zelle im Interior Grid. Wird vom UMTAInteriorSubsystem bei Bedarf
 * gespawnt, zwischen Besuchen "schlafend" (versteckt, ohne Kollision und Licht) gehalten und
 * nach Streaming-Radius/Speicherbudget wieder zerstoert.
 *
 * Materialien: als Blueprint ableiten und Materialien in den Class Defaults setzen, dann den
 * BP per Config (UMTAInteriorSubsystem::InstanceClass) eintragen. Das Primitiv-Material liest
 * PerInstanceCustomData 0..2 = Farbe (linear), 3 = Emissive-Staerke.
 */
UCLASS(Blueprintable)
class MTA6_API AMTAInteriorInstance : public AActor
{
	GENERATED_BODY()

public:
	AMTAInteriorInstance();

	virtual void Tick(float DeltaSeconds) override;

	// --- Platzhalter-Assets (ueberschreibbar) ---------------------------------------
	/** Index = EMTAPrimShape (Cube, Cylinder, Sphere, Cone, Plane); Standard: /Engine/BasicShapes. */
	UPROPERTY(EditDefaultsOnly, Category = "Interior|Assets")
	TArray<TObjectPtr<UStaticMesh>> ShapeMeshes;

	/** Waende, Moebel (liest PerInstanceCustomData 0..3). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interior|Assets")
	TObjectPtr<UMaterialInterface> PrimMaterial;

	/** Fensterglas (transluzent). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interior|Assets")
	TObjectPtr<UMaterialInterface> GlassMaterial;

	/** Fensterkulisse (unlit/emissive, Farbe per CustomData). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interior|Assets")
	TObjectPtr<UMaterialInterface> BackdropMaterial;

	/** Material interaktiver Objekte (liest CustomPrimitiveData 0..3). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interior|Assets")
	TObjectPtr<UMaterialInterface> InteractiveMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Interior|Assets")
	TSubclassOf<AMTAInteriorInteractive> InteractiveClass;

	// --- Licht ------------------------------------------------------------------------
	/** Fensterlicht-Staerke bei voller Mittagssonne (Unitless). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Interior|Light")
	float WindowDaylightIntensity = 9.f;

	/** Emissive-Staerke der Fensterkulisse am Tag. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Interior|Light")
	float BackdropDayEmissive = 3.f;

	// --- Laufzeitdaten ----------------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "Interior") FName EntryKey;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") FName TemplateId;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") FName RoomType;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") FString DisplayLabel;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") EMTADistrict District = EMTADistrict::Core;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") int32 GridLayer = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") float CostKB = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") double LastUsedTime = 0.0;
	/** Weltposition des Aussengebaeudes (fuer Streaming-Radius und Rueckkehr-Fallback). */
	UPROPERTY(BlueprintReadOnly, Category = "Interior") FVector ExteriorAnchor = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") TWeakObjectPtr<AMTABuildingEntry> SourceEntry;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") TObjectPtr<UMTAShopComponent> Shop;

	/** Baut alle Komponenten aus dem Rechenergebnis (einmalig nach dem Spawn). */
	void BuildFrom(const FMTAInteriorBuildResult& Result, const FMTAInteriorTemplate& Template, FName InEntryKey,
		EMTADistrict InDistrict, FName DefaultRole);

	/** Fensterlicht/Kulisse passend zur Tageszeit (0..24) und Wetter (0 = klar, 1 = sehr trueb). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	void ApplyDaylight(float Hour, float WeatherDim);

	/** Schlafend = unsichtbar, ohne Kollision, Lichter aus (Instanz bleibt fuer schnelle Rueckkehr). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	void SetDormant(bool bInDormant);

	UFUNCTION(BlueprintPure, Category = "MTA6|Interior")
	bool IsDormant() const { return bDormant; }

	/** Ankunftspunkt (Welt) fuer eine Eingangsart; Fallback: Vordertuer, dann beliebig. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	bool GetEntranceSpawn(EMTAEntranceKind Kind, FTransform& OutWorld) const;

	/** NPC-Slots in Weltkoordinaten (fuer den NPC-Agenten). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	TArray<FMTANPCSlot> GetNPCSlots() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	void SetRoomLights(int32 RoomIndex, bool bOn);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	void ToggleRoomLights(int32 RoomIndex);

	/** Wirkung eines Terminals/Schalters: Geheimnis aufdecken, Tuer entriegeln, Lichter aus. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	void ActivateLink(FName LinkId);

	UFUNCTION(BlueprintPure, Category = "MTA6|Interior")
	FBox GetWorldBounds() const { return LocalBounds.TransformBy(GetActorTransform()); }

	const TArray<FMTABuiltRoom>& GetRooms() const { return Rooms; }
	const TArray<TObjectPtr<AMTAInteriorInteractive>>& GetInteractives() const { return Interactives; }
	/** Globale Geheimnis-ID (Vorlagen-ID@Gebaeude), damit wiederverwendete Vorlagen nicht kollidieren. */
	FName MakeGlobalSecretId(FName LocalId) const;

private:
	UPROPERTY(VisibleAnywhere, Category = "Interior")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> PrimComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ULocalLightComponent>> LightComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMTAInteriorInteractive>> Interactives;

	/** Parallel zu LightComponents. */
	struct FLightInfo
	{
		float BaseIntensity = 0.f;
		int32 RoomIndex = INDEX_NONE;
		bool bFlicker = false;
		bool bWindow = false;
	};
	TArray<FLightInfo> LightInfos;
	TArray<bool> RoomLightsOn;
	TArray<FMTABuiltRoom> Rooms;
	TArray<FMTANPCSlot> LocalNpcSlots;
	TMap<EMTAEntranceKind, FTransform> LocalEntranceSpawns;
	FBox LocalBounds = FBox(ForceInit);
	/** Indizes der Kulissen-Instanzen in der Backdrop-ISM. */
	int32 BackdropComponentIndex = INDEX_NONE;
	bool bDormant = false;
	float FlickerTimer = 0.f;

	UInstancedStaticMeshComponent* GetOrCreatePrimComponent(EMTAPrimShape Shape, EMTAPrimLayer Layer);
	void UpdateLightVisibility();
};
