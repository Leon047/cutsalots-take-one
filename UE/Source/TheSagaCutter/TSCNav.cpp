// The Saga Cutter - game jam project.

#include "TSCNav.h"

#include "Algo/Reverse.h"

namespace
{
	/** Open-list entry for A* / Dijkstra. */
	struct FNavNode
	{
		int32 Idx = 0;
		float F = 0.f;
		float H = 0.f;
	};
	struct FNavNodeLess
	{
		bool operator()(const FNavNode& A, const FNavNode& B) const
		{
			return A.F < B.F || (A.F == B.F && A.H < B.H);
		}
	};

	constexpr int32 DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	constexpr int32 DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

	FVector To3(const FVector2D& P) { return FVector(P.X, P.Y, 0.f); }
	FVector2D To2(const FVector& P) { return FVector2D(P.X, P.Y); }
}

// ----- setup and sources -----

void FTSCNavGrid::Init(int32 InGW, int32 InGH, float InCell)
{
	GW = InGW;
	GH = InGH;
	Cell = InCell;
	CW = GW + PadW + PadE;
	CH = GH + PadN + PadS;
	NW = CW * SUB;
	NH = CH * SUB;
	NavCell = Cell / SUB;
	Origin = FVector2D((-(GW / 2) - PadW) * Cell, (-(GH / 2) - PadN) * Cell);

	Coarse.Init(0, CW * CH);
	TreeFlag.Init(0, CW * CH);
	TreePos.Init(FVector2D::ZeroVector, CW * CH);
	Props.Reset();
	bPropsActive = true;

	for (int32 L = 0; L < TSCNavLayer::Num; ++L) { Mask[L].Init(0, NW * NH); }
	PropMask.Init(0, NW * NH);
	ScratchG.Reset();
	ScratchParent.Reset();
	ScratchVisit.Reset();
	ScratchClosed.Reset();
	ScratchGen = 0;
	bDirty = true;
}

void FTSCNavGrid::SetCoarse(int32 Cx, int32 Cy, bool bBlock)
{
	const int32 X = Cx + PadW, Y = Cy + PadN;
	if (X < 0 || Y < 0 || X >= CW || Y >= CH) { return; }
	Coarse[Y * CW + X] = bBlock ? 1 : 0;
	TreeFlag[Y * CW + X] = 0;
	bDirty = true;
}

void FTSCNavGrid::SetTree(int32 Cx, int32 Cy, const FVector& TrunkPos)
{
	const int32 X = Cx + PadW, Y = Cy + PadN;
	if (X < 0 || Y < 0 || X >= CW || Y >= CH) { return; }
	TreeFlag[Y * CW + X] = 1;
	TreePos[Y * CW + X] = To2(TrunkPos);
	bDirty = true;
}

int32 FTSCNavGrid::AddPropBox(const FVector2D& Center, const FVector2D& AxisX, const FVector2D& HalfExtent)
{
	FProp& P = Props.AddDefaulted_GetRef();
	P.Center = Center;
	P.AxisX = AxisX;
	P.Half = HalfExtent;
	P.bDisc = false;
	bDirty = true;
	return Props.Num() - 1;
}

int32 FTSCNavGrid::AddPropDisc(const FVector2D& Center, float Radius)
{
	FProp& P = Props.AddDefaulted_GetRef();
	P.Center = Center;
	P.Radius = Radius;
	P.bDisc = true;
	bDirty = true;
	return Props.Num() - 1;
}

void FTSCNavGrid::SetPropEnabled(int32 Idx, bool bEnabled)
{
	if (!Props.IsValidIndex(Idx)) { return; }
	Props[Idx].bEnabled = bEnabled;
	bDirty = true;
}

void FTSCNavGrid::SetPropsActive(bool bActive)
{
	bPropsActive = bActive;
	bDirty = true;
}

int32 FTSCNavGrid::NumProps() const { return Props.Num(); }
FVector2D FTSCNavGrid::PropCenter(int32 Idx) const { return Props.IsValidIndex(Idx) ? Props[Idx].Center : FVector2D::ZeroVector; }
bool FTSCNavGrid::IsPropEnabled(int32 Idx) const { return Props.IsValidIndex(Idx) && Props[Idx].bEnabled; }

// ----- bake -----

void FTSCNavGrid::MarkCell(TArray<uint8>& InMask, int32 X, int32 Y) const
{
	if (InDomain(X, Y)) { InMask[Y * NW + X] = 1; }
}

void FTSCNavGrid::Bake() const
{
	if (!bDirty) { return; }
	bDirty = false;
	for (int32 L = 0; L < TSCNavLayer::Num; ++L) { Mask[L].Init(0, NW * NH); }
	PropMask.Init(0, NW * NH);

	// Coarse cells: People blocks every one; Wildlife skips tree cells (their trunks follow).
	for (int32 Cy = 0; Cy < CH; ++Cy)
	{
		for (int32 Cx = 0; Cx < CW; ++Cx)
		{
			const int32 C = Cy * CW + Cx;
			if (!Coarse[C]) { continue; }
			const bool bTree = TreeFlag[C] != 0;
			for (int32 Sy = 0; Sy < SUB; ++Sy)
			{
				for (int32 Sx = 0; Sx < SUB; ++Sx)
				{
					const int32 I = (Cy * SUB + Sy) * NW + (Cx * SUB + Sx);
					Mask[TSCNavLayer::People][I] = 1;
					if (!bTree) { Mask[TSCNavLayer::Wildlife][I] = 1; }
				}
			}
		}
	}

	// Trunk discs on the Wildlife layer.
	for (int32 C = 0; C < CW * CH; ++C)
	{
		if (!TreeFlag[C]) { continue; }
		const FVector2D T = TreePos[C];
		const int32 X0 = FMath::FloorToInt((T.X - TrunkRadius - Origin.X) / NavCell);
		const int32 X1 = FMath::FloorToInt((T.X + TrunkRadius - Origin.X) / NavCell);
		const int32 Y0 = FMath::FloorToInt((T.Y - TrunkRadius - Origin.Y) / NavCell);
		const int32 Y1 = FMath::FloorToInt((T.Y + TrunkRadius - Origin.Y) / NavCell);
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				if (FVector2D::DistSquared(To2(CellCenter(X, Y)), T) <= TrunkRadius * TrunkRadius) { MarkCell(Mask[TSCNavLayer::Wildlife], X, Y); }
			}
		}
		const FIntPoint TC = CellOf(To3(T));
		MarkCell(Mask[TSCNavLayer::Wildlife], TC.X, TC.Y);
	}

	// Level props block both layers.
	if (bPropsActive)
	{
		for (const FProp& P : Props)
		{
			if (!P.bEnabled) { continue; }
			const float Reach = P.bDisc ? P.Radius : P.Half.Size();
			const int32 X0 = FMath::FloorToInt((P.Center.X - Reach - Origin.X) / NavCell);
			const int32 X1 = FMath::FloorToInt((P.Center.X + Reach - Origin.X) / NavCell);
			const int32 Y0 = FMath::FloorToInt((P.Center.Y - Reach - Origin.Y) / NavCell);
			const int32 Y1 = FMath::FloorToInt((P.Center.Y + Reach - Origin.Y) / NavCell);
			const FVector2D AxisY(-P.AxisX.Y, P.AxisX.X);
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					if (!InDomain(X, Y)) { continue; }
					const FVector2D D = To2(CellCenter(X, Y)) - P.Center;
					const bool bInside = P.bDisc
						? D.SizeSquared() <= P.Radius * P.Radius
						: (FMath::Abs(D | P.AxisX) <= P.Half.X && FMath::Abs(D | AxisY) <= P.Half.Y);
					if (bInside)
					{
						MarkCell(Mask[TSCNavLayer::People], X, Y);
						MarkCell(Mask[TSCNavLayer::Wildlife], X, Y);
						MarkCell(PropMask, X, Y);
					}
				}
			}
			const FIntPoint PC = CellOf(To3(P.Center));
			MarkCell(Mask[TSCNavLayer::People], PC.X, PC.Y);
			MarkCell(Mask[TSCNavLayer::Wildlife], PC.X, PC.Y);
			MarkCell(PropMask, PC.X, PC.Y);
		}
	}

	// Clearance: free cells next to a blocked one (outside the domain counts as blocked).
	for (int32 L = 0; L < TSCNavLayer::Num; ++L)
	{
		NearWall[L].Init(0, NW * NH);
		for (int32 Y = 0; Y < NH; ++Y)
		{
			for (int32 X = 0; X < NW; ++X)
			{
				if (Mask[L][Y * NW + X]) { continue; }
				for (int32 D = 0; D < 8; ++D)
				{
					if (BlockedFast(X + DX[D], Y + DY[D], L)) { NearWall[L][Y * NW + X] = 1; break; }
				}
			}
		}
	}
}

// ----- cell helpers -----

int32 FTSCNavGrid::Width() const { return NW; }
int32 FTSCNavGrid::Height() const { return NH; }
float FTSCNavGrid::CellSize() const { return NavCell; }

FVector FTSCNavGrid::CellCenter(int32 X, int32 Y) const
{
	return FVector(Origin.X + (X + 0.5f) * NavCell, Origin.Y + (Y + 0.5f) * NavCell, 0.f);
}

FIntPoint FTSCNavGrid::CellOf(const FVector& P) const
{
	return FIntPoint(FMath::FloorToInt((P.X - Origin.X) / NavCell), FMath::FloorToInt((P.Y - Origin.Y) / NavCell));
}

FIntPoint FTSCNavGrid::ClampCell(const FIntPoint& C) const
{
	return FIntPoint(FMath::Clamp(C.X, 0, NW - 1), FMath::Clamp(C.Y, 0, NH - 1));
}

bool FTSCNavGrid::BlockedFast(int32 X, int32 Y, int32 Layer) const
{
	if (!InDomain(X, Y) || Layer < 0 || Layer >= TSCNavLayer::Num) { return true; }
	return Mask[Layer][Y * NW + X] != 0;
}

bool FTSCNavGrid::BlockedAtFast(float X, float Y, int32 Layer) const
{
	return BlockedFast(FMath::FloorToInt((X - Origin.X) / NavCell), FMath::FloorToInt((Y - Origin.Y) / NavCell), Layer);
}

bool FTSCNavGrid::IsFreeFast(const FVector2D& P, float Radius, int32 Layer) const
{
	if (BlockedAtFast(P.X, P.Y, Layer)) { return false; }
	if (Radius <= 0.f) { return true; }
	const float R = Radius, D = 0.7071f * Radius;
	return !BlockedAtFast(P.X + R, P.Y, Layer) && !BlockedAtFast(P.X - R, P.Y, Layer)
		&& !BlockedAtFast(P.X, P.Y + R, Layer) && !BlockedAtFast(P.X, P.Y - R, Layer)
		&& !BlockedAtFast(P.X + D, P.Y + D, Layer) && !BlockedAtFast(P.X + D, P.Y - D, Layer)
		&& !BlockedAtFast(P.X - D, P.Y + D, Layer) && !BlockedAtFast(P.X - D, P.Y - D, Layer);
}

bool FTSCNavGrid::LineFreeFast(const FVector2D& A, const FVector2D& B, int32 Layer) const
{
	const FVector2D Seg = B - A;
	const float Len = Seg.Size();
	const FVector2D Dir = Len > KINDA_SMALL_NUMBER ? Seg / Len : FVector2D(1.f, 0.f);
	const FVector2D Side = FVector2D(-Dir.Y, Dir.X) * LineHalfWidth;
	const int32 N = FMath::Max(1, FMath::CeilToInt(Len / 12.5f));
	for (int32 I = 0; I <= N; ++I)
	{
		const FVector2D P = A + Seg * (float(I) / N);
		if (BlockedAtFast(P.X, P.Y, Layer) || BlockedAtFast(P.X + Side.X, P.Y + Side.Y, Layer) || BlockedAtFast(P.X - Side.X, P.Y - Side.Y, Layer)) { return false; }
	}
	return true;
}

// ----- queries -----

bool FTSCNavGrid::IsBlockedCell(int32 X, int32 Y, int32 Layer) const
{
	Bake();
	return BlockedFast(X, Y, Layer);
}

bool FTSCNavGrid::IsPropCell(int32 X, int32 Y) const
{
	Bake();
	return InDomain(X, Y) && PropMask[Y * NW + X] != 0;
}

bool FTSCNavGrid::IsBlockedAt(const FVector& P, int32 Layer) const
{
	Bake();
	return BlockedAtFast(P.X, P.Y, Layer);
}

bool FTSCNavGrid::IsFree(const FVector& P, float Radius, int32 Layer) const
{
	Bake();
	return IsFreeFast(To2(P), Radius, Layer);
}

bool FTSCNavGrid::LineFree(const FVector& A, const FVector& B, int32 Layer) const
{
	Bake();
	return LineFreeFast(To2(A), To2(B), Layer);
}

FVector FTSCNavGrid::Slide(const FVector& From, const FVector& To, float Radius, int32 Layer) const
{
	Bake();
	const FVector2D F2 = To2(From), T2 = To2(To);
	// Escape: a unit already inside an obstacle may walk out, but not from free ground into a blocked cell.
	if (!IsFreeFast(F2, Radius, Layer))
	{
		const bool bFromBlocked = BlockedAtFast(F2.X, F2.Y, Layer);
		return (!BlockedAtFast(T2.X, T2.Y, Layer) || bFromBlocked) ? To3(T2) : To3(F2);
	}
	const FVector2D Delta = T2 - F2;
	const int32 N = FMath::Max(1, FMath::CeilToInt(Delta.Size() / 20.f));
	const FVector2D Step = Delta / N;
	FVector2D Cur = F2;
	for (int32 I = 0; I < N; ++I)
	{
		const FVector2D Next = Cur + Step;
		if (IsFreeFast(Next, Radius, Layer))
		{
			Cur = Next;
			continue;
		}
		const bool bXOk = FMath::Abs(Step.X) >= 0.01f && IsFreeFast(Cur + FVector2D(Step.X, 0.f), Radius, Layer);
		const bool bYOk = FMath::Abs(Step.Y) >= 0.01f && IsFreeFast(Cur + FVector2D(0.f, Step.Y), Radius, Layer);
		if (bXOk && (!bYOk || FMath::Abs(Step.X) >= FMath::Abs(Step.Y))) { Cur.X += Step.X; }
		else if (bYOk) { Cur.Y += Step.Y; }
		else { break; }
	}
	return To3(Cur);
}

bool FTSCNavGrid::NearestFreeFast(const FVector& P, int32 Layer, FVector& Out) const
{
	if (NW <= 0 || NH <= 0) { return false; }
	const FIntPoint Raw = CellOf(P);
	const FIntPoint C = ClampCell(Raw);
	if (!BlockedFast(C.X, C.Y, Layer))
	{
		Out = InDomain(Raw.X, Raw.Y) ? FVector(P.X, P.Y, 0.f) : CellCenter(C.X, C.Y);
		return true;
	}
	const FVector2D P2 = To2(P);
	float BestD = MAX_flt;
	FIntPoint Best(-1, -1);
	for (int32 Ring = 1; Ring <= 12; ++Ring)
	{
		for (int32 Y = C.Y - Ring; Y <= C.Y + Ring; ++Y)
		{
			const bool bEdgeRow = Y == C.Y - Ring || Y == C.Y + Ring;
			for (int32 X = C.X - Ring; X <= C.X + Ring; X += (bEdgeRow ? 1 : 2 * Ring))
			{
				if (BlockedFast(X, Y, Layer)) { continue; }
				const float D = FVector2D::DistSquared(To2(CellCenter(X, Y)), P2);
				if (D < BestD) { BestD = D; Best = FIntPoint(X, Y); }
			}
		}
	}
	if (Best.X < 0) { return false; }
	Out = CellCenter(Best.X, Best.Y);
	return true;
}

bool FTSCNavGrid::NearestFree(const FVector& P, int32 Layer, FVector& Out) const
{
	Bake();
	return NearestFreeFast(P, Layer, Out);
}

void FTSCNavGrid::EnsureScratch() const
{
	const int32 Num = NW * NH;
	if (ScratchG.Num() != Num)
	{
		ScratchG.Init(0, Num);
		ScratchParent.Init(INDEX_NONE, Num);
		ScratchVisit.Init(0, Num);
		ScratchClosed.Init(0, Num);
		ScratchGen = 0;
	}
	if (++ScratchGen == 0)
	{
		// Wrapped around: clear the stamps once.
		FMemory::Memzero(ScratchVisit.GetData(), ScratchVisit.Num() * sizeof(uint32));
		FMemory::Memzero(ScratchClosed.GetData(), ScratchClosed.Num() * sizeof(uint32));
		ScratchGen = 1;
	}
}

bool FTSCNavGrid::FindPath(const FVector& From, const FVector& To, TArray<FVector>& Out, int32 Layer) const
{
	Out.Reset();
	Bake();
	FVector StartP, GoalP;
	if (!NearestFreeFast(From, Layer, StartP)) { return false; }
	const FIntPoint Start = CellOf(StartP);
	const FIntPoint Goal = NearestFreeFast(To, Layer, GoalP) ? CellOf(GoalP) : ClampCell(CellOf(To));

	EnsureScratch();
	const uint32 Gen = ScratchGen;
	auto Heur = [&Goal](int32 X, int32 Y)
	{
		const int32 Dx = FMath::Abs(X - Goal.X), Dy = FMath::Abs(Y - Goal.Y);
		return 10 * FMath::Max(Dx, Dy) + 4 * FMath::Min(Dx, Dy);
	};

	const int32 StartIdx = Start.Y * NW + Start.X;
	const int32 GoalIdx = Goal.Y * NW + Goal.X;
	TArray<FNavNode> Open;
	Open.Reserve(1024);
	ScratchG[StartIdx] = 0;
	ScratchParent[StartIdx] = INDEX_NONE;
	ScratchVisit[StartIdx] = Gen;
	const int32 StartH = Heur(Start.X, Start.Y);
	Open.HeapPush(FNavNode{ StartIdx, float(StartH), float(StartH) }, FNavNodeLess());

	int32 BestIdx = StartIdx, BestH = StartH, BestG = 0;
	bool bReached = false;
	while (Open.Num() > 0)
	{
		FNavNode Node;
		Open.HeapPop(Node, FNavNodeLess(), EAllowShrinking::No);
		const int32 Idx = Node.Idx;
		if (ScratchClosed[Idx] == Gen) { continue; }
		ScratchClosed[Idx] = Gen;
		const int32 X = Idx % NW, Y = Idx / NW;
		const int32 G = ScratchG[Idx];
		const int32 H = Heur(X, Y);
		if (H < BestH || (H == BestH && G < BestG)) { BestIdx = Idx; BestH = H; BestG = G; }
		if (Idx == GoalIdx) { bReached = true; break; }
		for (int32 D = 0; D < 8; ++D)
		{
			const int32 NX = X + DX[D], NY = Y + DY[D];
			if (BlockedFast(NX, NY, Layer)) { continue; }
			if (D >= 4 && (BlockedFast(X + DX[D], Y, Layer) || BlockedFast(X, Y + DY[D], Layer))) { continue; } // no corner cutting
			const int32 NIdx = NY * NW + NX;
			if (ScratchClosed[NIdx] == Gen) { continue; }
			const int32 NG = G + (D >= 4 ? 14 : 10) + (NearWall[Layer][NIdx] ? 6 : 0); // keep off walls where there is room
			if (ScratchVisit[NIdx] == Gen && ScratchG[NIdx] <= NG) { continue; }
			ScratchVisit[NIdx] = Gen;
			ScratchG[NIdx] = NG;
			ScratchParent[NIdx] = Idx;
			const int32 NH2 = Heur(NX, NY);
			Open.HeapPush(FNavNode{ NIdx, float(NG + NH2), float(NH2) }, FNavNodeLess());
		}
	}
	const int32 EndIdx = bReached ? GoalIdx : BestIdx;

	// Waypoints: cell centres from start to end.
	TArray<FVector> Way;
	for (int32 I = EndIdx; I != INDEX_NONE; I = ScratchParent[I])
	{
		Way.Add(CellCenter(I % NW, I / NW));
		if (I == StartIdx) { break; }
	}
	Algo::Reverse(Way);
	const FIntPoint ToCell = CellOf(To);
	if (bReached && InDomain(ToCell.X, ToCell.Y) && ToCell == Goal) { Way.Last() = FVector(To.X, To.Y, 0.f); }
	const bool bFromBlocked = BlockedAtFast(From.X, From.Y, Layer);
	if (EndIdx == StartIdx)
	{
		Out.Add(Way.Last());
		return bReached;
	}
	if (!bFromBlocked) { Way.RemoveAt(0); } // the unit starts at From

	// String pulling, forward greedy.
	FVector2D Anchor;
	int32 K = 0;
	if (bFromBlocked)
	{
		Out.Add(Way[0]);
		Anchor = To2(Way[0]);
		K = 1;
	}
	else
	{
		Anchor = To2(From);
	}
	while (K < Way.Num() - 1)
	{
		int32 Good = INDEX_NONE;
		for (int32 J = K; J < Way.Num(); ++J)
		{
			if (!LineFreeFast(Anchor, To2(Way[J]), Layer)) { break; }
			Good = J;
		}
		if (Good == INDEX_NONE) { Good = K; } // not even the next waypoint is in clear sight: take it anyway
		if (Good >= Way.Num() - 1) { break; }
		Out.Add(Way[Good]);
		Anchor = To2(Way[Good]);
		K = Good + 1;
	}
	Out.Add(Way.Last());
	return bReached;
}

void FTSCNavGrid::Flood(const FVector& From, int32 Layer, TArray<float>& OutCost) const
{
	Bake();
	OutCost.Init(MAX_flt, NW * NH);
	FVector StartP;
	if (!NearestFreeFast(From, Layer, StartP)) { return; }
	EnsureScratch();
	const uint32 Gen = ScratchGen;
	const FIntPoint Start = CellOf(StartP);
	const int32 StartIdx = Start.Y * NW + Start.X;
	const float Orth = NavCell, Diag = NavCell * UE_SQRT_2;

	TArray<FNavNode> Open;
	Open.Reserve(1024);
	OutCost[StartIdx] = 0.f;
	Open.HeapPush(FNavNode{ StartIdx, 0.f, 0.f }, FNavNodeLess());
	while (Open.Num() > 0)
	{
		FNavNode Node;
		Open.HeapPop(Node, FNavNodeLess(), EAllowShrinking::No);
		const int32 Idx = Node.Idx;
		if (ScratchClosed[Idx] == Gen) { continue; }
		ScratchClosed[Idx] = Gen;
		const int32 X = Idx % NW, Y = Idx / NW;
		const float G = OutCost[Idx];
		for (int32 D = 0; D < 8; ++D)
		{
			const int32 NX = X + DX[D], NY = Y + DY[D];
			if (BlockedFast(NX, NY, Layer)) { continue; }
			if (D >= 4 && (BlockedFast(X + DX[D], Y, Layer) || BlockedFast(X, Y + DY[D], Layer))) { continue; }
			const int32 NIdx = NY * NW + NX;
			if (ScratchClosed[NIdx] == Gen) { continue; }
			const float NG = G + (D >= 4 ? Diag : Orth) + (NearWall[Layer][NIdx] ? 30.f : 0.f);
			if (NG >= OutCost[NIdx]) { continue; }
			OutCost[NIdx] = NG;
			Open.HeapPush(FNavNode{ NIdx, NG, 0.f }, FNavNodeLess());
		}
	}
}

float FTSCNavGrid::CostAt(const TArray<float>& Cost, const FVector& P) const
{
	const FIntPoint C = CellOf(P);
	if (!InDomain(C.X, C.Y) || Cost.Num() != NW * NH) { return MAX_flt; }
	return Cost[C.Y * NW + C.X];
}
