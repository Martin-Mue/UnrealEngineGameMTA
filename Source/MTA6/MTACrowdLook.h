#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MTAAppearance.h"
#include "MTACrowdLook.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * Layout der Per-Instance-Custom-Data fuer Crowd-Figuren (Index = Custom-Data-Slot im Material).
 * Im Material per PerInstanceCustomData (Index) lesen; RGB-Tripel belegen drei aufeinanderfolgende Slots.
 */
namespace MTACrowdData
{
	constexpr int32 SkinTone = 0;     // 0..1 (Position auf der Hautton-Palette)
	constexpr int32 HeightScale = 1;  // Faktor auf Referenzgroesse
	constexpr int32 BuildScale = 2;   // Breitenfaktor
	constexpr int32 TopTint = 3;      // R, G, B (linear)
	constexpr int32 BottomTint = 6;   // R, G, B
	constexpr int32 HairTint = 9;     // R, G, B (inkl. Ergrauung)
	constexpr int32 MeshVariant = 12; // Index der Mesh-Variante (fuer Material-Switch)
	constexpr int32 AccentTint = 13;  // R, G, B (Jacke/Tasche/Neon-Akzent)
	constexpr int32 Num = 16;         // 16 Floats pro Instanz (Rest 0)
}

/** Ergebnis der Crowd-Erzeugung fuer eine Fussgaenger-Instanz. */
USTRUCT(BlueprintType)
struct MTA6_API FMTACrowdLook
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Crowd") int64 SeedId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Crowd") FName ArchetypeId;
	UPROPERTY(BlueprintReadOnly, Category = "Crowd") FMTAAppearance Appearance;
	/** Gruppe in appearance.json "crowdMeshGroups" (z. B. adult_fem). */
	UPROPERTY(BlueprintReadOnly, Category = "Crowd") FName MeshGroup;
	/** Index der gewaehlten Mesh-Variante in der Gruppe; -1 = keine Assets, Platzhalter benutzen. */
	UPROPERTY(BlueprintReadOnly, Category = "Crowd") int32 MeshVariant = -1;
	UPROPERTY(BlueprintReadOnly, Category = "Crowd") float HeightScale = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Crowd") float WidthScale = 1.f;
	/** Fertige Custom-Data (MTACrowdData::Num Floats). */
	UPROPERTY(BlueprintReadOnly, Category = "Crowd") TArray<float> CustomData;
	/** Auffaelligkeit fuer Aegis (Zeugen/Scan-Prioritaet). */
	UPROPERTY(BlueprintReadOnly, Category = "Crowd") float Conspicuousness = 0.f;
};

/**
 * Crowd-Variation: erzeugt aus einer Seed-ID deterministisch ein Aussehen und fuellt
 * Per-Instance-Custom-Data fuer (Hierarchical) Instanced Static Meshes. Laeuft ohne Assets mit
 * einem Zylinder-Platzhalter (/Engine/BasicShapes/Cylinder).
 */
UCLASS()
class MTA6_API UMTACrowdLookSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 * Deterministisch: gleiche SeedId + gleicher Kontext = gleiches Ergebnis.
	 * Fuer eine stabile Identitaet (Passant 4711 bleibt derselbe) den Kontext beim Spawn speichern
	 * oder GenerateStableLook verwenden.
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Crowd")
	FMTACrowdLook GenerateLook(int64 SeedId, const FMTALookContext& Context) const;

	/** Wie GenerateLook, aber unabhaengig von Tageszeit und Wetter (nur Bezirk): Identitaet bleibt ueber den Tag gleich. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Crowd")
	FMTACrowdLook GenerateStableLook(int64 SeedId, EMTADistrict District) const;

	/** Fuellt die Custom-Data aus einem Aussehen. */
	static void FillCustomData(const FMTAAppearance& Appearance, float HeightScale, float WidthScale, int32 MeshVariant, TArray<float>& OutData);

	/** Der eingebaute Platzhalter (Zylinder, 100x100x100 cm, Pivot Mitte). Kann nullptr liefern, falls die Engine-Basisform fehlt. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Crowd")
	UStaticMesh* GetPlaceholderMesh() const;

	/** Stellt Custom-Data-Anzahl ein und setzt (falls kein Mesh gesetzt) den Platzhalter. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Crowd")
	void ConfigureInstancedComponent(UInstancedStaticMeshComponent* Component, bool bUsePlaceholderIfEmpty = true) const;

	/**
	 * Transform fuer eine Instanz. Mit bPlaceholder skaliert der 100-cm-Zylinder auf Koerpergroesse/-breite
	 * und wird um die halbe Hoehe angehoben (Fusspunkt = Location). Sonst nur Rotation/Hoehenskalierung (Mesh-Referenz 175 cm).
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Crowd")
	FTransform MakeInstanceTransform(const FMTACrowdLook& Look, const FVector& FootLocation, float YawDegrees, bool bPlaceholder) const;

	/** Fuegt eine Instanz samt Custom-Data hinzu. Gibt den Instanzindex zurueck (-1 bei Fehler). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Crowd")
	int32 AddCrowdInstance(UInstancedStaticMeshComponent* Component, const FMTACrowdLook& Look, const FVector& FootLocation, float YawDegrees, bool bPlaceholder = true) const;

	/** Aktualisiert Custom-Data einer bestehenden Instanz (z. B. nach Tageswechsel). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Crowd")
	bool UpdateCrowdInstanceLook(UInstancedStaticMeshComponent* Component, int32 InstanceIndex, const FMTACrowdLook& Look) const;

	/** Mesh-Pfad der gewaehlten Variante (leer = Platzhalter). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Crowd")
	FSoftObjectPath GetMeshPathForLook(const FMTACrowdLook& Look) const;

	/** Referenzgroesse der Crowd-Meshes in cm (Skalierung relativ dazu). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Crowd")
	float ReferenceHeightCm = 175.f;

	/** Durchmesser des Platzhalter-Koerpers bei Build 0.5 (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Crowd")
	float PlaceholderBaseWidthCm = 45.f;

private:
	static int32 HashSeed(int64 SeedId, int32 Salt);
	FMTACrowdLook BuildLook(int64 SeedId, const FMTALookContext& Context) const;
};
