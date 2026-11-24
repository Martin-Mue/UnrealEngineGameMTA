#include "MTAStreetCrowd.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Crc.h"

namespace
{
	// Vornamen und Nachnamen fuer generische Passanten (nur ASCII und Umlaute).
	const TCHAR* const GStreetFirstNames[] =
	{
		TEXT("Ada"), TEXT("Bela"), TEXT("Cem"), TEXT("Dana"), TEXT("Emil"), TEXT("Fenja"), TEXT("Goran"), TEXT("Hanna"),
		TEXT("Ilja"), TEXT("Jule"), TEXT("Kasimir"), TEXT("Lene"), TEXT("Malik"), TEXT("Nele"), TEXT("Oskar"), TEXT("Paula"),
		TEXT("Quentin"), TEXT("Rieke"), TEXT("Samir"), TEXT("Tilda"), TEXT("Ugo"), TEXT("Vera"), TEXT("Willem"), TEXT("Xenia"),
		TEXT("Yusuf"), TEXT("Zora"), TEXT("Jörn"), TEXT("Sönke"), TEXT("Märta"), TEXT("Günther"), TEXT("Ayla"), TEXT("Bruno"),
		TEXT("Carla"), TEXT("Darius"), TEXT("Elin"), TEXT("Fiete"), TEXT("Greta"), TEXT("Hamza"), TEXT("Ines"), TEXT("Jasper")
	};

	const TCHAR* const GStreetLastNames[] =
	{
		TEXT("Albers"), TEXT("Brink"), TEXT("Castell"), TEXT("Dahl"), TEXT("Eskildsen"), TEXT("Falk"), TEXT("Grau"), TEXT("Hagedorn"),
		TEXT("Ivanova"), TEXT("Jensen"), TEXT("Kowalski"), TEXT("Lund"), TEXT("Moreau"), TEXT("Nyberg"), TEXT("Okonkwo"), TEXT("Petersen"),
		TEXT("Quast"), TEXT("Rahimi"), TEXT("Sandvik"), TEXT("Tamm"), TEXT("Ullrich"), TEXT("Voigt"), TEXT("Wendt"), TEXT("Yilmaz"),
		TEXT("Zander"), TEXT("Möller"), TEXT("Brückner"), TEXT("Kröger"), TEXT("Schütte"), TEXT("Hölzel"), TEXT("Marsh"), TEXT("Cole"),
		TEXT("Ferreira"), TEXT("Nakamura"), TEXT("Osei"), TEXT("Sato"), TEXT("Varga"), TEXT("Wilde"), TEXT("Abara"), TEXT("Lindgren")
	};

	/** Eingebaute Archetypen, falls appearance.json fehlt. */
	TArray<FMTAStreetArchetype> StreetBuiltinArchetypes()
	{
		struct FDef { const TCHAR* Id; const TCHAR* Name; float W[6]; float Day; float Night; };
		static const FDef Defs[] =
		{
			{ TEXT("dock_worker"), TEXT("Hafenarbeiter"), { 0.05f, 1.f, 0.1f, 0.05f, 0.3f, 0.15f }, 1.3f, 0.3f },
			{ TEXT("corp_employee"), TEXT("Konzernangestellte"), { 1.f, 0.05f, 0.05f, 0.4f, 0.f, 0.15f }, 1.4f, 0.3f },
			{ TEXT("street_vendor"), TEXT("Straßenhändler"), { 0.2f, 1.f, 0.8f, 0.05f, 0.4f, 0.9f }, 1.3f, 0.4f },
			{ TEXT("student"), TEXT("Studentin"), { 0.4f, 0.1f, 1.f, 0.3f, 0.1f, 0.4f }, 1.2f, 0.8f },
			{ TEXT("retiree"), TEXT("Rentner"), { 0.3f, 0.3f, 0.3f, 1.f, 0.2f, 0.6f }, 1.3f, 0.1f },
			{ TEXT("club_goer"), TEXT("Nachtschwärmer"), { 0.3f, 0.1f, 0.9f, 0.f, 0.3f, 1.f }, 0.05f, 2.5f },
			{ TEXT("mechanic"), TEXT("Mechanikerin"), { 0.f, 0.f, 0.f, 0.f, 1.f, 0.05f }, 1.3f, 0.3f },
			{ TEXT("tourist"), TEXT("Tourist"), { 0.5f, 0.05f, 0.3f, 0.1f, 0.f, 1.f }, 1.4f, 0.4f }
		};
		TArray<FMTAStreetArchetype> Out;
		for (const FDef& D : Defs)
		{
			FMTAStreetArchetype A;
			A.Id = FName(D.Id);
			A.Name = D.Name;
			A.Faction = FName(TEXT("none"));
			for (int32 I = 0; I < 6; ++I) { A.DistrictWeights[I] = D.W[I]; }
			A.DayWeight = D.Day;
			A.NightWeight = D.Night;
			Out.Add(A);
		}
		return Out;
	}

	/** Waehlt einen Tag aus der ersten nicht leeren Liste. */
	FName StreetPickTag(FRandomStream& Rng, const TArray<FName>& Primary, const TArray<FName>& Second, const TArray<FName>& Third)
	{
		const TArray<FName>* Lists[3] = { &Primary, &Second, &Third };
		for (const TArray<FName>* L : Lists)
		{
			if (L->Num() > 0)
			{
				return (*L)[Rng.RandRange(0, L->Num() - 1)];
			}
		}
		return NAME_None;
	}

	/** Haengt einen Block an (leere Bloecke werden uebersprungen). */
	void StreetAddEntry(TArray<FMTALivingScheduleEntry>& Out, float Start, float End, EMTAActivity Activity, FName Tag,
		const FMTALivingSpot& Spot, bool bTalkable, bool bTrading = false, bool bWander = false)
	{
		if (End <= Start + KINDA_SMALL_NUMBER)
		{
			return;
		}
		FMTALivingScheduleEntry E;
		E.StartHour = Start;
		E.EndHour = End;
		E.Activity = Activity;
		E.Tag = Tag;
		E.Spot = Spot;
		E.bTalkable = bTalkable;
		E.bTrading = bTrading;
		E.bWander = bWander;
		Out.Add(E);
	}

	/** Berufstext: aus "Dockarbeiterin / Hafenarbeiter" eine Haelfte waehlen. */
	FString StreetOccupationText(const FString& ArchetypeName, FRandomStream& Rng)
	{
		TArray<FString> Parts;
		ArchetypeName.ParseIntoArray(Parts, TEXT("/"), true);
		if (Parts.Num() == 0)
		{
			return ArchetypeName;
		}
		return Parts[Rng.RandRange(0, Parts.Num() - 1)].TrimStartAndEnd();
	}
}

namespace MTAStreetCrowd
{
	const TArray<FName>& GetDistrictIds()
	{
		static const TArray<FName> Ids =
		{
			FName(TEXT("core")), FName(TEXT("saltmarket")), FName(TEXT("lantern")),
			FName(TEXT("greenmoor")), FName(TEXT("rustline")), FName(TEXT("sunstrip"))
		};
		return Ids;
	}

	TArray<FMTAStreetArchetype> LoadArchetypes()
	{
		TArray<FMTAStreetArchetype> Out;
		const TSharedPtr<FJsonObject> Json = MTANPC::LoadJsonFile(TEXT("appearance.json"));
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(TEXT("archetypes"), Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
				if (!O.IsValid())
				{
					continue;
				}
				FMTAStreetArchetype A;
				FString S;
				if (O->TryGetStringField(TEXT("id"), S)) { A.Id = FName(*S); }
				O->TryGetStringField(TEXT("name"), A.Name);
				if (O->TryGetStringField(TEXT("faction"), S) && !S.IsEmpty()) { A.Faction = FName(*S); }
				else { A.Faction = FName(TEXT("none")); }
				double Num = 0.0;
				if (O->TryGetNumberField(TEXT("dayWeight"), Num)) { A.DayWeight = (float)Num; }
				if (O->TryGetNumberField(TEXT("nightWeight"), Num)) { A.NightWeight = (float)Num; }
				const TArray<TSharedPtr<FJsonValue>>* W = nullptr;
				if (O->TryGetArrayField(TEXT("districtWeights"), W) && W)
				{
					for (int32 I = 0; I < 6 && I < W->Num(); ++I)
					{
						A.DistrictWeights[I] = (*W)[I].IsValid() ? (float)(*W)[I]->AsNumber() : 0.f;
					}
				}
				if (!A.Id.IsNone() && !A.Name.IsEmpty())
				{
					Out.Add(A);
				}
			}
		}
		if (Out.Num() == 0)
		{
			Out = StreetBuiltinArchetypes();
		}
		return Out;
	}

	FString MakeRandomName(FRandomStream& Rng)
	{
		const int32 F = Rng.RandRange(0, (int32)UE_ARRAY_COUNT(GStreetFirstNames) - 1);
		const int32 L = Rng.RandRange(0, (int32)UE_ARRAY_COUNT(GStreetLastNames) - 1);
		return FString::Printf(TEXT("%s %s"), GStreetFirstNames[F], GStreetLastNames[L]);
	}

	const FMTALivingScheduleEntry* FindEntry(const TArray<FMTALivingScheduleEntry>& Schedule, float Hour)
	{
		const float H = FMath::Fmod(FMath::Max(0.f, Hour), 24.f);
		for (const FMTALivingScheduleEntry& E : Schedule)
		{
			if (H >= E.StartHour && H < E.EndHour)
			{
				return &E;
			}
		}
		return Schedule.Num() > 0 ? &Schedule.Last() : nullptr;
	}

	void GenerateProfiles(int32 Count, int32 Seed, const TArray<FMTAStreetArchetype>& Archetypes,
		const TMap<FName, FMTAStreetTagPool>& Pools, FSpotPicker PickSpot, TArray<FMTAStreetProfile>& OutProfiles)
	{
		OutProfiles.Reset();
		const TArray<FName>& Districts = GetDistrictIds();
		const FMTAStreetTagPool EmptyPool;

		for (int32 I = 0; I < Count; ++I)
		{
			FRandomStream R(Seed * 7919 + I * 104729 + 17);
			const int32 DIdx = I % Districts.Num();
			const FName District = Districts[DIdx];
			const FMTAStreetTagPool* FoundPool = Pools.Find(District);
			const FMTAStreetTagPool& Pool = FoundPool ? *FoundPool : EmptyPool;

			// Archetyp nach Bezirksgewicht
			const FMTAStreetArchetype* Arch = nullptr;
			if (Archetypes.Num() > 0)
			{
				float Total = 0.f;
				for (const FMTAStreetArchetype& A : Archetypes) { Total += FMath::Max(0.02f, A.DistrictWeights[DIdx]); }
				float Pick = R.FRand() * Total;
				for (const FMTAStreetArchetype& A : Archetypes)
				{
					Pick -= FMath::Max(0.02f, A.DistrictWeights[DIdx]);
					if (Pick <= 0.f) { Arch = &A; break; }
				}
				if (!Arch) { Arch = &Archetypes.Last(); }
			}

			FMTAStreetProfile P;
			P.Id = FName(*FString::Printf(TEXT("street_%02d"), I));
			P.DisplayName = MakeRandomName(R);
			P.District = District;
			P.ArchetypeId = Arch ? Arch->Id : FName(TEXT("local_commuter"));
			P.Faction = Arch ? Arch->Faction : FName(TEXT("none"));
			P.Occupation = Arch ? StreetOccupationText(Arch->Name, R) : FString(TEXT("Passant"));
			P.Talk = (float)R.FRandRange(0.2, 0.9);
			P.Temper = (float)R.FRandRange(0.05, 0.7);
			P.WalkSpeed = (float)R.FRandRange(125.0, 185.0);

			const FString ArchId = P.ArchetypeId.ToString();
			if (ArchId == TEXT("street_vendor") || ArchId == TEXT("shopkeeper") || ArchId == TEXT("chef"))
			{
				P.Routine = EMTAStreetRoutine::Vendor;
			}
			else if (ArchId == TEXT("vagrant") || ArchId == TEXT("tourist") || ArchId == TEXT("jogger") || ArchId == TEXT("beachgoer")
				|| ArchId == TEXT("surfer") || ArchId == TEXT("retiree") || ArchId == TEXT("artist") || ArchId == TEXT("child")
				|| ArchId == TEXT("parent") || ArchId == TEXT("greenmoor_resident"))
			{
				P.Routine = EMTAStreetRoutine::Wanderer;
			}
			else if (Arch && Arch->NightWeight > Arch->DayWeight * 1.3f)
			{
				P.Routine = EMTAStreetRoutine::NightOwl;
			}
			else
			{
				P.Routine = EMTAStreetRoutine::DayWorker;
			}
			if (ArchId == TEXT("jogger"))
			{
				P.WalkSpeed = 300.f;
			}

			const FMTALivingSpot Home = PickSpot(District, FCrc::MemCrc32(&I, sizeof(I), (uint32)Seed * 3u + 1u));
			const FMTALivingSpot Work = PickSpot(District, FCrc::MemCrc32(&I, sizeof(I), (uint32)Seed * 3u + 2u));
			const FMTALivingSpot NoSpot;
			const float O = (float)(R.RandRange(0, 4) - 2) * 0.5f;
			TArray<FMTALivingScheduleEntry>& S = P.Schedule;

			switch (P.Routine)
			{
			case EMTAStreetRoutine::DayWorker:
			{
				const float Wake = 6.5f + O;
				const float Off = 17.f + O * 0.5f;
				StreetAddEntry(S, 0.f, Wake, EMTAActivity::Sleep, NAME_None, Home, false);
				StreetAddEntry(S, Wake, Wake + 1.f, EMTAActivity::Commute, StreetPickTag(R, Pool.Public, Pool.Social, Pool.Food), NoSpot, true);
				StreetAddEntry(S, Wake + 1.f, 12.f, EMTAActivity::Work, NAME_None, Work, false);
				StreetAddEntry(S, 12.f, 13.f, EMTAActivity::Eat, StreetPickTag(R, Pool.Food, Pool.Social, Pool.Public), NoSpot, true);
				StreetAddEntry(S, 13.f, Off, EMTAActivity::Work, NAME_None, Work, false);
				StreetAddEntry(S, Off, 19.5f, EMTAActivity::Leisure, StreetPickTag(R, Pool.Social, Pool.Public, Pool.Food), NoSpot, true, false, R.FRand() < 0.3f);
				if (R.FRand() < 0.5f)
				{
					StreetAddEntry(S, 19.5f, 22.5f, EMTAActivity::Bar, StreetPickTag(R, Pool.Bars, Pool.Food, Pool.Social), NoSpot, true);
				}
				else
				{
					StreetAddEntry(S, 19.5f, 22.5f, EMTAActivity::Home, NAME_None, Home, false);
				}
				StreetAddEntry(S, 22.5f, 24.f, EMTAActivity::Sleep, NAME_None, Home, false);
				break;
			}
			case EMTAStreetRoutine::NightOwl:
				StreetAddEntry(S, 0.f, 3.f, EMTAActivity::Bar, StreetPickTag(R, Pool.Bars, Pool.Food, Pool.Social), NoSpot, true);
				StreetAddEntry(S, 3.f, 12.f + O, EMTAActivity::Sleep, NAME_None, Home, false);
				StreetAddEntry(S, 12.f + O, 14.f, EMTAActivity::Home, NAME_None, Home, false);
				StreetAddEntry(S, 14.f, 18.f, EMTAActivity::Leisure, StreetPickTag(R, Pool.Social, Pool.Public, Pool.Food), NoSpot, true, false, true);
				StreetAddEntry(S, 18.f, 19.f, EMTAActivity::Eat, StreetPickTag(R, Pool.Food, Pool.Social, Pool.Public), NoSpot, true);
				StreetAddEntry(S, 19.f, 21.f, EMTAActivity::Leisure, StreetPickTag(R, Pool.Public, Pool.Social, Pool.Food), NoSpot, true);
				StreetAddEntry(S, 21.f, 24.f, EMTAActivity::Bar, StreetPickTag(R, Pool.Bars, Pool.Food, Pool.Social), NoSpot, true);
				break;
			case EMTAStreetRoutine::Wanderer:
			{
				const float Wake = 7.f + O;
				StreetAddEntry(S, 0.f, Wake, EMTAActivity::Sleep, NAME_None, Home, false);
				StreetAddEntry(S, Wake, 10.5f, EMTAActivity::Leisure, StreetPickTag(R, Pool.Social, Pool.Public, Pool.Food), NoSpot, true, false, true);
				StreetAddEntry(S, 10.5f, 12.5f, EMTAActivity::Market, StreetPickTag(R, Pool.Markets, Pool.Social, Pool.Public), NoSpot, true);
				StreetAddEntry(S, 12.5f, 13.5f, EMTAActivity::Eat, StreetPickTag(R, Pool.Food, Pool.Social, Pool.Public), NoSpot, true);
				StreetAddEntry(S, 13.5f, 17.f, EMTAActivity::Leisure, StreetPickTag(R, Pool.Public, Pool.Social, Pool.Food), NoSpot, true, false, true);
				StreetAddEntry(S, 17.f, 20.f, EMTAActivity::Leisure, StreetPickTag(R, Pool.Social, Pool.Public, Pool.Food), NoSpot, true, false, true);
				StreetAddEntry(S, 20.f, 22.5f, EMTAActivity::Home, NAME_None, Home, false);
				StreetAddEntry(S, 22.5f, 24.f, EMTAActivity::Sleep, NAME_None, Home, false);
				break;
			}
			case EMTAStreetRoutine::Vendor:
			default:
			{
				const FName Stand = StreetPickTag(R, Pool.Markets, Pool.Public, Pool.Social);
				StreetAddEntry(S, 0.f, 5.f + O * 0.5f, EMTAActivity::Sleep, NAME_None, Home, false);
				StreetAddEntry(S, 5.f + O * 0.5f, 13.f, EMTAActivity::Work, Stand, Stand.IsNone() ? Work : NoSpot, true, true);
				StreetAddEntry(S, 13.f, 14.f, EMTAActivity::Eat, StreetPickTag(R, Pool.Food, Pool.Social, Pool.Public), NoSpot, true);
				StreetAddEntry(S, 14.f, 19.f, EMTAActivity::Work, Stand, Stand.IsNone() ? Work : NoSpot, true, true);
				StreetAddEntry(S, 19.f, 22.f, EMTAActivity::Bar, StreetPickTag(R, Pool.Bars, Pool.Food, Pool.Social), NoSpot, true);
				StreetAddEntry(S, 22.f, 24.f, EMTAActivity::Home, NAME_None, Home, false);
				break;
			}
			}

			// Tag-lose Bloecke ohne gueltigen Punkt bekommen die Wohnung (z. B. Bezirk ohne Orte).
			for (FMTALivingScheduleEntry& E : S)
			{
				if (E.Tag.IsNone() && !E.Spot.bValid)
				{
					E.Spot = Home;
				}
			}
			OutProfiles.Add(MoveTemp(P));
		}
	}
}
