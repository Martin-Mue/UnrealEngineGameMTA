#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "MTAAudioTypes.h"
#include "MTARadioSubsystem.generated.h"

class UAudioComponent;
class USoundBase;

UENUM(BlueprintType)
enum class EMTARadioSegmentType : uint8
{
	Music, Jingle, Ad, News, Talk, Traffic, Weather
};

USTRUCT(BlueprintType)
struct MTA6_API FMTARadioHost
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FString Bio;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTARadioTrack
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FString Title;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FString Artist;
	/** Nominelle Spielzeit; die synthetisierte Schleife wird entsprechend oft wiederholt. */
	UPROPERTY(BlueprintReadOnly, Category = "Radio") float DurationSec = 180.f;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") float Bpm = 100.f;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FString Key;
	/** Name der Audio-Datei/des Assets (z. B. radio_neon_tide_a). */
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FName Loop;
};

/** Textbaustein (Nachricht, Talk, Verkehr, Wetter, Werbespot). */
USTRUCT(BlueprintType)
struct MTA6_API FMTARadioScript
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") EMTARadioSegmentType Type = EMTARadioSegmentType::Talk;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FName HostId;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FString Sponsor;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FString Text;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") float CooldownMinutes = 30.f;
	FMTAAudioCondition When;
};

/** Eine Sendung im Sendeplan (Spielstunden, Mitternachtsueberlauf erlaubt). */
USTRUCT(BlueprintType)
struct MTA6_API FMTARadioShow
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Radio") float FromHour = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") float ToHour = 24.f;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FName HostId;
	/** Gewichte: Musik, Jingle, Werbung, Nachrichten (News/Verkehr/Wetter), Talk. */
	UPROPERTY(BlueprintReadOnly, Category = "Radio") float WeightMusic = 6.f;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") float WeightJingle = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") float WeightAd = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") float WeightNews = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") float WeightTalk = 1.f;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTARadioStation
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") float Frequency = 100.f;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FString Genre;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FString Tagline;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") TArray<FMTARadioHost> Hosts;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") TArray<FString> Jingles;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") TArray<FMTARadioTrack> Tracks;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") TArray<FMTARadioScript> Ads;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") TArray<FMTARadioScript> Scripts;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") TArray<FMTARadioShow> Schedule;
};

/** Gesprochener Abschnitt (Text zum Einblenden/Vertonen; Musik wird dabei abgesenkt). */
USTRUCT(BlueprintType)
struct MTA6_API FMTARadioSegment
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Radio") EMTARadioSegmentType Type = EMTARadioSegmentType::Talk;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FName StationId;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FName ScriptId;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FString Speaker;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") FString Text;
	UPROPERTY(BlueprintReadOnly, Category = "Radio") float DurationSec = 5.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTARadioStationChanged, FName, StationId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTARadioTrackChanged, const FMTARadioTrack&, Track);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTARadioSegmentStarted, const FMTARadioSegment&, Segment);

/**
 * Radio: sechs fiktive Sender (Content/Data/radio.json), Umschalten, Lautstaerke, Sendeplan nach
 * Spielstunde, reaktive Texte (Fahndung/Wetter/Story-Flags/Bezirk), Persistenz des letzten Senders.
 * Die Musik sind selbst synthetisierte Loops (tools/audio); fehlen die Assets, laeuft die Logik
 * (Titel, Sprecher, Texte) trotzdem weiter.
 */
UCLASS()
class MTA6_API UMTARadioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Radio")
	bool LoadRadioData(const FString& RelativePath = TEXT("Data/radio.json"));

	// --- Bedienung ---
	UFUNCTION(BlueprintCallable, Category = "MTA6|Radio")
	bool SetStation(FName StationId);
	UFUNCTION(BlueprintCallable, Category = "MTA6|Radio")
	void NextStation();
	UFUNCTION(BlueprintCallable, Category = "MTA6|Radio")
	void PreviousStation();
	/** Radio aus (Station wird fuers naechste Einschalten gemerkt). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Radio")
	void TurnOff();
	UFUNCTION(BlueprintCallable, Category = "MTA6|Radio")
	void TurnOn();
	UFUNCTION(BlueprintCallable, Category = "MTA6|Radio")
	void SetVolume(float NewVolume);
	UFUNCTION(BlueprintPure, Category = "MTA6|Radio")
	float GetVolume() const { return Volume; }
	UFUNCTION(BlueprintPure, Category = "MTA6|Radio")
	bool IsOn() const { return bOn; }

	// --- Abfragen ---
	UFUNCTION(BlueprintPure, Category = "MTA6|Radio")
	const TArray<FMTARadioStation>& GetStations() const { return Stations; }
	UFUNCTION(BlueprintPure, Category = "MTA6|Radio")
	FName GetCurrentStationId() const;
	UFUNCTION(BlueprintPure, Category = "MTA6|Radio")
	bool GetCurrentStation(FMTARadioStation& Out) const;
	UFUNCTION(BlueprintPure, Category = "MTA6|Radio")
	bool GetCurrentTrack(FMTARadioTrack& Out) const;
	/** Aktuelle Sendung laut Sendeplan. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Radio")
	bool GetCurrentShow(FMTARadioShow& Out) const;

	// --- Spielzustand ---
	/** Ganzer Kontext auf einmal (Fahndung, Bezirk, Wetter, Stunde, Flags). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Radio")
	void SetContext(const FMTAAudioContext& NewContext);
	UFUNCTION(BlueprintCallable, Category = "MTA6|Radio")
	void SetWantedLevel(int32 Level);
	UFUNCTION(BlueprintCallable, Category = "MTA6|Radio")
	void SetDistrict(FName DistrictId);
	UFUNCTION(BlueprintCallable, Category = "MTA6|Radio")
	void SetGameHour(float Hour);
	UFUNCTION(BlueprintCallable, Category = "MTA6|Radio")
	void SetStoryFlag(FName Flag, bool bSet = true);
	/** Wenn true, werden Fahndung (UMTAHeatSubsystem) und Wetter (AMTAWeatherManager) selbst gelesen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Radio")
	bool bAutoReadWorld = true;

	/** Eigene Zeile einspielen (z. B. Story-Ereignis), umgeht Cooldowns. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Radio")
	void InjectLine(const FString& Speaker, const FString& Text, EMTARadioSegmentType Type = EMTARadioSegmentType::News);

	// --- Ereignisse ---
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Radio") FMTARadioStationChanged OnStationChanged;
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Radio") FMTARadioTrackChanged OnTrackChanged;
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Radio") FMTARadioSegmentStarted OnSegmentStarted;

private:
	bool Tick(float Dt);
	void StartTrack(int32 TrackIndex, float ElapsedOffset);
	void AdvanceProgram();
	void QueueBreakAfterTrack();
	bool PickScript(const FMTARadioStation& St, EMTARadioSegmentType Kind, FMTARadioSegment& Out);
	void EnqueueSegment(const FMTARadioSegment& Seg, bool bFront = false);
	void RefreshContextFromWorld();
	void TryReactive(bool bWantedRise, FName NewFlag);
	void StartNextSegment();
	const FMTARadioShow* FindShow(const FMTARadioStation& St) const;
	FString HostName(const FMTARadioStation& St, FName HostId) const;
	void ApplyAudio();
	USoundBase* LoadLoop(FName Loop);
	void LoadState();
	void SaveState() const;
	FString StatePath() const;

	UPROPERTY() TArray<FMTARadioStation> Stations;
	UPROPERTY() TMap<FName, TObjectPtr<USoundBase>> SoundCache;
	UPROPERTY() TObjectPtr<UAudioComponent> MusicComp;

	FMTAAudioContext Context;
	TArray<FMTARadioSegment> Pending;
	TMap<FName, double> LastUsed;
	FMTARadioSegment ActiveSegment;
	FString AudioPath = TEXT("/Game/Audio/Generated/");
	FString LastStationFromSave;
	FTSTicker::FDelegateHandle TickHandle;
	FRandomStream Rng;

	int32 CurrentStation = INDEX_NONE;
	int32 CurrentTrack = INDEX_NONE;
	int32 LastTrack = INDEX_NONE;
	int32 TracksSinceJingle = 0;
	float TrackElapsed = 0.f;
	float SpeechRemaining = 0.f;
	float DuckCurrent = 1.f;
	float Volume = 0.7f;
	float DuckAmount = 0.35f;
	float WordsPerSecond = 2.6f;
	int32 JingleEvery = 2;
	float WorldReadTimer = 0.f;
	double Clock = 0.0;
	bool bOn = false;
	bool bWarnedMissing = false;
};
