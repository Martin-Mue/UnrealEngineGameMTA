#include "MTAInteriorBuilder.h"
#include "MTAInteriorData.h"
#include "MTAFurnishing.h"

namespace
{
	constexpr float GridCm = 50.f;
	constexpr int32 CodeNone = -1;
	constexpr int32 CodeWall = -2;
	constexpr int32 WindowBase = 100000;

	/** Wand-Kantenraster einer Etage (siehe docs/INNENRAEUME.md, Abschnitt Geometrie). */
	struct FEdgeGrid
	{
		int32 Nx = 0, Ny = 0;
		TArray<int32> Owner;   // Zellen -> Raumindex der Etage oder -1
		TArray<int32> VEdges;  // (Nx+1) * Ny, Linie x = i*G
		TArray<int32> HEdges;  // Nx * (Ny+1), Linie y = j*G

		int32 Own(int32 I, int32 J) const { return (I < 0 || J < 0 || I >= Nx || J >= Ny) ? -1 : Owner[J * Nx + I]; }
		int32& V(int32 I, int32 J) { return VEdges[J * (Nx + 1) + I]; }
		int32& H(int32 I, int32 J) { return HEdges[J * Nx + I]; }
	};

	FLinearColor Weathered(const FLinearColor& C, float Wear, FRandomStream& Rng)
	{
		// Verschleiss: Richtung Schmutzbraun/grau ziehen, leicht streuen
		const FLinearColor Dirt(0.18f, 0.16f, 0.13f);
		const float A = FMath::Clamp(Wear * 0.45f + Rng.FRandRange(-0.05f, 0.05f) * Wear, 0.f, 0.8f);
		return FLinearColor::LerpUsingHSV(C, Dirt, A * 0.5f) * (1.f - 0.25f * A);
	}

	FLinearColor FloorColorFor(FName Kind, const FMTAStyleDef& Style)
	{
		static const FName Bath(TEXT("bath")), KitchenPro(TEXT("kitchen_pro")), Surgery(TEXT("surgery")), Ward(TEXT("ward"));
		static const FName Garage(TEXT("garage")), Warehouse(TEXT("warehouse")), Workshop(TEXT("workshop")), Cell(TEXT("cell")), Stair(TEXT("stair"));
		if (Kind == Bath || Kind == KitchenPro || Kind == Surgery || Kind == Ward) return FLinearColor(0.55f, 0.58f, 0.6f);
		if (Kind == Garage || Kind == Warehouse || Kind == Workshop || Kind == Cell || Kind == Stair) return FLinearColor(0.22f, 0.22f, 0.21f);
		return Style.Floor;
	}

	void SubtractRect(const FBox2D& A, const FBox2D& Hole, TArray<FBox2D>& Out)
	{
		if (!A.Intersect(Hole)) { Out.Add(A); return; }
		const FBox2D H(FVector2D::Max(A.Min, Hole.Min), FVector2D::Min(A.Max, Hole.Max));
		if (H.Min.Y > A.Min.Y + 1.f) Out.Add(FBox2D(A.Min, FVector2D(A.Max.X, H.Min.Y)));
		if (H.Max.Y < A.Max.Y - 1.f) Out.Add(FBox2D(FVector2D(A.Min.X, H.Max.Y), A.Max));
		if (H.Min.X > A.Min.X + 1.f) Out.Add(FBox2D(FVector2D(A.Min.X, H.Min.Y), FVector2D(H.Min.X, H.Max.Y)));
		if (H.Max.X < A.Max.X - 1.f) Out.Add(FBox2D(FVector2D(H.Max.X, H.Min.Y), FVector2D(A.Max.X, H.Max.Y)));
	}

	void SubtractAll(const FBox2D& A, const TArray<FBox2D>& Holes, TArray<FBox2D>& Out)
	{
		TArray<FBox2D> Cur = { A };
		for (const FBox2D& Hole : Holes)
		{
			TArray<FBox2D> Next;
			for (const FBox2D& R : Cur) { SubtractRect(R, Hole, Next); }
			Cur = MoveTemp(Next);
		}
		Out.Append(Cur);
	}

	/** Platziertes Moebelstueck (fuer Oberflaechen-Deko und Ueberlappungstest). */
	struct FPlaced
	{
		const FMTAFurnItem* Item = nullptr;
		FTransform Xf;
		FBox2D Box;
	};

	/** Seite eines Raums: 0 = -X, 1 = +X, 2 = -Y, 3 = +Y. */
	struct FWallSpan { int32 Side = 0; float From = 0.f; float To = 0.f; bool bWindow = false; };

	class FBuildContext
	{
	public:
		FBuildContext(const FMTAInteriorTemplate& InT, const FMTAInteriorDatabase& InDb, const FMTAStyleDef& InStyle,
			int32 Seed, const FMTAInteriorBuildSettings& InSettings, FMTAInteriorBuildResult& InOut)
			: T(InT), Db(InDb), Lib(InDb.GetFurnishing()), Style(InStyle), Settings(InSettings), Out(InOut), Rng(Seed)
		{
			const FMTAInteriorDefaults& D = Db.GetDefaults();
			Thick = D.WallThickness;
			DoorH = D.DoorHeight;
			Sill = D.WindowSill;
			WinTop = D.WindowTop;
			Slab = D.SlabThickness;
		}

		void Run()
		{
			for (const FMTAFloorDef& F : T.Floors) { BuildFloor(F); }
			PlaceTemplateExtras();
			const float TotalH = T.GetFloorBaseZ(TNumericLimits<int32>::Max(), Slab);
			Out.Bounds = FBox(FVector(-400.f, -400.f, -Slab - 50.f), FVector(T.Footprint.X + 400.f, T.Footprint.Y + 400.f, TotalH + 50.f));
		}

	private:
		const FMTAInteriorTemplate& T;
		const FMTAInteriorDatabase& Db;
		const FMTAFurnishingLibrary& Lib;
		const FMTAStyleDef& Style;
		const FMTAInteriorBuildSettings& Settings;
		FMTAInteriorBuildResult& Out;
		FRandomStream Rng;
		float Thick = 20.f, DoorH = 220.f, Sill = 90.f, WinTop = 220.f, Slab = 20.f;
		int32 WindowLights = 0;
		int32 ShadowLights = 0;
		TMap<TPair<int32, FName>, int32> RoomIndexByName;

		// --- Grundbausteine ------------------------------------------------------

		void AddPrim(EMTAPrimShape Shape, EMTAPrimLayer Layer, const FVector& Center, const FVector& SizeCm, const FLinearColor& Color,
			float Emissive = 0.f, const FRotator& Rot = FRotator::ZeroRotator)
		{
			FMTAPrimInstance P;
			P.Shape = Shape;
			P.Layer = Layer;
			// Die Grundformen aus /Engine/BasicShapes sind 100 cm gross -> Skalierung = Groesse / 100
			P.Xf = FTransform(Rot, Center, SizeCm / 100.f);
			P.Color = Color;
			P.Emissive = Emissive;
			Out.Prims.Add(P);
		}

		void AddBox(const FVector& Center, const FVector& Size, const FLinearColor& Color, EMTAPrimLayer Layer = EMTAPrimLayer::Solid, float Emissive = 0.f)
		{
			AddPrim(EMTAPrimShape::Cube, Layer, Center, Size, Color, Emissive);
		}

		bool AddLight(const FMTALightSpec& L)
		{
			if (Out.Lights.Num() >= Settings.MaxLights) { return false; }
			Out.Lights.Add(L);
			return true;
		}

		/** Baut ein Rechteck entlang einer Wandlinie: bVertical = Linie x = Line. */
		FVector WallPoint(bool bVertical, float Line, float Along, float Normal, float Z) const
		{
			return bVertical ? FVector(Line + Normal, Along, Z) : FVector(Along, Line + Normal, Z);
		}

		FVector WallSize(bool bVertical, float AlongSize, float NormalSize, float ZSize) const
		{
			return bVertical ? FVector(NormalSize, AlongSize, ZSize) : FVector(AlongSize, NormalSize, ZSize);
		}

		// --- Etage -----------------------------------------------------------------

		void BuildFloor(const FMTAFloorDef& F)
		{
			const float Base = T.GetFloorBaseZ(F.Level, Slab);
			const float H = F.Height;

			FEdgeGrid G;
			G.Nx = FMath::RoundToInt(T.Footprint.X / GridCm);
			G.Ny = FMath::RoundToInt(T.Footprint.Y / GridCm);
			G.Owner.Init(-1, G.Nx * G.Ny);
			G.VEdges.Init(CodeNone, (G.Nx + 1) * G.Ny);
			G.HEdges.Init(CodeNone, G.Nx * (G.Ny + 1));

			TArray<int32> GlobalRoom;
			for (int32 R = 0; R < F.Rooms.Num(); ++R)
			{
				const FMTARoomDef& Room = F.Rooms[R];
				for (int32 J = FMath::RoundToInt(Room.Rect.Min.Y / GridCm); J < FMath::RoundToInt(Room.Rect.Max.Y / GridCm); ++J)
				{
					for (int32 I = FMath::RoundToInt(Room.Rect.Min.X / GridCm); I < FMath::RoundToInt(Room.Rect.Max.X / GridCm); ++I)
					{
						if (I >= 0 && J >= 0 && I < G.Nx && J < G.Ny) { G.Owner[J * G.Nx + I] = R; }
					}
				}
				FMTABuiltRoom BR;
				BR.Id = Room.Id;
				BR.Kind = Room.Kind;
				BR.Floor = F.Level;
				BR.Bounds = FBox(FVector(Room.Rect.Min, Base), FVector(Room.Rect.Max, Base + H));
				GlobalRoom.Add(Out.Rooms.Add(BR));
				RoomIndexByName.Add(TPair<int32, FName>(F.Level, Room.Id), GlobalRoom.Last());
			}

			// Wandkanten: dort, wo der Besitzer wechselt
			for (int32 J = 0; J < G.Ny; ++J)
				for (int32 I = 0; I <= G.Nx; ++I)
					if (G.Own(I - 1, J) != G.Own(I, J)) { G.V(I, J) = CodeWall; }
			for (int32 J = 0; J <= G.Ny; ++J)
				for (int32 I = 0; I < G.Nx; ++I)
					if (G.Own(I, J - 1) != G.Own(I, J)) { G.H(I, J) = CodeWall; }

			auto RoomIdx = [&F](FName Id) { return Id.IsNone() ? -1 : F.Rooms.IndexOfByPredicate([Id](const FMTARoomDef& R) { return R.Id == Id; }); };
			auto MarkOpening = [&G](const FVector2D& At, float Width, int32 A, int32 B, int32 Code)
			{
				const float Half = Width * 0.5f - 1.f;
				const float Fx = At.X / GridCm, Fy = At.Y / GridCm;
				if (FMath::IsNearlyEqual(Fx, FMath::RoundToFloat(Fx), 0.01f))
				{
					const int32 I = FMath::RoundToInt(Fx);
					TArray<int32> Js;
					for (int32 J = 0; J < G.Ny; ++J)
					{
						const float Cy = (J + 0.5f) * GridCm;
						const int32 L = G.Own(I - 1, J), R = G.Own(I, J);
						if (FMath::Abs(Cy - At.Y) < Half && ((L == A && R == B) || (L == B && R == A))) { Js.Add(J); }
					}
					if (Js.Num() > 0) { for (int32 J : Js) { G.V(I, J) = Code; } return; }
				}
				if (FMath::IsNearlyEqual(Fy, FMath::RoundToFloat(Fy), 0.01f))
				{
					const int32 J = FMath::RoundToInt(Fy);
					for (int32 I = 0; I < G.Nx; ++I)
					{
						const float Cx = (I + 0.5f) * GridCm;
						const int32 L = G.Own(I, J - 1), R = G.Own(I, J);
						if (FMath::Abs(Cx - At.X) < Half && ((L == A && R == B) || (L == B && R == A))) { G.H(I, J) = Code; }
					}
				}
			};
			for (int32 D = 0; D < F.Doors.Num(); ++D)
			{
				MarkOpening(F.Doors[D].At, F.Doors[D].Width, RoomIdx(F.Doors[D].RoomA), RoomIdx(F.Doors[D].RoomB), D);
			}
			for (int32 W = 0; W < F.Windows.Num(); ++W)
			{
				MarkOpening(F.Windows[W].At, F.Windows[W].Width, RoomIdx(F.Windows[W].Room), -1, WindowBase + W);
			}

			// Zusammenhaengende Kanten gleicher Art zu Segmenten verschmelzen
			for (int32 I = 0; I <= G.Nx; ++I)
			{
				int32 Start = 0;
				for (int32 J = 1; J <= G.Ny; ++J)
				{
					if (J == G.Ny || G.V(I, J) != G.V(I, Start))
					{
						const int32 Code = G.V(I, Start);
						if (Code != CodeNone)
						{
							const int32 Inside = G.Own(I, Start) >= 0 ? 1 : -1;
							EmitSegment(F, true, I * GridCm, Start * GridCm, J * GridCm, Code, Base, H, Inside,
								G.Own(I, Start) < 0 || G.Own(I - 1, Start) < 0);
						}
						Start = J;
					}
				}
			}
			for (int32 J = 0; J <= G.Ny; ++J)
			{
				int32 Start = 0;
				for (int32 I = 1; I <= G.Nx; ++I)
				{
					if (I == G.Nx || G.H(I, J) != G.H(Start, J))
					{
						const int32 Code = G.H(Start, J);
						if (Code != CodeNone)
						{
							const int32 Inside = G.Own(Start, J) >= 0 ? 1 : -1;
							EmitSegment(F, false, J * GridCm, Start * GridCm, I * GridCm, Code, Base, H, Inside,
								G.Own(Start, J) < 0 || G.Own(Start, J - 1) < 0);
						}
						Start = I;
					}
				}
			}

			// Boden und Decke je Raum (Treppenloecher ausgespart)
			TArray<FBox2D> HolesBelow, HolesAbove;
			for (const FMTAFloorDef& Other : T.Floors)
				for (const FMTAStairDef& S : Other.Stairs)
					if (S.ToLevel == F.Level) { HolesBelow.Add(S.Rect); }
			for (const FMTAStairDef& S : F.Stairs) { HolesAbove.Add(S.Rect); }

			for (int32 R = 0; R < F.Rooms.Num(); ++R)
			{
				const FMTARoomDef& Room = F.Rooms[R];
				const FLinearColor FloorC = Weathered(FloorColorFor(Room.Kind, Style), Style.Wear, Rng);
				TArray<FBox2D> Parts;
				SubtractAll(Room.Rect, HolesBelow, Parts);
				for (const FBox2D& P : Parts)
				{
					AddBox(FVector(P.GetCenter(), Base - Slab * 0.5f), FVector(P.GetSize(), Slab), FloorC);
				}
				Parts.Reset();
				SubtractAll(Room.Rect, HolesAbove, Parts);
				for (const FBox2D& P : Parts)
				{
					AddBox(FVector(P.GetCenter(), Base + H + 2.5f), FVector(P.GetSize(), 5.f), Style.Ceiling);
				}
			}

			for (const FMTAStairDef& S : F.Stairs) { BuildStair(S, Base, H); }

			// Raumlichter, Moebel, Einrichtung
			for (int32 R = 0; R < F.Rooms.Num(); ++R)
			{
				BuildRoomLights(F, F.Rooms[R], GlobalRoom[R], Base, H);
			}
			for (int32 R = 0; R < F.Rooms.Num(); ++R)
			{
				FurnishRoom(F, F.Rooms[R], GlobalRoom[R], Base, H);
			}
		}

		void EmitSegment(const FMTAFloorDef& F, bool bVertical, float Line, float From, float To, int32 Code,
			float Base, float H, int32 Inside, bool bExterior)
		{
			const float Len = To - From;
			const float Mid = (From + To) * 0.5f;
			const FLinearColor WallC = Weathered(bExterior ? Style.Wall * 0.92f : Style.Wall, Style.Wear, Rng);
			const FLinearColor FrameC(0.12f, 0.1f, 0.09f);

			if (Code == CodeWall)
			{
				// Um die Wandstaerke verlaengert, damit Ecken geschlossen sind
				AddBox(WallPoint(bVertical, Line, Mid, 0.f, Base + H * 0.5f), WallSize(bVertical, Len + Thick, Thick, H), WallC);
				return;
			}

			if (Code >= WindowBase)
			{
				AddBox(WallPoint(bVertical, Line, Mid, 0.f, Base + Sill * 0.5f), WallSize(bVertical, Len, Thick, Sill), WallC);
				if (H > WinTop) { AddBox(WallPoint(bVertical, Line, Mid, 0.f, Base + (WinTop + H) * 0.5f), WallSize(bVertical, Len, Thick, H - WinTop), WallC); }
				AddBox(WallPoint(bVertical, Line, Mid, 0.f, Base + (Sill + WinTop) * 0.5f), WallSize(bVertical, Len, 3.f, WinTop - Sill), FLinearColor(0.6f, 0.75f, 0.85f), EMTAPrimLayer::Glass);
				AddBox(WallPoint(bVertical, Line, Mid, Inside * (Thick * 0.5f + 2.f), Base + Sill + 2.f), WallSize(bVertical, Len + 10.f, 12.f, 4.f), FrameC);
				AddBox(WallPoint(bVertical, Line, Mid, 0.f, Base + (Sill + WinTop) * 0.5f), WallSize(bVertical, 5.f, Thick + 2.f, WinTop - Sill), FrameC);
				// Fensterkulisse (Himmel/Stadtschein) draussen; Farbe setzt AMTAInteriorInstance::ApplyDaylight
				AddPrim(EMTAPrimShape::Cube, EMTAPrimLayer::Backdrop,
					WallPoint(bVertical, Line, Mid, -Inside * Settings.BackdropDistance, Base + (Sill + WinTop) * 0.5f),
					WallSize(bVertical, Len + 200.f, 2.f, WinTop - Sill + 150.f), FLinearColor(0.5f, 0.6f, 0.75f), 1.f);
				if (WindowLights < Settings.MaxWindowLights)
				{
					FMTALightSpec L;
					L.bWindow = true;
					L.Pos = WallPoint(bVertical, Line, Mid, Inside * (Thick * 0.5f + 10.f), Base + (Sill + WinTop) * 0.5f);
					// Rect Lights strahlen entlang ihrer +X-Achse -> in den Raum drehen
					L.Rot = bVertical ? FRotator(0.f, Inside > 0 ? 0.f : 180.f, 0.f) : FRotator(0.f, Inside > 0 ? 90.f : -90.f, 0.f);
					L.Width = Len;
					L.Height = WinTop - Sill;
					L.Radius = 900.f;
					L.Intensity = 0.f; // wird per Tageszeit gesetzt
					if (AddLight(L)) { ++WindowLights; }
				}
				return;
			}

			// Tuer
			const FMTADoorDef& D = F.Doors[Code];
			const bool bWide = D.Width >= 300.f;
			const float OpenH = bWide ? FMath::Min(H - 30.f, 400.f) : FMath::Min(DoorH, H - 20.f);
			if (H > OpenH) { AddBox(WallPoint(bVertical, Line, Mid, 0.f, Base + (OpenH + H) * 0.5f), WallSize(bVertical, Len, Thick, H - OpenH), WallC); }
			AddBox(WallPoint(bVertical, Line, From + 3.f, 0.f, Base + OpenH * 0.5f), WallSize(bVertical, 6.f, Thick + 4.f, OpenH), FrameC);
			AddBox(WallPoint(bVertical, Line, To - 3.f, 0.f, Base + OpenH * 0.5f), WallSize(bVertical, 6.f, Thick + 4.f, OpenH), FrameC);
			AddBox(WallPoint(bVertical, Line, Mid, 0.f, Base + OpenH + 3.f), WallSize(bVertical, Len, Thick + 4.f, 6.f), FrameC);

			const FRotator Facing = bVertical ? FRotator(0.f, Inside > 0 ? 0.f : 180.f, 0.f) : FRotator(0.f, Inside > 0 ? 90.f : -90.f, 0.f);
			EMTAEntranceKind EK;
			if (D.RoomB.IsNone() && !D.Entrance.IsNone() && MTAInterior::EntranceKindFromName(D.Entrance, EK))
			{
				// Ankunftspunkt innen + Ausgangs-Interaktion an der Tuer
				const float In = bWide ? 450.f : 120.f;
				const FVector Spawn = WallPoint(bVertical, Line, Mid, Inside * In, Base + 100.f);
				if (!Out.EntranceSpawns.Contains(EK)) { Out.EntranceSpawns.Add(EK, FTransform(Facing, Spawn)); }
				FMTAInteractiveSpec X;
				X.Kind = EMTAInteractiveKind::Exit;
				X.Entrance = EK;
				X.Id = D.Entrance;
				X.Size = FVector2D(Len, OpenH);
				X.Xf = FTransform(Facing, WallPoint(bVertical, Line, Mid, Inside * 30.f, Base));
				Out.Interactives.Add(X);
				// Leuchtendes Ausgangsschild ueber der Tuer
				AddBox(WallPoint(bVertical, Line, Mid, Inside * (Thick * 0.5f + 3.f), Base + OpenH + 25.f), WallSize(bVertical, 40.f, 4.f, 15.f),
					FLinearColor(0.1f, 1.f, 0.3f), EMTAPrimLayer::Deco, 6.f);
			}
			else if (D.bPanel)
			{
				FMTAInteractiveSpec X;
				X.Kind = EMTAInteractiveKind::Door;
				X.Id = FName(*FString::Printf(TEXT("door_%d_%s_%s"), F.Level, *D.RoomA.ToString(), *D.RoomB.ToString()));
				X.KeyId = D.KeyId;
				X.HackDifficulty = D.HackDifficulty;
				X.Size = FVector2D(Len, OpenH);
				X.Xf = FTransform(Facing, WallPoint(bVertical, Line, Mid, 0.f, Base));
				Out.Interactives.Add(X);
			}
		}

		void BuildStair(const FMTAStairDef& S, float Base, float H)
		{
			const float Rise = H + Slab;
			const int32 Steps = FMath::Max(2, FMath::CeilToInt(Rise / 18.f));
			const FVector2D Size = S.Rect.GetSize();
			const float Run = S.bAlongX ? Size.X : Size.Y;
			const float StepD = Run / Steps;
			const FLinearColor StepC = Weathered(FLinearColor(0.35f, 0.33f, 0.3f), Style.Wear, Rng);
			for (int32 K = 0; K < Steps; ++K)
			{
				const float Top = Rise * (K + 1) / Steps;
				const float A = (K + 0.5f) * StepD;
				const FVector C = S.bAlongX ? FVector(S.Rect.Min.X + A, S.Rect.GetCenter().Y, Base + Top * 0.5f)
					: FVector(S.Rect.GetCenter().X, S.Rect.Min.Y + A, Base + Top * 0.5f);
				const FVector Sz = S.bAlongX ? FVector(StepD, Size.Y, Top) : FVector(Size.X, StepD, Top);
				AddBox(C, Sz, StepC);
			}
			// Gelaender auf der offenen Seite
			const float RailZ = Base + Rise * 0.5f + 90.f;
			const FVector RailC = S.bAlongX ? FVector(S.Rect.GetCenter().X, S.Rect.Max.Y - 3.f, RailZ) : FVector(S.Rect.Max.X - 3.f, S.Rect.GetCenter().Y, RailZ);
			const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(Rise, Run));
			const FRotator RailRot = S.bAlongX ? FRotator(Pitch, 0.f, 0.f) : FRotator(Pitch, 90.f, 0.f);
			AddPrim(EMTAPrimShape::Cube, EMTAPrimLayer::Deco, RailC, FVector(FMath::Sqrt(Run * Run + Rise * Rise), 5.f, 5.f), FLinearColor(0.1f, 0.1f, 0.1f), 0.f, RailRot);
		}

		void BuildRoomLights(const FMTAFloorDef& F, const FMTARoomDef& Room, int32 RoomIndex, float Base, float H)
		{
			const FVector2D Sz = Room.Rect.GetSize();
			const float AreaM2 = Sz.X * Sz.Y / 10000.f;
			const int32 Count = FMath::Clamp(FMath::RoundToInt(AreaM2 / 10.f * Style.LightsPer10m2), 1, 4);
			const bool bAlongX = Sz.X >= Sz.Y;
			for (int32 K = 0; K < Count; ++K)
			{
				const float A = (K + 0.5f) / Count;
				const FVector2D P = bAlongX ? FVector2D(Room.Rect.Min.X + Sz.X * A, Room.Rect.GetCenter().Y) : FVector2D(Room.Rect.GetCenter().X, Room.Rect.Min.Y + Sz.Y * A);
				FMTALightSpec L;
				L.Pos = FVector(P, Base + H - 15.f);
				L.Color = Style.LightColor;
				L.Intensity = Style.LightIntensity * Rng.FRandRange(0.85f, 1.1f);
				L.Radius = FMath::Clamp(FMath::Max(Sz.X / Count, Sz.Y) * 0.9f + 150.f, 450.f, 1500.f);
				L.RoomIndex = RoomIndex;
				L.bFlicker = Rng.FRand() < Style.Flicker;
				L.bShadows = ShadowLights < Settings.MaxShadowLights && AreaM2 > 25.f;
				if (AddLight(L))
				{
					ShadowLights += L.bShadows ? 1 : 0;
					// Sichtbarer Leuchtkoerper
					AddPrim(EMTAPrimShape::Cylinder, EMTAPrimLayer::Deco, FVector(P, Base + H - 4.f), FVector(35.f, 35.f, 4.f), Style.LightColor, L.bFlicker ? 2.f : 4.f);
				}
			}
		}

		// --- Einrichtung ---------------------------------------------------------

		struct FRoomState
		{
			FBox2D Usable;
			TArray<FBox2D> Blocked;
			TArray<FWallSpan> Spans;    // Tueren/Fenster je Wandseite
			TArray<FWallSpan> DecorUsed;
			TArray<FPlaced> Placed;
			float Base = 0.f, H = 300.f;
			int32 RoomIndex = INDEX_NONE;
			int32 Floor = 0;
			FName RoomId;
		};

		static int32 SideOf(const FBox2D& Rect, const FVector2D& At, float& OutAlong)
		{
			if (FMath::IsNearlyEqual(At.X, Rect.Min.X, 2.f)) { OutAlong = At.Y; return 0; }
			if (FMath::IsNearlyEqual(At.X, Rect.Max.X, 2.f)) { OutAlong = At.Y; return 1; }
			if (FMath::IsNearlyEqual(At.Y, Rect.Min.Y, 2.f)) { OutAlong = At.X; return 2; }
			OutAlong = At.X;
			return 3;
		}

		static FVector2D InwardOfSide(int32 Side)
		{
			switch (Side) { case 0: return FVector2D(1, 0); case 1: return FVector2D(-1, 0); case 2: return FVector2D(0, 1); default: return FVector2D(0, -1); }
		}

		void InitRoomState(const FMTAFloorDef& F, const FMTARoomDef& Room, int32 RoomIndex, float Base, float H, FRoomState& S)
		{
			S.Usable = FBox2D(Room.Rect.Min + FVector2D(Thick * 0.5f + 5.f), Room.Rect.Max - FVector2D(Thick * 0.5f + 5.f));
			S.Base = Base;
			S.H = H;
			S.RoomIndex = RoomIndex;
			S.Floor = F.Level;
			S.RoomId = Room.Id;
			for (const FMTADoorDef& D : F.Doors)
			{
				if (D.RoomA != Room.Id && D.RoomB != Room.Id) { continue; }
				float Along = 0.f;
				const int32 Side = SideOf(Room.Rect, D.At, Along);
				S.Spans.Add({ Side, Along - D.Width * 0.5f - 20.f, Along + D.Width * 0.5f + 20.f, false });
				// Freiraum vor der Tuer (130 cm tief)
				const FVector2D In = InwardOfSide(Side);
				const FVector2D Perp(FMath::Abs(In.Y), FMath::Abs(In.X));
				const FVector2D C = D.At + In * 65.f;
				const FVector2D Ext = Perp * (D.Width * 0.5f + 25.f) + FVector2D(FMath::Abs(In.X), FMath::Abs(In.Y)) * 65.f;
				S.Blocked.Add(FBox2D(C - Ext, C + Ext));
			}
			for (const FMTAWindowDef& W : F.Windows)
			{
				if (W.Room != Room.Id) { continue; }
				float Along = 0.f;
				const int32 Side = SideOf(Room.Rect, W.At, Along);
				S.Spans.Add({ Side, Along - W.Width * 0.5f - 10.f, Along + W.Width * 0.5f + 10.f, true });
			}
			for (const FMTAFloorDef& Other : T.Floors)
			{
				for (const FMTAStairDef& St : Other.Stairs)
				{
					if ((Other.Level == F.Level && St.Room == Room.Id) || St.ToLevel == F.Level) { S.Blocked.Add(St.Rect.ExpandBy(100.f)); }
				}
			}
		}

		bool SpanFree(const FRoomState& S, int32 Side, float From, float To, bool bTall, bool bDecor) const
		{
			for (const FWallSpan& Sp : S.Spans)
			{
				if (Sp.Side != Side || Sp.To <= From || Sp.From >= To) { continue; }
				if (!Sp.bWindow || bTall || bDecor) { return false; }
			}
			if (bDecor)
			{
				for (const FWallSpan& Sp : S.DecorUsed)
				{
					if (Sp.Side == Side && Sp.To > From && Sp.From < To) { return false; }
				}
			}
			return true;
		}

		bool BoxFree(const FRoomState& S, const FBox2D& B) const
		{
			if (B.Min.X < S.Usable.Min.X - 0.5f || B.Min.Y < S.Usable.Min.Y - 0.5f || B.Max.X > S.Usable.Max.X + 0.5f || B.Max.Y > S.Usable.Max.Y + 0.5f) { return false; }
			const FBox2D Shrunk(B.Min + FVector2D(2.f), B.Max - FVector2D(2.f));
			for (const FBox2D& O : S.Blocked) { if (Shrunk.Intersect(O)) return false; }
			for (const FPlaced& P : S.Placed) { if (Shrunk.Intersect(P.Box)) return false; }
			return true;
		}

		/** Liefert Transform (Ursprung am Boden) und 2D-Box fuer ein an Seite Side stehendes Objekt. */
		void WallPose(const FRoomState& S, const FMTAFurnItem& It, int32 Side, float Along, float Inset, FTransform& OutXf, FBox2D& OutBox) const
		{
			const float Depth = It.Footprint.X, Width = It.Footprint.Y;
			FVector2D P;
			float Yaw = 0.f;
			switch (Side)
			{
			case 0: P = FVector2D(S.Usable.Min.X + Depth * 0.5f + Inset, Along); Yaw = 0.f; break;
			case 1: P = FVector2D(S.Usable.Max.X - Depth * 0.5f - Inset, Along); Yaw = 180.f; break;
			case 2: P = FVector2D(Along, S.Usable.Min.Y + Depth * 0.5f + Inset); Yaw = 90.f; break;
			default: P = FVector2D(Along, S.Usable.Max.Y - Depth * 0.5f - Inset); Yaw = -90.f; break;
			}
			const FVector2D Ext = (Side <= 1) ? FVector2D(Depth, Width) * 0.5f : FVector2D(Width, Depth) * 0.5f;
			OutXf = FTransform(FRotator(0.f, Yaw, 0.f), FVector(P, S.Base));
			OutBox = FBox2D(P - Ext, P + Ext);
		}

		void SideRange(const FRoomState& S, int32 Side, float& OutMin, float& OutMax) const
		{
			if (Side <= 1) { OutMin = S.Usable.Min.Y; OutMax = S.Usable.Max.Y; }
			else { OutMin = S.Usable.Min.X; OutMax = S.Usable.Max.X; }
		}

		bool TryPlace(FRoomState& S, const FMTAFurnItem& It, EMTAFurnPlace Place)
		{
			const float Depth = It.Footprint.X, Width = It.Footprint.Y;
			const bool bTall = It.Height > Sill - 5.f;
			for (int32 Attempt = 0; Attempt < 14; ++Attempt)
			{
				FTransform Xf;
				FBox2D Box(ForceInit);
				switch (Place)
				{
				case EMTAFurnPlace::Wall:
				case EMTAFurnPlace::Corner:
				{
					const int32 Side = Rng.RandRange(0, 3);
					float Lo, Hi;
					SideRange(S, Side, Lo, Hi);
					if (Hi - Lo < Width) { continue; }
					const float Along = Place == EMTAFurnPlace::Corner ? (Rng.FRand() < 0.5f ? Lo + Width * 0.5f : Hi - Width * 0.5f)
						: Rng.FRandRange(Lo + Width * 0.5f, Hi - Width * 0.5f);
					if (!SpanFree(S, Side, Along - Width * 0.5f, Along + Width * 0.5f, bTall, false)) { continue; }
					WallPose(S, It, Side, Along, 0.f, Xf, Box);
					break;
				}
				case EMTAFurnPlace::Center:
				case EMTAFurnPlace::Scatter:
				{
					const int32 Quarter = Rng.RandRange(0, 3);
					float Yaw = Quarter * 90.f;
					if (Place == EMTAFurnPlace::Scatter && It.Category == FName(TEXT("trash"))) { Yaw = Rng.FRandRange(0.f, 360.f); }
					const FVector2D Ext = (Quarter % 2 == 0) ? FVector2D(Depth, Width) * 0.5f : FVector2D(Width, Depth) * 0.5f;
					const FVector2D USz = S.Usable.GetSize();
					if (USz.X < Ext.X * 2.f || USz.Y < Ext.Y * 2.f) { continue; }
					FVector2D P;
					if (Place == EMTAFurnPlace::Center)
					{
						const FVector2D Jit = FVector2D(Rng.FRandRange(-0.25f, 0.25f) * FMath::Max(0.f, USz.X - Ext.X * 2.f), Rng.FRandRange(-0.25f, 0.25f) * FMath::Max(0.f, USz.Y - Ext.Y * 2.f));
						P = S.Usable.GetCenter() + Jit;
					}
					else
					{
						P = FVector2D(Rng.FRandRange(S.Usable.Min.X + Ext.X, S.Usable.Max.X - Ext.X), Rng.FRandRange(S.Usable.Min.Y + Ext.Y, S.Usable.Max.Y - Ext.Y));
					}
					Xf = FTransform(FRotator(0.f, Yaw, 0.f), FVector(P, S.Base));
					Box = FBox2D(P - Ext, P + Ext);
					break;
				}
				default:
					return false;
				}
				if (!BoxFree(S, Box)) { continue; }
				Commit(S, It, Xf, Box, true);
				return true;
			}
			return false;
		}

		bool TryPlaceDecor(FRoomState& S, const FMTAFurnItem& It)
		{
			const float Width = It.Footprint.Y;
			for (int32 Attempt = 0; Attempt < 10; ++Attempt)
			{
				const int32 Side = Rng.RandRange(0, 3);
				float Lo, Hi;
				SideRange(S, Side, Lo, Hi);
				if (Hi - Lo < Width + 20.f) { continue; }
				const float Along = Rng.FRandRange(Lo + Width * 0.5f + 10.f, Hi - Width * 0.5f - 10.f);
				if (!SpanFree(S, Side, Along - Width * 0.5f, Along + Width * 0.5f, true, true)) { continue; }
				FTransform Xf;
				FBox2D Box;
				WallPose(S, It, Side, Along, -5.f, Xf, Box);
				S.DecorUsed.Add({ Side, Along - Width * 0.5f - 10.f, Along + Width * 0.5f + 10.f, false });
				Commit(S, It, Xf, Box, false);
				return true;
			}
			return false;
		}

		bool TryPlaceCeiling(FRoomState& S, const FMTAFurnItem& It)
		{
			const FVector2D Ext = It.Footprint * 0.5f;
			for (int32 Attempt = 0; Attempt < 8; ++Attempt)
			{
				const FVector2D USz = S.Usable.GetSize();
				if (USz.X < Ext.X * 2.f || USz.Y < Ext.Y * 2.f) { return false; }
				const FVector2D P(Rng.FRandRange(S.Usable.Min.X + Ext.X, S.Usable.Max.X - Ext.X), Rng.FRandRange(S.Usable.Min.Y + Ext.Y, S.Usable.Max.Y - Ext.Y));
				const FBox2D Box(P - Ext, P + Ext);
				bool bStair = false;
				for (const FBox2D& B : S.Blocked) { bStair |= Box.Intersect(B) && B.GetSize().X > 250.f && B.GetSize().Y > 250.f; }
				if (bStair) { continue; }
				Commit(S, It, FTransform(FRotator(0.f, Rng.RandRange(0, 3) * 90.f, 0.f), FVector(P, S.Base + S.H)), Box, false);
				return true;
			}
			return false;
		}

		bool TryPlaceSurface(FRoomState& S, const FMTAFurnItem& It)
		{
			static const TSet<FName> Surfaces = { FName(TEXT("table")), FName(TEXT("desk")), FName(TEXT("shop")), FName(TEXT("bar")), FName(TEXT("kitchen")), FName(TEXT("work")), FName(TEXT("hall")) };
			TArray<const FPlaced*, TInlineAllocator<8>> Hosts;
			for (const FPlaced& P : S.Placed)
			{
				if (P.Item && Surfaces.Contains(P.Item->Category) && P.Item->Mount == EMTAFurnMount::Floor && P.Item->Height > 40.f && P.Item->Height < 130.f) { Hosts.Add(&P); }
			}
			if (Hosts.Num() == 0) { return false; }
			const FPlaced& H = *Hosts[Rng.RandRange(0, Hosts.Num() - 1)];
			const FVector Local(Rng.FRandRange(-0.25f, 0.25f) * H.Item->Footprint.X, Rng.FRandRange(-0.3f, 0.3f) * H.Item->Footprint.Y, H.Item->Height);
			const FVector World = H.Xf.TransformPosition(Local);
			const FTransform Xf(FRotator(0.f, H.Xf.Rotator().Yaw + Rng.FRandRange(-25.f, 25.f), 0.f), World);
			Commit(S, It, Xf, FBox2D(FVector2D(World) - FVector2D(1.f), FVector2D(World) + FVector2D(1.f)), false);
			return true;
		}

		/** Schreibt Teile, Licht, NPC-Slot und Interaktion eines Objekts in das Ergebnis. */
		void Commit(FRoomState& S, const FMTAFurnItem& It, const FTransform& Xf, const FBox2D& Box, bool bBlocksFloor)
		{
			if (bBlocksFloor) { S.Placed.Add({ &It, Xf, Box }); }
			else if (It.Mount == EMTAFurnMount::Floor) { S.Placed.Add({ &It, Xf, FBox2D(Box.GetCenter(), Box.GetCenter()) }); }

			const bool bDirty = Style.Wear > 0.6f && It.Category == FName(TEXT("clutter"));
			const FRotator Tilt = bDirty ? FRotator(Rng.FRandRange(-4.f, 4.f), 0.f, Rng.FRandRange(-4.f, 4.f)) : FRotator::ZeroRotator;
			const FTransform ItemXf = FTransform(Tilt) * Xf;
			for (const FMTAFurnPart& Part : It.Parts)
			{
				FMTAPrimInstance P;
				P.Shape = Part.Shape;
				// Kleine Teile ohne Kollision, damit Figuren nicht an Deko haengen bleiben
				const bool bSmall = Part.Size.GetMin() < 6.f && Part.Size.Z < 30.f;
				P.Layer = (It.bCollision && !bSmall) ? EMTAPrimLayer::Solid : EMTAPrimLayer::Deco;
				P.Xf = FTransform(Part.Rot, Part.Pos, Part.Size / 100.f) * ItemXf;
				P.Color = Part.Emissive > 0.f ? Part.Color : Weathered(Part.Color, Style.Wear, Rng);
				P.Emissive = Part.Emissive;
				Out.Prims.Add(P);
			}
			if (It.Light.bValid)
			{
				FMTALightSpec L;
				L.Pos = ItemXf.TransformPosition(It.Light.Pos);
				L.Color = It.Light.Color;
				L.Intensity = It.Light.Intensity;
				L.Radius = It.Light.Radius;
				L.RoomIndex = S.RoomIndex;
				L.bFlicker = It.Category == FName(TEXT("neon")) && Rng.FRand() < Style.Flicker * 1.5f;
				AddLight(L);
			}
			if (!It.Activity.IsNone())
			{
				static const FName Bed(TEXT("bed")), Seat(TEXT("seat"));
				const bool bOn = It.Category == Bed || It.Category == Seat;
				const FVector Front = Xf.GetRotation().GetForwardVector();
				FMTANPCSlot Slot;
				Slot.Transform = bOn ? Xf : FTransform((-Front).Rotation(), Xf.GetLocation() + Front * (It.Footprint.X * 0.5f + 55.f));
				Slot.Activity = It.Activity;
				Slot.Role = NAME_None; // wird vom Instanz-Actor mit der Rolle des Raumtyps gefuellt
				Slot.Chance = 0.5f;
				Slot.RoomId = S.RoomId;
				Slot.Floor = S.Floor;
				Out.NpcSlots.Add(Slot);
			}
			if (!It.Interactive.IsNone())
			{
				FMTAInteractiveSpec X;
				X.RoomIndex = S.RoomIndex;
				X.Xf = Xf;
				X.Size = FVector2D(FMath::Max(It.Footprint.X, It.Footprint.Y), FMath::Max(It.Height, 60.f));
				X.Id = FName(*FString::Printf(TEXT("%s_%d"), *It.Id.ToString(), Out.Interactives.Num()));
				if (It.Interactive == FName(TEXT("hiding"))) { X.Kind = EMTAInteractiveKind::HidingSpot; }
				else if (It.Interactive == FName(TEXT("switch"))) { X.Kind = EMTAInteractiveKind::Switch; }
				else { X.Kind = EMTAInteractiveKind::Terminal; X.HackDifficulty = FMath::Max(1, T.Access.HackDifficulty - 1); }
				Out.Interactives.Add(X);
			}
		}

		void PlaceItem(FRoomState& S, const FMTAFurnItem& It, EMTAFurnPlace Place)
		{
			if (It.Mount == EMTAFurnMount::Wall || Place == EMTAFurnPlace::WallDecor) { TryPlaceDecor(S, It); }
			else if (It.Mount == EMTAFurnMount::Ceiling || Place == EMTAFurnPlace::Ceiling) { TryPlaceCeiling(S, It); }
			else if (It.Mount == EMTAFurnMount::Surface || Place == EMTAFurnPlace::Surface) { TryPlaceSurface(S, It); }
			else { TryPlace(S, It, Place); }
		}

		void FurnishRoom(const FMTAFloorDef& F, const FMTARoomDef& Room, int32 RoomIndex, float Base, float H)
		{
			FRoomState S;
			InitRoomState(F, Room, RoomIndex, Base, H, S);
			if (S.Usable.GetSize().X < 60.f || S.Usable.GetSize().Y < 60.f) { return; }

			// 1) Handgesetzte Props der Vorlage zuerst (blockieren Flaeche)
			for (const FMTAPropDef& P : T.Props)
			{
				if (P.Floor != F.Level || P.Room != Room.Id) { continue; }
				if (const FMTAFurnItem* It = Lib.FindItem(P.Item))
				{
					const float Z = It->Mount == EMTAFurnMount::Ceiling ? Base + H : Base + P.At.Z;
					const FVector2D Ext = It->Footprint * 0.5f;
					Commit(S, *It, FTransform(FRotator(0.f, P.Yaw, 0.f), FVector(P.At.X, P.At.Y, Z)),
						FBox2D(FVector2D(P.At) - Ext, FVector2D(P.At) + Ext), It->Mount == EMTAFurnMount::Floor);
				}
			}

			// 2) Regeln der Raumart
			const float AreaFactor = FMath::Clamp(S.Usable.GetArea() / (16.f * 10000.f), 0.6f, 3.f);
			if (const TArray<FMTAFurnRule>* Rules = Lib.FindRules(Room.Kind))
			{
				for (const FMTAFurnRule& R : *Rules)
				{
					if (Rng.FRand() > R.Chance) { continue; }
					int32 N = Rng.RandRange(R.Count.X, R.Count.Y);
					if (R.bClutter) { N = FMath::RoundToInt(N * Style.Clutter); }
					if (R.Count.Y > 2) { N = FMath::RoundToInt(N * FMath::Sqrt(AreaFactor)); }
					for (int32 K = 0; K < N; ++K)
					{
						if (const FMTAFurnItem* It = Lib.PickItem(R.Pick, Style, Rng)) { PlaceItem(S, *It, R.Place); }
					}
				}
			}

			// 3) Bezirksstimmung: Pflanzen, Neon, Muell
			static const TArray<FName> PlantPick = { TEXT("plant_potted"), TEXT("plant_palm"), TEXT("plant_ficus"), TEXT("plant_dead"), TEXT("plant_hanging") };
			static const TArray<FName> NeonPick = { TEXT("neon_open"), TEXT("neon_bar"), TEXT("neon_arrow"), TEXT("neon_heart"), TEXT("neon_cocktail"), TEXT("neon_kestrel"), TEXT("neon_strip"), TEXT("neon_24h") };
			const bool bWet = Room.Kind == FName(TEXT("bath")) || Room.Kind == FName(TEXT("cell"));
			if (!bWet && Rng.FRand() < Style.Plants * 0.5f)
			{
				if (const FMTAFurnItem* It = Lib.PickItem(PlantPick, Style, Rng)) { PlaceItem(S, *It, EMTAFurnPlace::Corner); }
			}
			if (!bWet && Rng.FRand() < Style.Neon * 0.6f)
			{
				if (const FMTAFurnItem* It = Lib.PickItem(NeonPick, Style, Rng)) { PlaceItem(S, *It, EMTAFurnPlace::WallDecor); }
			}
			TArray<const FMTAFurnItem*> Trash;
			Lib.GetTrashItems(Style, Trash);
			if (Trash.Num() > 0)
			{
				const int32 N = FMath::RoundToInt(Rng.RandRange(Style.Trash.X, Style.Trash.Y) * AreaFactor * 0.5f);
				for (int32 K = 0; K < N; ++K) { PlaceItem(S, *Trash[Rng.RandRange(0, Trash.Num() - 1)], EMTAFurnPlace::Scatter); }
			}
		}

		// --- Vorlagen-Extras -----------------------------------------------------

		FVector PointToLocal(const FMTAPointDef& P, float DefaultZ) const
		{
			const float Base = T.GetFloorBaseZ(P.Floor, Slab);
			return FVector(P.At.X, P.At.Y, Base + (P.At.Z > 0.f ? P.At.Z : DefaultZ));
		}

		int32 RoomIndexOf(int32 Floor, FName Room) const
		{
			const int32* I = RoomIndexByName.Find(TPair<int32, FName>(Floor, Room));
			return I ? *I : INDEX_NONE;
		}

		void PlaceTemplateExtras()
		{
			for (const FMTASecretDef& Sd : T.Secrets)
			{
				FMTAInteractiveSpec X;
				X.Kind = EMTAInteractiveKind::Secret;
				X.Id = Sd.Id;
				X.SecretId = Sd.Id;
				X.SecretKind = Sd.Kind;
				X.bHidden = Sd.bHidden;
				X.RoomIndex = RoomIndexOf(Sd.Floor, Sd.Room);
				X.Size = FVector2D(60.f, 60.f);
				X.Xf = FTransform(FRotator(0.f, Sd.Yaw, 0.f), PointToLocal(Sd, 35.f));
				Out.Interactives.Add(X);
			}
			for (const FMTAInteractiveDef& I : T.Interactives)
			{
				FMTAInteractiveSpec X;
				X.Kind = I.Kind;
				X.Id = I.Id;
				X.Links = I.Links;
				X.KeyId = I.KeyId;
				X.HackDifficulty = I.HackDifficulty;
				X.RoomIndex = RoomIndexOf(I.Floor, I.Room);
				X.Size = FVector2D(80.f, 150.f);
				X.Xf = FTransform(FRotator(0.f, I.Yaw, 0.f), PointToLocal(I, 0.f));
				Out.Interactives.Add(X);
				if (I.Kind == EMTAInteractiveKind::Terminal)
				{
					// Sichtbares Terminal als Platzhalter
					if (const FMTAFurnItem* It = Lib.FindItem(TEXT("terminal_public")))
					{
						for (const FMTAFurnPart& Part : It->Parts)
						{
							FMTAPrimInstance P;
							P.Shape = Part.Shape;
							P.Layer = EMTAPrimLayer::Solid;
							P.Xf = FTransform(Part.Rot, Part.Pos, Part.Size / 100.f) * X.Xf;
							P.Color = Part.Color;
							P.Emissive = Part.Emissive;
							Out.Prims.Add(P);
						}
					}
				}
			}
			for (const FMTANPCSlotDef& N : T.NpcSlots)
			{
				FMTANPCSlot Slot;
				Slot.Transform = FTransform(FRotator(0.f, N.Yaw, 0.f), PointToLocal(N, 0.f));
				Slot.Activity = N.Activity;
				Slot.Role = N.Role;
				Slot.FromHour = N.FromHour;
				Slot.ToHour = N.ToHour;
				Slot.Chance = N.Chance;
				Slot.RoomId = N.Room;
				Slot.Floor = N.Floor;
				Out.NpcSlots.Add(Slot);
			}
		}
	};
}

float FMTAInteriorBuildResult::EstimateCostKB() const
{
	// Faustwerte: ISM-Instanz ~0,2 KB (Transform + Custom Data + Render-/Physikdaten),
	// Licht ~4 KB, interaktiver Actor ~12 KB, Grundkosten Actor + Komponenten ~64 KB
	return 64.f + Prims.Num() * 0.2f + Lights.Num() * 4.f + Interactives.Num() * 12.f;
}

bool FMTAInteriorBuilder::Build(const FMTAInteriorTemplate& Template, const FMTAInteriorDatabase& Db, const FMTAStyleDef& Style,
	int32 Seed, const FMTAInteriorBuildSettings& Settings, FMTAInteriorBuildResult& Out)
{
	Out = FMTAInteriorBuildResult();
	if (Template.Floors.Num() == 0 || Template.Footprint.X <= 0.f || Template.Footprint.Y <= 0.f) { return false; }
	FBuildContext Ctx(Template, Db, Style, Seed, Settings, Out);
	Ctx.Run();
	return Out.Prims.Num() > 0;
}
