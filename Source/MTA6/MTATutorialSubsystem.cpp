#include "MTATutorialSubsystem.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "MTALocalizationSubsystem.h"
#include "MTAMenuStyle.h"
#include "MTAMenuWidgets.h"
#include "MTASettingsSubsystem.h"
#include "MTASettingsUser.h"

UMTATutorialSubsystem* UMTATutorialSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMTATutorialSubsystem>() : nullptr;
}

void UMTATutorialSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMTASettingsSubsystem>();
	Collection.InitializeDependency<UMTALocalizationSubsystem>();
	LoadHints();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMTATutorialSubsystem::Tick), 0.f);
}

void UMTATutorialSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	HideToast();
	CloseHelp();
	Super::Deinitialize();
}

int32 UMTATutorialSubsystem::LoadHints()
{
	Hints.Reset();
	const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"), TEXT("tutorial.json"));
	FString Raw;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Raw, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw), Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("MTA6: tutorial.json fehlt oder ist ungueltig (%s)"), *Path);
		return 0;
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Root->TryGetArrayField(TEXT("hints"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V->TryGetObject(O)) { continue; }
			FMTATutorialHint H;
			FString S;
			if ((*O)->TryGetStringField(TEXT("id"), S)) { H.Id = FName(*S); }
			if ((*O)->TryGetStringField(TEXT("trigger"), S)) { H.Trigger = FName(*S); }
			(*O)->TryGetStringField(TEXT("textKey"), H.TextKey);
			(*O)->TryGetStringField(TEXT("condition"), H.Condition);
			(*O)->TryGetStringField(TEXT("category"), H.Category);
			(*O)->TryGetBoolField(TEXT("once"), H.bOnce);
			double D = 0;
			if ((*O)->TryGetNumberField(TEXT("delay"), D)) { H.Delay = (float)D; }
			if ((*O)->TryGetNumberField(TEXT("duration"), D)) { H.Duration = (float)D; }
			if ((*O)->TryGetNumberField(TEXT("priority"), D)) { H.Priority = (int32)D; }
			if (!H.Id.IsNone() && !H.TextKey.IsEmpty()) { Hints.Add(H); }
		}
	}
	return Hints.Num();
}

// ------------------------------------------------------------------------- Bedingungen

void UMTATutorialSubsystem::SetContextValue(FName Key, float Value)
{
	Context.Add(Key, Value);
}

bool UMTATutorialSubsystem::EvaluateCondition(const FString& Condition) const
{
	if (Condition.TrimStartAndEnd().IsEmpty()) { return true; }
	TArray<FString> Terms;
	Condition.ParseIntoArray(Terms, TEXT("&&"));
	for (FString Term : Terms)
	{
		Term.TrimStartAndEndInline();
		static const TCHAR* const Ops[] = { TEXT(">="), TEXT("<="), TEXT("=="), TEXT("!="), TEXT(">"), TEXT("<") };
		bool bHandled = false;
		for (const TCHAR* Op : Ops)
		{
			FString Left, Right;
			if (Term.Split(Op, &Left, &Right))
			{
				const float L = Context.FindRef(FName(*Left.TrimStartAndEnd()));
				const float R = FCString::Atof(*Right.TrimStartAndEnd());
				const FString O(Op);
				const bool bOk = O == TEXT(">=") ? L >= R : O == TEXT("<=") ? L <= R : O == TEXT("==") ? FMath::IsNearlyEqual(L, R) : O == TEXT("!=") ? !FMath::IsNearlyEqual(L, R) : O == TEXT(">") ? L > R : L < R;
				if (!bOk) { return false; }
				bHandled = true;
				break;
			}
		}
		if (!bHandled)
		{
			// Ohne Operator: "key" = wahr wenn != 0, "!key" = wahr wenn == 0.
			const bool bNeg = Term.StartsWith(TEXT("!"));
			const float V = Context.FindRef(FName(*(bNeg ? Term.Mid(1) : Term).TrimStartAndEnd()));
			if ((V != 0.f) == bNeg) { return false; }
		}
	}
	return true;
}

// ---------------------------------------------------------------------------- Ausloeser

void UMTATutorialSubsystem::FireTrigger(FName Trigger)
{
	const UMTAGameUserSettings* S = UMTAGameUserSettings::Get();
	UMTASettingsSubsystem* Sub = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMTASettingsSubsystem>() : nullptr;
	UMTAGameUserSettings* Settings = Sub ? Sub->GetSettings() : const_cast<UMTAGameUserSettings*>(S);
	if (Settings && !Settings->bTutorialHints) { return; }

	for (const FMTATutorialHint& H : Hints)
	{
		if (H.Trigger != Trigger) { continue; }
		if (H.bOnce && Settings && Settings->HasSeenTutorialHint(H.Id)) { continue; }
		if (SessionShown.Contains(H.Id) && H.bOnce) { continue; }
		if (Pending.ContainsByPredicate([&H](const FPending& P) { return P.Hint.Id == H.Id; })) { continue; }
		if (!EvaluateCondition(H.Condition)) { continue; }
		Pending.Add({ H, H.Delay });
	}
	// Hoehere Prioritaet zuerst.
	Pending.StableSort([](const FPending& A, const FPending& B) { return A.Hint.Priority > B.Hint.Priority; });
}

FText UMTATutorialSubsystem::ResolveText(const FMTATutorialHint& Hint, bool bGamepad) const
{
	const UMTALocalizationSubsystem* Loc = UMTALocalizationSubsystem::GetActive();
	if (!Loc) { return FText::FromString(Hint.TextKey); }
	const FString Specific = Hint.TextKey + (bGamepad ? TEXT(".pad") : TEXT(".kb"));
	return Loc->HasKey(Specific) ? Loc->Tr(Specific) : Loc->Tr(Hint.TextKey);
}

void UMTATutorialSubsystem::DetectInputDevice()
{
	if (!bAutoDetect || !GEngine) { return; }
	const APlayerController* PC = UGameplayStatics::GetPlayerController(GetGameInstance()->GetWorld(), 0);
	if (!PC) { return; }
	static const FKey PadKeys[] = { EKeys::Gamepad_LeftX, EKeys::Gamepad_LeftY, EKeys::Gamepad_RightTriggerAxis, EKeys::Gamepad_LeftTriggerAxis };
	for (const FKey& K : PadKeys)
	{
		if (FMath::Abs(PC->GetInputAnalogKeyState(K)) > 0.3f) { bUsingGamepad = true; return; }
	}
	if (PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom) || PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left))
	{
		bUsingGamepad = true; return;
	}
	if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::SpaceBar))
	{
		bUsingGamepad = false;
	}
}

bool UMTATutorialSubsystem::Tick(float DeltaTime)
{
	DetectInputDevice();

	// F1: Kontexthilfe.
	if (const APlayerController* PC = (GetGameInstance() && GetGameInstance()->GetWorld()) ? UGameplayStatics::GetPlayerController(GetGameInstance()->GetWorld(), 0) : nullptr)
	{
		if (PC->WasInputKeyJustPressed(EKeys::F1)) { ToggleHelp(); }
	}

	// Aktuelle Einblendung ausblenden.
	if (ToastWidget.IsValid())
	{
		ToastTimeLeft -= DeltaTime;
		ToastAlpha = FMath::Clamp(ToastTimeLeft / 0.5f, 0.f, 1.f); // letzte halbe Sekunde ausblenden
		if (ToastTimeLeft <= 0.f) { HideToast(); }
	}
	else if (Pending.Num() > 0)
	{
		// Verzoegerungen herunterzaehlen und den ersten faelligen Hinweis zeigen.
		for (FPending& P : Pending) { P.TimeLeft -= DeltaTime; }
		const int32 Idx = Pending.IndexOfByPredicate([](const FPending& P) { return P.TimeLeft <= 0.f; });
		if (Idx != INDEX_NONE)
		{
			const FMTATutorialHint H = Pending[Idx].Hint;
			Pending.RemoveAt(Idx);
			if (EvaluateCondition(H.Condition)) { ShowHint(H); }
		}
	}
	return true;
}

void UMTATutorialSubsystem::ShowHint(const FMTATutorialHint& Hint)
{
	if (!GEngine || !GEngine->GameViewport) { return; }
	const FMTAUiPalette P = MTAUI::Pal();
	const FMTATutorialHint HintCopy = Hint;

	TSharedRef<SWidget> W =
		SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0.f, 0.f, 0.f, 120.f))
		[
			SNew(SBox).MaxDesiredWidth(760.f)
			[
				SNew(SBorder).BorderImage(MTAUI::White()).BorderBackgroundColor(FSlateColor(P.Panel)).Padding(FMargin(22.f, 14.f))
				.ColorAndOpacity_Lambda([this]() { return FLinearColor(1.f, 1.f, 1.f, ToastAlpha); })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
					[ MTAUI::Label(FText::FromString(TEXT("[i]")), 22.f, false, true) ]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[ MTAUI::Label(TAttribute<FText>::CreateLambda([this, HintCopy]() { return ResolveText(HintCopy, bUsingGamepad); }), 18.f) ]
				]
			]
		];

	HideToast();
	ToastWidget = W;
	ToastTimeLeft = FMath::Max(Hint.Duration, 2.f);
	ToastAlpha = 1.f;
	GEngine->GameViewport->AddViewportWidgetContent(W, 90);

	SessionShown.Add(Hint.Id);
	if (Hint.bOnce)
	{
		if (UMTASettingsSubsystem* Sub = GetGameInstance()->GetSubsystem<UMTASettingsSubsystem>())
		{
			if (UMTAGameUserSettings* S = Sub->GetSettings()) { S->MarkTutorialHintSeen(Hint.Id); }
		}
	}
	OnHintShown.Broadcast(Hint);
}

void UMTATutorialSubsystem::HideToast()
{
	if (ToastWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ToastWidget.ToSharedRef());
	}
	ToastWidget.Reset();
	ToastTimeLeft = 0.f;
}

void UMTATutorialSubsystem::ResetSeenHints()
{
	SessionShown.Reset();
	if (UMTASettingsSubsystem* Sub = GetGameInstance()->GetSubsystem<UMTASettingsSubsystem>())
	{
		if (UMTAGameUserSettings* S = Sub->GetSettings()) { S->SeenTutorialHints.Reset(); S->SaveConfig(); }
	}
}

// ---------------------------------------------------------------------------- Kontexthilfe

void UMTATutorialSubsystem::ToggleHelp()
{
	if (HelpWidget.IsValid()) { CloseHelp(); } else { BuildHelp(); }
}

void UMTATutorialSubsystem::CloseHelp()
{
	if (HelpWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(HelpWidget.ToSharedRef());
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetGameInstance()->GetWorld(), 0))
		{
			PC->SetInputMode(FInputModeGameOnly());
			PC->SetShowMouseCursor(false);
		}
	}
	HelpWidget.Reset();
}

void UMTATutorialSubsystem::BuildHelp()
{
	if (!GEngine || !GEngine->GameViewport) { return; }
	bHelpGamepadView = bUsingGamepad;
	const FMTAUiPalette P = MTAUI::Pal();

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FMTATutorialHint& H : Hints)
	{
		const FMTATutorialHint Copy = H;
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			MTAUI::Panel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ MTAUI::Label(FText::FromString(H.Category.ToUpper()), 12.f, true) ]
				+ SVerticalBox::Slot().AutoHeight()[ MTAUI::Label(TAttribute<FText>::CreateLambda([this, Copy]() { return ResolveText(Copy, bHelpGamepadView); }), 16.f) ],
				true, 12.f)
		];
	}

	TSharedRef<SWidget> Content =
		SNew(SBorder).BorderImage(MTAUI::White()).BorderBackgroundColor(FSlateColor(P.Background)).Padding(48.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)[ MTAUI::Label(MTAUI::TrAttr(TEXT("tut.help.title")), 34.f, false, true) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
				[ MTAUI::Button(TAttribute<FText>::CreateLambda([this]() { return MTATr(bHelpGamepadView ? TEXT("tut.help.view_pad") : TEXT("tut.help.view_kb")); }),
					FOnClicked::CreateLambda([this]() { bHelpGamepadView = !bHelpGamepadView; return FReply::Handled(); }), 280.f, true) ]
				+ SHorizontalBox::Slot().AutoWidth()
				[ MTAUI::Button(MTAUI::TrAttr(TEXT("common.close")), FOnClicked::CreateLambda([this]() { CloseHelp(); return FReply::Handled(); }), 180.f) ]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)[ SNew(SScrollBox) + SScrollBox::Slot()[ List ] ]
		];

	TSharedRef<SMTAScreenFrame> Frame = SNew(SMTAScreenFrame).OnBack(FSimpleDelegate::CreateUObject(this, &UMTATutorialSubsystem::CloseHelp))[ Content ];
	HelpWidget = Frame;
	GEngine->GameViewport->AddViewportWidgetContent(Frame, 95);
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetGameInstance()->GetWorld(), 0))
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Frame);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
	}
}
