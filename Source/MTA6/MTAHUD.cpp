#include "MTAHUD.h"

#include "EngineUtils.h"
#include "MTA6GameMode.h"
#include "MTACharacter.h"
#include "MTADialogueNPC.h"
#include "MTAInteractionComponent.h"
#include "MTAMinimapTypes.h"
#include "MTAMissionComponent.h"
#include "MTAStorySubsystem.h"
#include "MTAVehiclePawn.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
	const FLinearColor ColText(1.f, 1.f, 1.f, 1.f);
	const FLinearColor ColAccent(1.f, 0.82f, 0.2f, 1.f);
	const FLinearColor ColDanger(1.f, 0.25f, 0.2f, 1.f);
	const FLinearColor ColMapBg(0.09f, 0.12f, 0.16f, 0.92f);
	const FLinearColor ColMapBlock(0.2f, 0.26f, 0.32f, 1.f);
	const FLinearColor ColMapTraffic(0.55f, 0.6f, 0.66f, 1.f);
	const FLinearColor ColPoliceRed(1.f, 0.2f, 0.2f, 1.f);
	const FLinearColor ColPoliceBlue(0.25f, 0.4f, 1.f, 1.f);
}

AMTAHUD::AMTAHUD()
{
	FactionIds = { TEXT("union"), TEXT("aurelia"), TEXT("collective"), TEXT("wrenchers") };
	FactionNames = { TEXT("Union"), TEXT("Aurelia"), TEXT("Collective"), TEXT("Wrenchers") };
}

UFont* AMTAHUD::GetHudFont() const
{
	if (HudFont)
	{
		return HudFont;
	}
	return GEngine ? GEngine->GetLargeFont() : nullptr;
}

void AMTAHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}

	UIScale = FMath::Clamp(Canvas->ClipY / FMath::Max(1.f, ReferenceHeight), 0.5f, 3.f);

	UWorld* World = GetWorld();
	AMTA6GameMode* GM = World ? World->GetAuthGameMode<AMTA6GameMode>() : nullptr;
	if (!GM)
	{
		return;
	}

	DrawStatus(GM);
	DrawMinimap(GM);
	DrawToastAndOverlays(GM);
	DrawDialog(GM);

	UpdateConversationInput();
	DrawInteractionPrompt();
	DrawConversation();
}

// --- NPC-Gespraeche -----------------------------------------------------------------------------

void AMTAHUD::OnDialogueViewChanged(const FMTADialogueView& View)
{
	DialogueView = View;
}

void AMTAHUD::UpdateConversationInput()
{
	UGameInstance* GI = GetGameInstance();
	UMTADialogueSubsystem* Dialog = GI ? GI->GetSubsystem<UMTADialogueSubsystem>() : nullptr;
	if (!Dialog || !PlayerOwner)
	{
		return;
	}
	if (!bDialogueBound)
	{
		Dialog->OnDialogueView.AddDynamic(this, &AMTAHUD::OnDialogueViewChanged);
		bDialogueBound = true;
	}

	// Rueckfall: E startet das Gespraech mit dem naechsten NPC, falls die Interaktions-Komponente es nicht tat
	if (!Dialog->IsInConversation() && PlayerOwner->WasInputKeyJustPressed(EKeys::E))
	{
		if (AMTADialogueNPC* Near = FindNearbyNPC())
		{
			IMTAInteractable::Execute_Interact(Near, PlayerOwner->GetPawn());
		}
	}

	const bool bActive = Dialog->IsInConversation() && DialogueView.bActive;

	// Bewegung und Kamera sperren, solange gesprochen wird
	if (bActive && !bMoveLocked)
	{
		PlayerOwner->SetIgnoreMoveInput(true);
		PlayerOwner->SetIgnoreLookInput(true);
		bMoveLocked = true;
	}
	else if (!bActive && bMoveLocked)
	{
		PlayerOwner->SetIgnoreMoveInput(false);
		PlayerOwner->SetIgnoreLookInput(false);
		bMoveLocked = false;
	}
	if (!bActive)
	{
		return;
	}

	if (DialogueView.bIsChoice)
	{
		static const FKey Keys[9] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
		for (int32 i = 0; i < 9 && i < DialogueView.Options.Num(); ++i)
		{
			if (PlayerOwner->WasInputKeyJustPressed(Keys[i]))
			{
				Dialog->ChooseOption(DialogueView.Options[i].Index);
				break;
			}
		}
	}
	else if (PlayerOwner->WasInputKeyJustPressed(EKeys::Enter) || PlayerOwner->WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		Dialog->Continue();
	}
	// Gespraech jederzeit beenden: Esc, Rueck- oder X-Taste
	if (PlayerOwner->WasInputKeyJustPressed(EKeys::Escape) || PlayerOwner->WasInputKeyJustPressed(EKeys::BackSpace) || PlayerOwner->WasInputKeyJustPressed(EKeys::X))
	{
		Dialog->EndConversation();
	}
}

void AMTAHUD::DrawInteractionPrompt()
{
	if (DialogueView.bActive || !PlayerOwner)
	{
		return;
	}
	const AMTACharacter* Hero = Cast<AMTACharacter>(PlayerOwner->GetPawn());
	if (!Hero || !Hero->Interaction)
	{
		return;
	}
	FString PromptText = Hero->Interaction->GetPromptText().ToString();
	if (PromptText.IsEmpty())
	{
		// Rueckfall: naechster ansprechbarer NPC in Reichweite
		if (const AMTADialogueNPC* Near = FindNearbyNPC())
		{
			PromptText = FString::Printf(TEXT("Mit %s sprechen"), *Near->DisplayName);
		}
	}
	if (PromptText.IsEmpty())
	{
		return;
	}
	DrawCenteredText(FString::Printf(TEXT("[E] %s"), *PromptText), Canvas->ClipY * 0.70f, FLinearColor(1.f, 0.88f, 0.45f, 1.f), 1.2f * UIScale);
}

AMTADialogueNPC* AMTAHUD::FindNearbyNPC() const
{
	const APawn* Pawn = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
	UWorld* World = GetWorld();
	if (!Pawn || !World || !Pawn->IsA<AMTACharacter>())
	{
		return nullptr;
	}
	AMTADialogueNPC* Best = nullptr;
	float BestDist = 500.f;
	for (TActorIterator<AMTADialogueNPC> It(World); It; ++It)
	{
		const float D = FVector::Dist2D(It->GetActorLocation(), Pawn->GetActorLocation());
		if (D < BestDist)
		{
			BestDist = D;
			Best = *It;
		}
	}
	return Best;
}

void AMTAHUD::DrawConversation()
{
	if (!DialogueView.bActive)
	{
		return;
	}

	const float S = 1.0f * UIScale;
	const float Pad = 22.f * UIScale;
	const float Width = FMath::Min(1000.f * UIScale, Canvas->ClipX - 40.f * UIScale);
	const float TextWidth = Width - 2.f * Pad;
	const float LineH = 34.f * UIScale;

	TArray<FString> Lines;
	TArray<FLinearColor> Colors;
	if (!DialogueView.SpeakerName.IsEmpty())
	{
		Lines.Add(DialogueView.SpeakerName);
		Colors.Add(FLinearColor(1.f, 0.82f, 0.35f, 1.f));
	}
	TArray<FString> Wrapped;
	WrapText(DialogueView.Text, TextWidth, S, Wrapped);
	for (const FString& L : Wrapped)
	{
		Lines.Add(L);
		Colors.Add(FLinearColor::White);
	}
	if (DialogueView.bIsChoice)
	{
		for (int32 i = 0; i < DialogueView.Options.Num() && i < 9; ++i)
		{
			const FMTADialogueOptionView& O = DialogueView.Options[i];
			FString Label = FString::Printf(TEXT("[%d] %s"), i + 1, *O.Text);
			if (O.bHasCheck)
			{
				Label += FString::Printf(TEXT("  (%s %d/%d)"), *O.Skill.ToString(), O.PlayerValue, O.Difficulty);
			}
			Lines.Add(Label);
			Colors.Add(O.bHasCheck && !O.bWillSucceed ? FLinearColor(1.f, 0.55f, 0.45f, 1.f) : FLinearColor(0.75f, 0.95f, 0.8f, 1.f));
		}
	}
	else
	{
		Lines.Add(TEXT("[Enter] Weiter"));
		Colors.Add(FLinearColor(0.85f, 0.85f, 0.85f, 1.f));
	}
	// Immer sichtbar: so beendet man das Gespraech
	Lines.Add(TEXT("[Esc] Gespraech beenden"));
	Colors.Add(FLinearColor(1.f, 0.6f, 0.5f, 1.f));

	const float Height = Pad * 2.f + LineH * Lines.Num();
	const float X = (Canvas->ClipX - Width) * 0.5f;
	const float Y = Canvas->ClipY - Height - 40.f * UIScale;
	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.88f), X, Y, Width, Height);
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		DrawShadowText(Lines[i], X + Pad, Y + Pad + LineH * i, Colors[i], S);
	}
}

// --- Text-Hilfen ------------------------------------------------------------------------------

float AMTAHUD::DrawShadowText(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale)
{
	UFont* Font = GetHudFont();
	if (!Canvas || !Font)
	{
		return 0.f;
	}
	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), Font, Color);
	Item.Scale = FVector2D(Scale, Scale);
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, Color.A), FVector2D(2.f, 2.f));
	Canvas->DrawItem(Item);

	float W = 0.f, H = 0.f;
	GetTextSize(Text.IsEmpty() ? FString(TEXT(" ")) : Text, W, H, Font, Scale);
	return H;
}

void AMTAHUD::DrawCenteredText(const FString& Text, float Y, const FLinearColor& Color, float Scale)
{
	float W = 0.f, H = 0.f;
	GetTextSize(Text, W, H, GetHudFont(), Scale);
	DrawShadowText(Text, (Canvas->ClipX - W) * 0.5f, Y, Color, Scale);
}

void AMTAHUD::WrapText(const FString& Text, float MaxWidth, float Scale, TArray<FString>& OutLines)
{
	UFont* Font = GetHudFont();
	TArray<FString> Paragraphs;
	Text.ParseIntoArray(Paragraphs, TEXT("\n"), false);
	for (const FString& Paragraph : Paragraphs)
	{
		TArray<FString> Words;
		Paragraph.ParseIntoArrayWS(Words);
		if (Words.Num() == 0)
		{
			OutLines.Add(FString());
			continue;
		}
		FString Line;
		for (const FString& Word : Words)
		{
			const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
			float W = 0.f, H = 0.f;
			GetTextSize(Candidate, W, H, Font, Scale);
			if (W > MaxWidth && !Line.IsEmpty())
			{
				OutLines.Add(Line);
				Line = Word;
			}
			else
			{
				Line = Candidate;
			}
		}
		OutLines.Add(Line);
	}
}

// --- Statusblock -------------------------------------------------------------------------------

void AMTAHUD::DrawStatus(AMTA6GameMode* GM)
{
	const float S = 0.9f * UIScale;
	const float X = 20.f * UIScale;
	float Y = 14.f * UIScale;

	const AMTAVehiclePawn* Vehicle = GM->GetPlayerVehicle();
	const APawn* PlayerPawn = GetOwningPawn();
	const float SpeedKmh = Vehicle ? Vehicle->GetSpeedKmh() : (PlayerPawn ? PlayerPawn->GetVelocity().Size() * 0.036f : 0.f);

	const float Hour = GM->GetHour();
	const int32 HH = FMath::FloorToInt(Hour) % 24;
	const int32 MM = FMath::Clamp(FMath::FloorToInt((Hour - FMath::FloorToFloat(Hour)) * 60.f), 0, 59);
	Y += DrawShadowText(FString::Printf(TEXT("%d km/h    %02d:%02d"), FMath::RoundToInt(SpeedKmh), HH, MM), X, Y, ColText, S);
	Y += DrawShadowText(FString::Printf(TEXT("$ %d"), GM->GetMoney()), X, Y, ColAccent, S);

	const int32 Stars = GM->GetWantedStars();
	const FString StarText = FString::ChrN(Stars, TEXT('*')) + FString::ChrN(5 - Stars, TEXT('-'));
	Y += DrawShadowText(TEXT("Fahndung: ") + StarText, X, Y, Stars > 0 ? ColDanger : ColText, S);

	if (const UMTAMissionComponent* Mission = GM->GetMissionComponent())
	{
		Y += DrawShadowText(TEXT("Auftrag: ") + Mission->GetMissionDisplayTitle(), X, Y, ColText, S);

		FString TimeText;
		if (!Mission->IsStoryMission())
		{
			TimeText = Mission->GetTimeLeft() > 0.f ? FString::Printf(TEXT("%d s"), FMath::FloorToInt(Mission->GetTimeLeft())) : FString(TEXT("ohne Bonus"));
		}
		const float Dist = Mission->GetDistanceToTarget();
		Y += DrawShadowText(FString::Printf(TEXT("Ziel: %d m   %s"), FMath::Max(0, FMath::FloorToInt(Dist / 100.f)), *TimeText), X, Y, ColText, S);
	}

	// Fraktionsstaende
	const UGameInstance* GI = GetGameInstance();
	if (const UMTAStorySubsystem* Story = GI ? GI->GetSubsystem<UMTAStorySubsystem>() : nullptr)
	{
		FString Line;
		for (int32 i = 0; i < FactionIds.Num(); ++i)
		{
			const FString& Name = FactionNames.IsValidIndex(i) ? FactionNames[i] : FactionIds[i];
			Line += FString::Printf(TEXT("%s%s %d"), Line.IsEmpty() ? TEXT("") : TEXT("  "), *Name, FMath::RoundToInt(Story->GetStanding(FactionIds[i])));
		}
		Y += DrawShadowText(Line, X, Y, ColText, S);
	}

	if (Vehicle)
	{
		const int32 Pct = FMath::RoundToInt(Vehicle->GetHealthFraction() * 100.f);
		const FLinearColor C = Pct > 50 ? ColText : (Pct > 20 ? ColAccent : ColDanger);
		Y += DrawShadowText(FString::Printf(TEXT("Fahrzeug: %d%%"), Pct), X, Y, C, S);
	}

	if (bShowHelpLine)
	{
		const FString Help = TEXT("W/S Gas/Bremse   A/D Lenken   Leertaste Handbremse   R Zuruecksetzen   F5 Speichern   F9 Laden");
		DrawShadowText(Help, X, Canvas->ClipY - 36.f * UIScale, FLinearColor(1.f, 1.f, 1.f, 0.8f), 0.6f * UIScale);
	}
}

// --- Toast, WRECKED, FESTGENOMMEN ------------------------------------------------------------------

void AMTAHUD::DrawToastAndOverlays(AMTA6GameMode* GM)
{
	const float Alpha = GM->GetToastAlpha();
	if (Alpha > 0.f && !GM->GetToastText().IsEmpty())
	{
		DrawCenteredText(GM->GetToastText(), 130.f * UIScale, FLinearColor(1.f, 1.f, 1.f, Alpha), 1.3f * UIScale);
	}

	const AMTAVehiclePawn* Vehicle = GM->GetPlayerVehicle();
	if (Vehicle && Vehicle->bWrecked)
	{
		DrawCenteredText(TEXT("WRECKED"), Canvas->ClipY * 0.38f, FLinearColor(1.f, 0.45f, 0.1f, 1.f), 3.f * UIScale);
	}
	if (GM->IsBustedOverlayActive())
	{
		DrawCenteredText(TEXT("FESTGENOMMEN"), Canvas->ClipY * 0.38f, ColDanger, 3.f * UIScale);
	}
}

// --- Dialog ------------------------------------------------------------------------------------------

void AMTAHUD::DrawDialog(AMTA6GameMode* GM)
{
	const UMTAMissionComponent* Mission = GM->GetMissionComponent();
	if (!Mission || !Mission->IsDialogOpen())
	{
		return;
	}

	const float S = 0.85f * UIScale;
	const float Pad = 20.f * UIScale;
	const float Width = FMath::Min(780.f * UIScale, Canvas->ClipX - 40.f * UIScale);
	const float TextWidth = Width - 2.f * Pad;

	TArray<FString> Lines;
	TArray<FLinearColor> Colors;
	auto AddBlock = [&](const FString& Text, const FLinearColor& Color)
	{
		const int32 Before = Lines.Num();
		WrapText(Text, TextWidth, S, Lines);
		for (int32 i = Before; i < Lines.Num(); ++i)
		{
			Colors.Add(Color);
		}
	};

	AddBlock(Mission->GetDialogTitle(), ColAccent);
	AddBlock(FString(), ColText);
	AddBlock(Mission->GetDialogText(), ColText);
	AddBlock(FString(), ColText);

	const TArray<FMTAChoiceOption> Options = Mission->GetDialogOptions();
	for (int32 i = 0; i < Options.Num() && i < 4; ++i)
	{
		AddBlock(FString::Printf(TEXT("[%d] %s"), i + 1, *Options[i].Text), ColAccent);
	}
	if (Options.Num() == 0)
	{
		AddBlock(Mission->GetDialogStage() == EMTADialogStage::Ending ? TEXT("[Enter] Schliessen") : TEXT("[Enter] Weiter"), ColAccent);
	}

	float LineW = 0.f, LineH = 0.f;
	GetTextSize(TEXT("Ag"), LineW, LineH, GetHudFont(), S);
	const float Height = Lines.Num() * LineH + 2.f * Pad;
	const float Left = (Canvas->ClipX - Width) * 0.5f;
	const float Top = FMath::Max(10.f, (Canvas->ClipY - Height) * 0.5f);

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.78f), Left, Top, Width, Height);
	DrawLine(Left, Top, Left + Width, Top, ColAccent, 2.f);

	float Y = Top + Pad;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		DrawShadowText(Lines[i], Left + Pad, Y, Colors[i], S);
		Y += LineH;
	}
}

// --- Minimap -----------------------------------------------------------------------------------------

void AMTAHUD::AddTriangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color, TArray<FCanvasUVTri>& OutTris)
{
	FCanvasUVTri& T = OutTris.AddDefaulted_GetRef();
	T.V0_Pos = A; T.V1_Pos = B; T.V2_Pos = C;
	T.V0_UV = FVector2D::ZeroVector; T.V1_UV = FVector2D::ZeroVector; T.V2_UV = FVector2D::ZeroVector;
	T.V0_Color = Color; T.V1_Color = Color; T.V2_Color = Color;
}

void AMTAHUD::AddCircle(const FVector2D& Center, float Radius, const FLinearColor& Color, TArray<FCanvasUVTri>& OutTris)
{
	constexpr int32 Segments = 12;
	for (int32 i = 0; i < Segments; ++i)
	{
		const float A0 = 2.f * UE_PI * i / Segments;
		const float A1 = 2.f * UE_PI * (i + 1) / Segments;
		AddTriangle(Center,
			Center + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius,
			Center + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius, Color, OutTris);
	}
}

void AMTAHUD::AddClippedPolygon(const TArray<FVector2D>& Poly, const FVector2D& Min, const FVector2D& Max,
	const FLinearColor& Color, TArray<FCanvasUVTri>& OutTris)
{
	// Sutherland-Hodgman gegen die vier Kanten des Minimap-Rechtecks
	TArray<FVector2D> Current = Poly;
	for (int32 Edge = 0; Edge < 4 && Current.Num() > 0; ++Edge)
	{
		const int32 Axis = Edge / 2;          // 0 = X, 1 = Y
		const bool bMinSide = (Edge % 2) == 0;
		const float Limit = bMinSide ? Min[Axis] : Max[Axis];
		auto Inside = [&](const FVector2D& P) { return bMinSide ? P[Axis] >= Limit : P[Axis] <= Limit; };

		TArray<FVector2D> Out;
		for (int32 i = 0; i < Current.Num(); ++i)
		{
			const FVector2D& A = Current[i];
			const FVector2D& B = Current[(i + 1) % Current.Num()];
			const bool bInA = Inside(A);
			const bool bInB = Inside(B);
			if (bInA)
			{
				Out.Add(A);
			}
			if (bInA != bInB)
			{
				const double T = (Limit - A[Axis]) / (B[Axis] - A[Axis]);
				Out.Add(A + (B - A) * T);
			}
		}
		Current = MoveTemp(Out);
	}
	for (int32 i = 1; i + 1 < Current.Num(); ++i)
	{
		AddTriangle(Current[0], Current[i], Current[i + 1], Color, OutTris);
	}
}

void AMTAHUD::DrawMinimap(AMTA6GameMode* GM)
{
	const APawn* PlayerPawn = GetOwningPawn();
	if (!PlayerPawn)
	{
		return;
	}

	const float Size = MinimapSize * UIScale;
	const FVector2D Min(Canvas->ClipX - Size - 20.f * UIScale, 20.f * UIScale);
	const FVector2D Max = Min + FVector2D(Size, Size);
	const FVector2D Center = (Min + Max) * 0.5;
	const float Scale = (Size * 0.5f) / FMath::Max(1.f, MinimapWorldRadius);

	const FVector CarLoc = PlayerPawn->GetActorLocation();
	const float YawRad = FMath::DegreesToRadians(PlayerPawn->GetActorRotation().Yaw);
	const FVector2D Fwd(FMath::Cos(YawRad), FMath::Sin(YawRad));
	const FVector2D Right(-FMath::Sin(YawRad), FMath::Cos(YawRad));

	// Weltpunkt -> Minimap (Fahrzeug zeigt nach oben, Karte dreht sich)
	auto ToMap = [&](double WX, double WY) -> FVector2D
	{
		const FVector2D D(WX - CarLoc.X, WY - CarLoc.Y);
		return Center + FVector2D(FVector2D::DotProduct(D, Right) * Scale, -FVector2D::DotProduct(D, Fwd) * Scale);
	};
	auto InRect = [&](const FVector2D& P) { return P.X >= Min.X && P.X <= Max.X && P.Y >= Min.Y && P.Y <= Max.Y; };

	DrawRect(ColMapBg, Min.X, Min.Y, Size, Size);

	// Stadtbloecke
	TArray<FCanvasUVTri> Tris;
	const float H = MTAGrid::BlockHalfSize;
	for (int32 i = 0; i < MTAGrid::Blocks; ++i)
	{
		for (int32 j = 0; j < MTAGrid::Blocks; ++j)
		{
			const float CX = MTAGrid::R(i) + MTAGrid::Period * 0.5f;
			const float CY = MTAGrid::R(j) + MTAGrid::Period * 0.5f;
			if (FVector2D::Distance(FVector2D(CX, CY), FVector2D(CarLoc.X, CarLoc.Y)) > MinimapWorldRadius * 1.5f + H)
			{
				continue;
			}
			const TArray<FVector2D> Poly = { ToMap(CX - H, CY - H), ToMap(CX + H, CY - H), ToMap(CX + H, CY + H), ToMap(CX - H, CY + H) };
			AddClippedPolygon(Poly, Min, Max, ColMapBlock, Tris);
		}
	}

	// KI-Punkte (Polizei blinkt rot/blau)
	const bool bBlink = (FMath::FloorToInt(GM->GetGameTime() * 4.f) % 2) == 0;
	for (const FMTAMinimapBlip& Blip : GM->GetMinimapBlips())
	{
		if (Blip.Kind == EMTAMinimapBlipKind::Pedestrian)
		{
			continue;
		}
		const FVector2D P = ToMap(Blip.WorldPos.X, Blip.WorldPos.Y);
		if (!InRect(P))
		{
			continue;
		}
		if (Blip.Kind == EMTAMinimapBlipKind::Police)
		{
			AddCircle(P, 4.f * UIScale, bBlink ? ColPoliceRed : ColPoliceBlue, Tris);
		}
		else if (bShowTrafficOnMinimap)
		{
			const float R = 1.5f * UIScale;
			AddClippedPolygon({ P + FVector2D(-R, -R), P + FVector2D(R, -R), P + FVector2D(R, R), P + FVector2D(-R, R) }, Min, Max, ColMapTraffic, Tris);
		}
	}

	// Missionsziel, am Rand geklemmt
	if (const UMTAMissionComponent* Mission = GM->GetMissionComponent())
	{
		const FVector Target = Mission->GetTargetLocation();
		FVector2D D(Target.X - CarLoc.X, Target.Y - CarLoc.Y);
		const float Lim = MinimapWorldRadius * 0.88f;
		if (D.Size() > Lim)
		{
			D = D.GetSafeNormal() * Lim;
		}
		AddCircle(ToMap(CarLoc.X + D.X, CarLoc.Y + D.Y), 6.f * UIScale, ColAccent, Tris);
	}

	// Spielerpfeil
	const float U = UIScale;
	AddTriangle(Center + FVector2D(0.f, -9.f * U), Center + FVector2D(-6.f * U, 7.f * U), Center + FVector2D(6.f * U, 7.f * U), FLinearColor::White, Tris);

	if (Tris.Num() > 0)
	{
		Canvas->K2_DrawTriangle(nullptr, Tris);
	}

	// Rahmen
	const FLinearColor Border(1.f, 1.f, 1.f, 0.8f);
	const float T = 2.f;
	DrawLine(Min.X, Min.Y, Max.X, Min.Y, Border, T);
	DrawLine(Max.X, Min.Y, Max.X, Max.Y, Border, T);
	DrawLine(Max.X, Max.Y, Min.X, Max.Y, Border, T);
	DrawLine(Min.X, Max.Y, Min.X, Min.Y, Border, T);
}
