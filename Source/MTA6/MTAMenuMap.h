#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "MTAMenuMap.generated.h"

class SWidgetSwitcher;
class SVerticalBox;

/** Kartenmarker-Typ (bestimmt Form und Farbe). */
enum class EMTAMapMarkerType : uint8 { Mission, Landmark, Garage, Safehouse, Shop, Police, Custom };

enum class EMTAMissionState : uint8 { Available, Active, Completed, Locked };

/** Marker mit normierter Position: U = West(0)..Ost(1), V = Sued(0)..Nord(1). */
struct FMTAMapMarker
{
	FName Id;
	EMTAMapMarkerType Type = EMTAMapMarkerType::Custom;
	FVector2D Pos = FVector2D::ZeroVector;
	FString Label;
	FLinearColor Color = FLinearColor::White;
};

struct FMTAMissionEntry
{
	FName Id;
	FString Title;
	FString Description;
	EMTAMissionState State = EMTAMissionState::Available;
	bool bMain = false;
	/** Bezirks-Schluessel (core, saltmarket, lanternhill, greenmoor, rustline, sunstrip) oder leer. */
	FName District;
	FString Reward;
	bool bHasPos = false;
	FVector2D Pos = FVector2D::ZeroVector;
};

struct FMTAFactionEntry
{
	FName Id;
	FString Name;
	/** Stand -100..100. */
	int32 Standing = 0;
	FLinearColor Color = FLinearColor::White;
	FString Rank;
};

struct FMTAStatEntry
{
	FString Label;
	FString Value;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMTAMapDataProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Neutrale Datenschnittstelle fuer Karte und Journal. Der Hauptagent implementiert sie (oder
 * nutzt UMTAMapDataStore) und liefert Daten aus Story, Missionen, Fraktionen, Statistik.
 * Die Menues kennen keine fremden Klassen.
 */
class MTA6_API IMTAMapDataProvider
{
	GENERATED_BODY()

public:
	virtual void GetMapMarkers(TArray<FMTAMapMarker>& Out) const = 0;
	virtual void GetMapMissions(TArray<FMTAMissionEntry>& Out) const = 0;
	virtual void GetMapFactions(TArray<FMTAFactionEntry>& Out) const = 0;
	virtual void GetMapStats(TArray<FMTAStatEntry>& Out) const = 0;
	/** Spielerposition (normiert) und Blickrichtung in Grad (0 = Norden, im Uhrzeigersinn). */
	virtual bool GetPlayerPose(FVector2D& OutPos01, float& OutYawDeg) const = 0;
	virtual bool GetWaypoint(FVector2D& OutPos01) const = 0;
	virtual void SetWaypoint(const FVector2D& Pos01) = 0;
	virtual void ClearWaypoint() = 0;
};

/** Wird ausgeloest, wenn der Spieler einen Wegpunkt setzt/loescht (Pos normiert, bSet=false: geloescht). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FMTAWaypointChanged, bool /*bSet*/, FVector2D /*Pos01*/);

/**
 * Einfache Standardimplementierung: haelt alle Daten im Speicher. Der Hauptagent fuellt sie
 * (SetMissions, SetFactions, ...) und aktualisiert die Spielerpose pro Tick mit SetPlayerWorld.
 */
UCLASS()
class MTA6_API UMTAMapDataStore : public UObject, public IMTAMapDataProvider
{
	GENERATED_BODY()

public:
	TArray<FMTAMapMarker> Markers;
	TArray<FMTAMissionEntry> Missions;
	TArray<FMTAFactionEntry> Factions;
	TArray<FMTAStatEntry> Stats;

	/** Weltposition (cm, X/Y) und Gier-Winkel der Spielfigur setzen; rechnet auf 0..1 um. */
	void SetPlayerWorld(const FVector2D& WorldXY, float WorldYawDeg);

	/** Normierte Koordinaten aus Weltkoordinaten (Stadtraster MTAGrid, Weltkoordinaten in cm). */
	static FVector2D WorldToNormalized(const FVector2D& WorldXY);
	static FVector2D NormalizedToWorld(const FVector2D& Pos01);

	FMTAWaypointChanged OnWaypointChanged;

	virtual void GetMapMarkers(TArray<FMTAMapMarker>& Out) const override { Out = Markers; }
	virtual void GetMapMissions(TArray<FMTAMissionEntry>& Out) const override { Out = Missions; }
	virtual void GetMapFactions(TArray<FMTAFactionEntry>& Out) const override { Out = Factions; }
	virtual void GetMapStats(TArray<FMTAStatEntry>& Out) const override { Out = Stats; }
	virtual bool GetPlayerPose(FVector2D& OutPos01, float& OutYawDeg) const override;
	virtual bool GetWaypoint(FVector2D& OutPos01) const override;
	virtual void SetWaypoint(const FVector2D& Pos01) override;
	virtual void ClearWaypoint() override;

private:
	bool bHasPose = false;
	FVector2D PlayerPos = FVector2D(0.5, 0.5);
	float PlayerYaw = 0.f;
	bool bHasWaypoint = false;
	FVector2D Waypoint = FVector2D::ZeroVector;
};

/** Grosse Karte: zeichnet Bezirke, Namen, Marker, Spieler und Wegpunkt per FSlateDrawElement. */
class MTA6_API SMTAMapWidget : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMTAMapWidget) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, Provider)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(720.0, 560.0); }
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/** Zentriert die Karte auf eine normierte Position. */
	void CenterOn(const FVector2D& Pos01) { Center = Pos01; }

private:
	IMTAMapDataProvider* GetProvider() const;
	FVector2D LocalToMap(const FGeometry& G, const FVector2D& Local) const;
	FVector2D MapToLocal(const FGeometry& G, const FVector2D& Pos01) const;
	float MapSide(const FGeometry& G) const;

	TWeakObjectPtr<UObject> ProviderObj;
	FVector2D Center = FVector2D(0.5, 0.5);
	float Zoom = 1.f;
	bool bDragging = false;
	float DragDistance = 0.f;

	/** Bezirksmitten (normiert) fuer die Namensanzeige, einmal berechnet. */
	TArray<FVector2D> DistrictCenters;
};

/** Journal mit Reitern: Karte, Missionen (Filter), Fraktionen, Statistik. */
class MTA6_API SMTAJournal : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMTAJournal) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, Provider)
		SLATE_ARGUMENT(int32, InitialTab)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	IMTAMapDataProvider* GetProvider() const;
	void RebuildMissions();
	void RebuildFactions();
	void RebuildStats();
	TSharedRef<SWidget> MakeTabButton(int32 Index, const FString& LabelKey);

	TWeakObjectPtr<UObject> ProviderObj;
	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SVerticalBox> MissionList;
	TSharedPtr<SVerticalBox> FactionList;
	TSharedPtr<SVerticalBox> StatList;
	TSharedPtr<SMTAMapWidget> Map;
	int32 TabIndex = 0;
	/** 0 Alle, 1 Haupt, 2 Aktiv, 3 Erledigt. */
	int32 MissionFilter = 0;
	FName SelectedMission;
};
