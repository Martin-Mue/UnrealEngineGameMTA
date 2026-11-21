#include "MTAMenuMap.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "MTADistrict.h"
#include "MTALocalizationSubsystem.h"
#include "MTAMenuStyle.h"
#include "MTAMinimapTypes.h"

namespace
{
	const TCHAR* const GDistrictKeys[] = { TEXT("core"), TEXT("saltmarket"), TEXT("lanternhill"), TEXT("greenmoor"), TEXT("rustline"), TEXT("sunstrip") };
	constexpr int32 GGrid = 40; // Zellen je Achse fuer die Bezirksfaerbung
}

// ----------------------------------------------------------------------------- Datenspeicher

FVector2D UMTAMapDataStore::WorldToNormalized(const FVector2D& WorldXY)
{
	const double Size = MTAGrid::Half * 2.0;
	// U (West->Ost) = Unreal +Y, V (Sued->Nord) = Unreal +X.
	return FVector2D(FMath::Clamp((WorldXY.Y + MTAGrid::Half) / Size, 0.0, 1.0), FMath::Clamp((WorldXY.X + MTAGrid::Half) / Size, 0.0, 1.0));
}

FVector2D UMTAMapDataStore::NormalizedToWorld(const FVector2D& Pos01)
{
	const double Size = MTAGrid::Half * 2.0;
	return FVector2D(Pos01.Y * Size - MTAGrid::Half, Pos01.X * Size - MTAGrid::Half);
}

void UMTAMapDataStore::SetPlayerWorld(const FVector2D& WorldXY, float WorldYawDeg)
{
	PlayerPos = WorldToNormalized(WorldXY);
	// Unreal-Yaw 0 = +X = Norden; positiv im Uhrzeigersinn von oben gesehen -> passt zur Kartenanzeige.
	PlayerYaw = WorldYawDeg;
	bHasPose = true;
}

bool UMTAMapDataStore::GetPlayerPose(FVector2D& OutPos01, float& OutYawDeg) const
{
	OutPos01 = PlayerPos; OutYawDeg = PlayerYaw; return bHasPose;
}

bool UMTAMapDataStore::GetWaypoint(FVector2D& OutPos01) const
{
	OutPos01 = Waypoint; return bHasWaypoint;
}

void UMTAMapDataStore::SetWaypoint(const FVector2D& Pos01)
{
	bHasWaypoint = true; Waypoint = Pos01; OnWaypointChanged.Broadcast(true, Pos01);
}

void UMTAMapDataStore::ClearWaypoint()
{
	bHasWaypoint = false; OnWaypointChanged.Broadcast(false, FVector2D::ZeroVector);
}

// ---------------------------------------------------------------------------------- Karte

void SMTAMapWidget::Construct(const FArguments& InArgs)
{
	ProviderObj = InArgs._Provider;

	// Bezirksmitten aus dem Raster bestimmen (Schwerpunkt aller Zellen eines Bezirks).
	const int32 N = MTADistrict::Num();
	TArray<FVector2D> Sum; Sum.Init(FVector2D::ZeroVector, N);
	TArray<int32> Count; Count.Init(0, N);
	for (int32 y = 0; y < GGrid; ++y)
	{
		for (int32 x = 0; x < GGrid; ++x)
		{
			const float U = (x + 0.5f) / GGrid, V = (y + 0.5f) / GGrid;
			const int32 D = static_cast<int32>(MTADistrict::ClassifyNormalized(U, V));
			Sum[D] += FVector2D(U, V); Count[D]++;
		}
	}
	DistrictCenters.Init(FVector2D(0.5, 0.5), N);
	for (int32 i = 0; i < N; ++i)
	{
		if (Count[i] > 0) { DistrictCenters[i] = Sum[i] / Count[i]; }
	}
}

IMTAMapDataProvider* SMTAMapWidget::GetProvider() const
{
	return Cast<IMTAMapDataProvider>(ProviderObj.Get());
}

float SMTAMapWidget::MapSide(const FGeometry& G) const
{
	const FVector2D S = G.GetLocalSize();
	return FMath::Min(S.X, S.Y) * Zoom;
}

FVector2D SMTAMapWidget::MapToLocal(const FGeometry& G, const FVector2D& P) const
{
	const double Side = MapSide(G);
	return G.GetLocalSize() * 0.5 + FVector2D((P.X - Center.X) * Side, -(P.Y - Center.Y) * Side);
}

FVector2D SMTAMapWidget::LocalToMap(const FGeometry& G, const FVector2D& L) const
{
	const double Side = MapSide(G);
	const FVector2D D = L - G.GetLocalSize() * 0.5;
	return FVector2D(Center.X + D.X / Side, Center.Y - D.Y / Side);
}

int32 SMTAMapWidget::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FMTAUiPalette Pal = MTAUI::Pal();
	const UMTAAccessibilitySubsystem* Acc = UMTAAccessibilitySubsystem::GetActive();
	auto Tint = [Acc](const FLinearColor& C) { return Acc ? Acc->TransformColor(C) : C; };
	const FSlateBrush* White = MTAUI::White();

	auto Box = [&](int32 Layer, const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Col)
	{
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos))), White, ESlateDrawEffect::None, Col);
	};
	auto Text = [&](int32 Layer, const FVector2D& Pos, const FString& Str, float Size, const FLinearColor& Col)
	{
		FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2f(400.f, 40.f), FSlateLayoutTransform(FVector2f(Pos))), Str, MTAUI::Font(Size, true), ESlateDrawEffect::None, Col);
	};
	auto TextCentered = [&](int32 Layer, const FVector2D& Pos, const FString& Str, float Size, const FLinearColor& Col)
	{
		const FVector2D Ext = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Str, MTAUI::Font(Size, true));
		Text(Layer, Pos - Ext * 0.5, Str, Size, Col);
	};

	Out.PushClip(FSlateClippingZone(G));
	int32 Layer = LayerId;
	Box(Layer, FVector2D::ZeroVector, G.GetLocalSize(), FLinearColor(0.01f, 0.02f, 0.035f, 1.f)); // Meer/Hintergrund

	// Bezirke als Raster; West = Meer (Konzept 8.1) wird durch Dunkelblau gekennzeichnet.
	++Layer;
	const double Cell = MapSide(G) / GGrid;
	for (int32 y = 0; y < GGrid; ++y)
	{
		for (int32 x = 0; x < GGrid; ++x)
		{
			const float U = (x + 0.5f) / GGrid, V = (y + 0.5f) / GGrid;
			const EMTADistrict D = MTADistrict::ClassifyNormalized(U, V);
			FLinearColor C = MTADistrict::MakeDefaultProfile(D).LeadColor;
			// Abwechselnd leicht abdunkeln: Schachbrett macht Zoom/Pan sichtbar, ohne Textur.
			C = C * (((x + y) & 1) ? 0.30f : 0.36f);
			C.A = 1.f;
			const FVector2D P0 = MapToLocal(G, FVector2D(x / double(GGrid), (y + 1) / double(GGrid)));
			Box(Layer, P0, FVector2D(Cell + 0.5, Cell + 0.5), Tint(C));
		}
	}

	// Bezirksnamen
	++Layer;
	for (int32 i = 0; i < MTADistrict::Num(); ++i)
	{
		const FString Name = MTATr(FString::Printf(TEXT("district.%s"), GDistrictKeys[i])).ToString().ToUpper();
		TextCentered(Layer, MapToLocal(G, DistrictCenters[i]), Name, 15.f, FLinearColor(Pal.Text.R, Pal.Text.G, Pal.Text.B, 0.8f));
	}

	// Marker
	++Layer;
	if (const IMTAMapDataProvider* Prov = GetProvider())
	{
		TArray<FMTAMapMarker> Markers;
		Prov->GetMapMarkers(Markers);
		TArray<FMTAMissionEntry> Missions;
		Prov->GetMapMissions(Missions);
		for (const FMTAMissionEntry& M : Missions)
		{
			if (M.bHasPos && (M.State == EMTAMissionState::Active || M.State == EMTAMissionState::Available))
			{
				FMTAMapMarker Mk; Mk.Type = EMTAMapMarkerType::Mission; Mk.Pos = M.Pos; Mk.Label = M.Title;
				Mk.Color = (M.State == EMTAMissionState::Active) ? Pal.Accent : Pal.Good;
				Markers.Add(Mk);
			}
		}
		for (const FMTAMapMarker& Mk : Markers)
		{
			const FVector2D P = MapToLocal(G, Mk.Pos);
			// Formen statt nur Farben (Barrierefreiheit): Mission = Raute (gedreht), sonst Quadrat/Strich.
			const float S = (Mk.Type == EMTAMapMarkerType::Mission) ? 12.f : 9.f;
			Box(Layer, P - FVector2D(S, S) * 0.5, FVector2D(S, S), FLinearColor::Black);
			Box(Layer + 1, P - FVector2D(S - 3.f, S - 3.f) * 0.5, FVector2D(S - 3.f, S - 3.f), Tint(Mk.Color));
			if (Zoom >= 2.f && !Mk.Label.IsEmpty())
			{
				Text(Layer + 1, P + FVector2D(10.0, -8.0), Mk.Label, 13.f, Pal.Text);
			}
		}

		// Wegpunkt: Kreuz aus zwei Linien plus Ring aus Quadrat
		FVector2D Wp;
		if (Prov->GetWaypoint(Wp))
		{
			const FVector2D P = MapToLocal(G, Wp);
			TArray<FVector2f> L1 = { FVector2f(P + FVector2D(-9, -9)), FVector2f(P + FVector2D(9, 9)) };
			TArray<FVector2f> L2 = { FVector2f(P + FVector2D(-9, 9)), FVector2f(P + FVector2D(9, -9)) };
			FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), L1, ESlateDrawEffect::None, Pal.Warning, true, 3.f);
			FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), L2, ESlateDrawEffect::None, Pal.Warning, true, 3.f);
			Text(Layer + 2, P + FVector2D(12.0, -8.0), MTATr(TEXT("map.waypoint")).ToString(), 13.f, Pal.Warning);
		}

		// Spieler: Pfeil aus drei Linien in Blickrichtung
		FVector2D Pp; float Yaw = 0.f;
		if (Prov->GetPlayerPose(Pp, Yaw))
		{
			const FVector2D P = MapToLocal(G, Pp);
			const double R = FMath::DegreesToRadians(Yaw);
			const FVector2D Dir(FMath::Sin(R), -FMath::Cos(R)); // 0 Grad = Norden = oben
			const FVector2D Side(-Dir.Y, Dir.X);
			TArray<FVector2f> Arrow = { FVector2f(P - Dir * 8 - Side * 6), FVector2f(P + Dir * 10), FVector2f(P - Dir * 8 + Side * 6), FVector2f(P - Dir * 3), FVector2f(P - Dir * 8 - Side * 6) };
			FSlateDrawElement::MakeLines(Out, Layer + 3, G.ToPaintGeometry(), Arrow, ESlateDrawEffect::None, Tint(FLinearColor::White), true, 3.f);
		}
	}

	// Legende und Bedienhinweis
	Text(Layer + 4, FVector2D(10.0, G.GetLocalSize().Y - 26.0), MTATr(TEXT("map.hint")).ToString(), 13.f, Pal.TextDim);
	Out.PopClip();
	return Layer + 5;
}

FReply SMTAMapWidget::OnMouseWheel(const FGeometry& G, const FPointerEvent& E)
{
	const FVector2D Local = G.AbsoluteToLocal(E.GetScreenSpacePosition());
	const FVector2D Before = LocalToMap(G, Local);
	Zoom = FMath::Clamp(Zoom * (E.GetWheelDelta() > 0.f ? 1.2f : 1.f / 1.2f), 1.f, 8.f);
	const FVector2D After = LocalToMap(G, Local);
	Center += Before - After; // Zoom auf den Mauszeiger
	Center.X = FMath::Clamp(Center.X, 0.0, 1.0); Center.Y = FMath::Clamp(Center.Y, 0.0, 1.0);
	return FReply::Handled();
}

FReply SMTAMapWidget::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = true; DragDistance = 0.f;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	if (E.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (IMTAMapDataProvider* P = GetProvider()) { P->ClearWaypoint(); }
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SMTAMapWidget::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() == EKeys::LeftMouseButton && bDragging)
	{
		bDragging = false;
		if (DragDistance < 5.f) // kurzer Klick ohne Ziehen: Wegpunkt setzen
		{
			if (IMTAMapDataProvider* P = GetProvider())
			{
				const FVector2D M = LocalToMap(G, G.AbsoluteToLocal(E.GetScreenSpacePosition()));
				if (M.X >= 0.0 && M.X <= 1.0 && M.Y >= 0.0 && M.Y <= 1.0) { P->SetWaypoint(M); }
			}
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SMTAMapWidget::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	if (bDragging)
	{
		const FVector2D D = E.GetCursorDelta();
		DragDistance += D.Size();
		const double Side = MapSide(G);
		Center.X = FMath::Clamp(Center.X - D.X / Side, 0.0, 1.0);
		Center.Y = FMath::Clamp(Center.Y + D.Y / Side, 0.0, 1.0);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// -------------------------------------------------------------------------------- Journal

IMTAMapDataProvider* SMTAJournal::GetProvider() const
{
	return Cast<IMTAMapDataProvider>(ProviderObj.Get());
}

TSharedRef<SWidget> SMTAJournal::MakeTabButton(int32 Index, const FString& LabelKey)
{
	const FMTAUiPalette P = MTAUI::Pal();
	return SNew(SButton)
		.ButtonColorAndOpacity_Lambda([this, Index, P]() { return FSlateColor(TabIndex == Index ? P.ButtonSelected : P.Button); })
		.ContentPadding(FMargin(18.f, 8.f))
		.OnClicked_Lambda([this, Index]() { TabIndex = Index; Switcher->SetActiveWidgetIndex(Index); return FReply::Handled(); })
		[ SNew(STextBlock).Text(MTAUI::TrAttr(LabelKey)).Font(MTAUI::Font(17.f, true)).ColorAndOpacity(FSlateColor(P.Text)) ];
}

void SMTAJournal::Construct(const FArguments& InArgs)
{
	ProviderObj = InArgs._Provider;
	TabIndex = FMath::Clamp(InArgs._InitialTab, 0, 3);
	const FMTAUiPalette P = MTAUI::Pal();
	FSimpleDelegate OnClose = InArgs._OnClose;

	// Missionsfilter-Leiste
	TSharedRef<SHorizontalBox> Filters = SNew(SHorizontalBox);
	static const TCHAR* const FilterKeys[] = { TEXT("journal.filter.all"), TEXT("journal.filter.main"), TEXT("journal.filter.active"), TEXT("journal.filter.done") };
	for (int32 i = 0; i < 4; ++i)
	{
		Filters->AddSlot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SButton)
			.ButtonColorAndOpacity_Lambda([this, i, P]() { return FSlateColor(MissionFilter == i ? P.ButtonSelected : P.Button); })
			.OnClicked_Lambda([this, i]() { MissionFilter = i; RebuildMissions(); return FReply::Handled(); })
			[ SNew(STextBlock).Text(MTAUI::TrAttr(FilterKeys[i])).Font(MTAUI::Font(15.f)).ColorAndOpacity(FSlateColor(P.Text)) ]
		];
	}

	ChildSlot
	[
		SNew(SBorder).BorderImage(MTAUI::White()).BorderBackgroundColor(FSlateColor(P.Background)).Padding(24.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)[MakeTabButton(0, TEXT("journal.tab.map"))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)[MakeTabButton(1, TEXT("journal.tab.missions"))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)[MakeTabButton(2, TEXT("journal.tab.factions"))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)[MakeTabButton(3, TEXT("journal.tab.stats"))]
				+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
				+ SHorizontalBox::Slot().AutoWidth()[MTAUI::Button(MTAUI::TrAttr(TEXT("common.close")), FOnClicked::CreateLambda([OnClose]() { OnClose.ExecuteIfBound(); return FReply::Handled(); }), 140.f)]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SAssignNew(Switcher, SWidgetSwitcher).WidgetIndex(TabIndex)
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(Map, SMTAMapWidget).Provider(ProviderObj)
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[Filters]
					+ SVerticalBox::Slot().FillHeight(1.f)[ SNew(SScrollBox) + SScrollBox::Slot()[ SAssignNew(MissionList, SVerticalBox) ] ]
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SScrollBox) + SScrollBox::Slot()[ SAssignNew(FactionList, SVerticalBox) ]
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SScrollBox) + SScrollBox::Slot()[ SAssignNew(StatList, SVerticalBox) ]
				]
			]
		]
	];

	RebuildMissions();
	RebuildFactions();
	RebuildStats();
}

void SMTAJournal::RebuildMissions()
{
	if (!MissionList.IsValid()) { return; }
	MissionList->ClearChildren();
	const FMTAUiPalette P = MTAUI::Pal();
	TArray<FMTAMissionEntry> Missions;
	if (const IMTAMapDataProvider* Prov = GetProvider()) { Prov->GetMapMissions(Missions); }

	int32 Shown = 0;
	for (const FMTAMissionEntry& M : Missions)
	{
		const bool bPass = MissionFilter == 0
			|| (MissionFilter == 1 && M.bMain)
			|| (MissionFilter == 2 && M.State == EMTAMissionState::Active)
			|| (MissionFilter == 3 && M.State == EMTAMissionState::Completed);
		if (!bPass) { continue; }
		++Shown;

		const TCHAR* StateKey = M.State == EMTAMissionState::Active ? TEXT("journal.state.active")
			: M.State == EMTAMissionState::Completed ? TEXT("journal.state.done")
			: M.State == EMTAMissionState::Locked ? TEXT("journal.state.locked") : TEXT("journal.state.available");
		const FLinearColor StateCol = M.State == EMTAMissionState::Active ? P.Accent : M.State == EMTAMissionState::Completed ? P.Good : P.TextDim;
		const FName Id = M.Id;
		const FVector2D Pos = M.Pos;
		const bool bHasPos = M.bHasPos;
		const bool bSel = (SelectedMission == Id);

		TSharedRef<SVerticalBox> Details = SNew(SVerticalBox);
		Details->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[ MTAUI::Label(FText::FromString((M.bMain ? TEXT("[*] ") : TEXT("")) + M.Title), 18.f, false, true) ]
			+ SHorizontalBox::Slot().AutoWidth()[ MTAUI::Label(MTAUI::TrAttr(StateKey), 15.f, false, true) ]
		];
		if (bSel)
		{
			Details->AddSlot().AutoHeight().Padding(0.f, 6.f)[ MTAUI::Label(FText::FromString(M.Description), 15.f, true) ];
			if (!M.Reward.IsEmpty())
			{
				Details->AddSlot().AutoHeight()[ MTAUI::Label(MTATrF(TEXT("journal.reward"), { M.Reward }), 15.f) ];
			}
			if (bHasPos)
			{
				Details->AddSlot().AutoHeight().Padding(0.f, 6.f)[ MTAUI::Label(MTAUI::TrAttr(TEXT("journal.click_waypoint")), 14.f, true) ];
			}
		}
		MissionList->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(SButton)
			.ButtonColorAndOpacity(FSlateColor(bSel ? P.PanelAlt : P.Panel))
			.ContentPadding(12.f)
			.OnClicked_Lambda([this, Id, Pos, bHasPos, bSel]()
			{
				SelectedMission = bSel ? NAME_None : Id;
				if (bHasPos && bSel)
				{
					if (IMTAMapDataProvider* Prov = GetProvider()) { Prov->SetWaypoint(Pos); }
					if (Map.IsValid()) { Map->CenterOn(Pos); }
				}
				RebuildMissions();
				return FReply::Handled();
			})
			[ Details ]
		];
	}
	if (Shown == 0)
	{
		MissionList->AddSlot().AutoHeight()[ MTAUI::Label(MTAUI::TrAttr(TEXT("journal.empty")), 16.f, true) ];
	}
}

void SMTAJournal::RebuildFactions()
{
	FactionList->ClearChildren();
	const FMTAUiPalette P = MTAUI::Pal();
	TArray<FMTAFactionEntry> Factions;
	if (const IMTAMapDataProvider* Prov = GetProvider()) { Prov->GetMapFactions(Factions); }
	for (const FMTAFactionEntry& F : Factions)
	{
		const float Alpha = (FMath::Clamp(F.Standing, -100, 100) + 100) / 200.f;
		FactionList->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			MTAUI::Panel(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[ MTAUI::Label(FText::FromString(F.Name), 18.f, false, true) ]
					+ SHorizontalBox::Slot().AutoWidth()[ MTAUI::Label(FText::FromString(FString::Printf(TEXT("%s  %+d"), *F.Rank, F.Standing)), 16.f) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					// Balken: Hintergrund plus Fuellung (Mitte = neutral)
					SNew(SBox).HeightOverride(10.f)
					[
						SNew(SBorder).BorderImage(MTAUI::White()).BorderBackgroundColor(FSlateColor(P.Button)).Padding(0.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(FMath::Max(Alpha, 0.001f))[ SNew(SBorder).BorderImage(MTAUI::White()).BorderBackgroundColor(FSlateColor(F.Color)) ]
							+ SHorizontalBox::Slot().FillWidth(FMath::Max(1.f - Alpha, 0.001f))[ SNew(SSpacer) ]
						]
					]
				], true, 12.f)
		];
	}
	if (Factions.Num() == 0)
	{
		FactionList->AddSlot().AutoHeight()[ MTAUI::Label(MTAUI::TrAttr(TEXT("journal.empty")), 16.f, true) ];
	}
}

void SMTAJournal::RebuildStats()
{
	StatList->ClearChildren();
	TArray<FMTAStatEntry> Stats;
	if (const IMTAMapDataProvider* Prov = GetProvider()) { Prov->GetMapStats(Stats); }
	for (const FMTAStatEntry& S : Stats)
	{
		StatList->AddSlot().AutoHeight().Padding(0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.6f)[ MTAUI::Label(FText::FromString(S.Label), 17.f, true) ]
			+ SHorizontalBox::Slot().FillWidth(0.4f)[ MTAUI::Label(FText::FromString(S.Value), 17.f, false, true) ]
		];
	}
	if (Stats.Num() == 0)
	{
		StatList->AddSlot().AutoHeight()[ MTAUI::Label(MTAUI::TrAttr(TEXT("journal.empty")), 16.f, true) ];
	}
}
