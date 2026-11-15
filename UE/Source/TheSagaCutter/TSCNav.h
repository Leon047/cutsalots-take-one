// The Saga Cutter - game jam project.

#pragma once

#include "CoreMinimal.h"

/** Nav layers: People (a tree blocks its whole grid cell), Wildlife (a tree blocks only its trunk) and Hostile
 *  (raiders and fog-born: People plus the barred gate, which villagers can open and enemies cannot). */
namespace TSCNavLayer { enum Type { People = 0, Wildlife = 1, Hostile = 2, Num = 3 }; }

/**
 * Fine walkability grid for unit movement: 50 cm nav cells over the coarse game grid plus a margin.
 * Sources (coarse cells, trees, level props) are baked lazily into one mask per layer on the next query.
 * Plain struct, game thread only.
 */
struct FTSCNavGrid
{
	static constexpr int32 SUB = 4;                                   // nav cells per grid cell side
	static constexpr int32 PadW = 10, PadE = 2, PadN = 4, PadS = 4;   // margin in grid cells
	static constexpr float WallRadius = 22.f;    // body radius against obstacles; must stay below half a nav cell
	static constexpr float TrunkRadius = 45.f;
	static constexpr float LineHalfWidth = WallRadius;   // LineFree tests the same body width that Slide moves with

	void Init(int32 InGW, int32 InGH, float InCell);
	// ----- sources (each marks the grid dirty; the masks are baked lazily on the next query) -----
	void SetCoarse(int32 Cx, int32 Cy, bool bBlock);              // grid cell coords, may lie in the margin; also clears the tree flag
	void SetTree(int32 Cx, int32 Cy, const FVector& TrunkPos);    // call after SetCoarse(Cx, Cy, true)
	void SetGateBar(int32 Cx, int32 Cy, bool bBar);               // a barred gate cell blocks only the Hostile layer
	int32 AddPropBox(const FVector2D& Center, const FVector2D& AxisX, const FVector2D& HalfExtent);  // oriented box; AxisX unit length
	int32 AddPropDisc(const FVector2D& Center, float Radius);
	void SetPropEnabled(int32 Idx, bool bEnabled);
	void SetPropsActive(bool bActive);
	int32 NumProps() const;  FVector2D PropCenter(int32 Idx) const;  bool IsPropEnabled(int32 Idx) const;
	// ----- queries -----
	int32 Width() const;  int32 Height() const;  float CellSize() const;   // nav cells, cm
	FVector CellCenter(int32 X, int32 Y) const;                            // nav cell -> world (Z = 0)
	bool IsBlockedCell(int32 X, int32 Y, int32 Layer) const;               // outside the domain = blocked
	bool IsPropCell(int32 X, int32 Y) const;
	bool IsBlockedAt(const FVector& P, int32 Layer) const;
	bool IsFree(const FVector& P, float Radius, int32 Layer) const;
	bool LineFree(const FVector& A, const FVector& B, int32 Layer) const;
	FVector Slide(const FVector& From, const FVector& To, float Radius, int32 Layer) const;
	bool NearestFree(const FVector& P, int32 Layer, FVector& Out) const;
	bool FindPath(const FVector& From, const FVector& To, TArray<FVector>& Out, int32 Layer) const;
	void Flood(const FVector& From, int32 Layer, TArray<float>& OutCost) const;
	float CostAt(const TArray<float>& Cost, const FVector& P) const;

private:
	struct FProp
	{
		FVector2D Center = FVector2D::ZeroVector;
		FVector2D AxisX = FVector2D(1.f, 0.f);
		FVector2D Half = FVector2D::ZeroVector;
		float Radius = 0.f;
		bool bDisc = false;
		bool bEnabled = true;
	};

	/** Re-bakes the layer masks when a source changed. */
	void Bake() const;
	void MarkCell(TArray<uint8>& Mask, int32 X, int32 Y) const;
	bool InDomain(int32 X, int32 Y) const { return X >= 0 && Y >= 0 && X < NW && Y < NH; }
	/** Nav cell containing P (may lie outside the domain). */
	FIntPoint CellOf(const FVector& P) const;
	FIntPoint ClampCell(const FIntPoint& C) const;
	bool BlockedFast(int32 X, int32 Y, int32 Layer) const;   // no bake check
	bool BlockedAtFast(float X, float Y, int32 Layer) const; // no bake check
	bool IsFreeFast(const FVector2D& P, float Radius, int32 Layer) const;
	bool LineFreeFast(const FVector2D& A, const FVector2D& B, int32 Layer) const;
	bool NearestFreeFast(const FVector& P, int32 Layer, FVector& Out) const;
	void EnsureScratch() const;

	int32 GW = 0, GH = 0;    // coarse game grid
	int32 CW = 0, CH = 0;    // coarse domain incl. margin
	int32 NW = 0, NH = 0;    // nav cells
	float Cell = 200.f;      // coarse cell (cm)
	float NavCell = 50.f;    // nav cell (cm)
	FVector2D Origin = FVector2D::ZeroVector;   // world corner of nav cell (0, 0)

	TArray<uint8> Coarse;
	TArray<uint8> TreeFlag;
	TArray<uint8> GateBar;
	TArray<FVector2D> TreePos;
	TArray<FProp> Props;
	bool bPropsActive = true;

	mutable bool bDirty = true;
	mutable TArray<uint8> Mask[TSCNavLayer::Num];
	mutable TArray<uint8> PropMask;
	/** Per layer: free nav cell with a blocked cell among its 8 neighbours (paths pay extra to enter it). */
	mutable TArray<uint8> NearWall[TSCNavLayer::Num];

	// A* / Dijkstra scratch, sized once; a generation counter replaces clearing.
	mutable TArray<int32> ScratchG;
	mutable TArray<int32> ScratchParent;
	mutable TArray<uint32> ScratchVisit;
	mutable TArray<uint32> ScratchClosed;
	mutable uint32 ScratchGen = 0;
};
