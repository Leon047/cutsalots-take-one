# Unit pathfinding and avoidance

Goal: Every `ATSCUnit` (villagers, raiders, mushroom creatures, animals) walks only over free ground: never through buildings, the palisade, a closed gate, trees, rocks or level props, and units push apart instead of standing inside each other. A headless check proves it: zero "entered a blocked cell" violations over a full autoplay run.

Out of scope: AI decisions (targeting, `CanReach`, orders, scene scripts, economy), animation, the scripted movers (`ProWatchers`, boats, embers, camera), `Teleport` call sites, `ATSCColonist`, the `TEMP DIAG` block in `ATSCUnit::Sim`, Unreal NavMesh / collision profiles (not used, see Context), game balance numbers.

## Context

**Paths.** Repo root `C:\Users\Sega\Documents\Projects\TheSagaCutter`. The UE project is in `UE/`; all C++ is in `UE/Source/TheSagaCutter/`. Paths below are relative to the repo root.

**How movement works today.**
- All units are `ATSCUnit` (`TSCUnit.h/.cpp`), simulated by `ATSCGameMode` calling `Unit->Sim(Dt)` (main loop in `ATSCGameMode::Tick`, `TSCGameMode.cpp` ~line 1276; prologue loop in `SimPrologue`, ~line 1104). Not Pawns, no controllers, no physics. Everything is `NoCollision`, so sweeps and Recast cannot be used.
- The world is a grid: `GW = 34`, `GH = 24`, `TSC::CELL = 200` cm. World pos of cell corner = `((Cx - 17) * 200, (Cy - 12) * 200)`. `CellF(x, y)` takes fractional cell coordinates. `ATSCGameMode::Blocked` (one byte per cell) is filled in `BuildWorld` by `SetBlocked` / `BlockRect` (trees, palisade, buildings, log piles, rocks, water) and changed at runtime (gate, fallen event tree, plot buildings, felled timber). Every change does `++GridVersion`; units replan when `PathVersion != GM->GridVersion`.
- `ATSCGameMode::FindPath` = BFS on that grid + string pulling. `ATSCUnit::StepToward(Dest, Dt, StopDist)` follows it with `SetActorLocation`, returns true when within `StopDist`. All orders (Work, Hold, Shelter, Leave, Assault, Wander, Follow, timber, fighting) go through `StepToward`.
- Why units go through things today: (1) when `FindPath` fails the unit walks straight to the goal; (2) after the last waypoint it walks straight to the raw goal even if that is in a blocked cell; (3) the move is never checked against the grid; (4) ~50 level-placed props in `Map_Saga` are not in the grid; (5) everything outside the grid counts as blocked, so off-grid wildlife ignores trees; (6) forest tree cells are 62% dense, so the forest is not connected. No unit-to-unit separation exists.
- Wildlife confinement (`ETSCZone`, `ZoneAllows`, `bZoneBlocked`) is geometric (palisade superellipse) and must keep working exactly as now.

**Design chosen (do not swap for NavMesh / DetourCrowd / RVO).**
1. `FTSCNavGrid` (new, plain C++ struct owned by the game mode): 50 cm nav cells (4 x 4 per grid cell) over the grid plus a margin (10 cells west, 2 east, 4 north, 4 south, so off-grid forest is covered). Two layers: `People` (a tree blocks its whole 200 cm cell, as today) and `Wildlife` (a tree blocks only a 45 cm trunk disc, so animals can walk between trees; the forest is too dense to be connected otherwise). Level props are baked in as boxes / discs.
2. A* (8-neighbour, no corner cutting) + string pulling on the nav grid replaces the BFS. Contract kept from the old `FindPath`, relied on by the timber code: if `To` is in a free cell, `Out.Last() == To` exactly.
3. Movement goes through `FTSCNavGrid::Slide`: a step that would enter a blocked nav cell slides along it or stops. A unit that is already inside a blocked cell (spawned or teleported there, a building placed on it) may walk out ("escape").
4. A path always ends at the best reachable point. Reaching the end of the path counts as arrived, so orders whose authored goal is inside a blocked cell keep cycling.
5. Unit vs unit: soft separation pass in the game mode after all `Sim` calls (overlapping pairs are pushed apart through `Slide`), plus sidestep steering inside `StepToward`. Soft on purpose: hard blocking deadlocks in the gate.

**Gotchas.**
- `ATSCGameMode` is a `friend` of `ATSCUnit`, so game-mode code may read unit privates.
- Timber code (`ReserveTimber`, `ValidateTimber`, `SimTimberWork`, `TestTimber`) uses the coarse `IsBlocked` and the `Route.Last() == Spot` contract. Coarse `Blocked` stays the authority for game logic; the nav grid mirrors it.
- Several authored destinations are in blocked cells or hug walls (timber spots are 130 cm from the trunk; `CellF(10.6, 6.5)` is under a log pile). Never "fix" the data; rule 4 handles it.
- Scene scripts wait on `AtHoldPos()` (within 60 cm): woodcutter at `CellF(10.4, 10.5)` has no timeout. S3's key-point guard protects these.
- The prologue (title screen) runs the same `Sim` on the finished-village grid while props are still hidden; it is time-scripted, not arrival-scripted.
- `SpeedMul` sub-steps the sim (`Dt = RealDt * SpeedMul / Steps`); tests call `Sim(0.1f)`. Movement must be correct for `Dt` up to 0.1 s (`Slide` sub-steps internally).
- Log category: `LogSaga` (`TheSagaCutter.h`). Dev-only code: wrap in `#if !UE_BUILD_SHIPPING`.
- Code style: tabs, Allman braces, short `/** */` comments, `// The Saga Cutter - game jam project.` as first line of new files.

**Build and run.**
- Build (editor must be closed; `ue_stop` first if one runs): `sh Scripts/build_editor.sh` from the repo root. It prints only error / warning / Result lines. Success = a `Result: Succeeded` line and no `error`.
- Headless game run (no editor), used by Accept checks. From Git Bash at the repo root:
  ```bash
  LOG="$(cygpath -w "$PWD/UE/Saved/Logs/navcheck.log")"; rm -f "UE/Saved/Logs/navcheck.log"
  MSYS_NO_PATHCONV=1 "/c/Programs/UE_5.9/Engine/Binaries/Win64/UnrealEditor.exe" "$(cygpath -w "$PWD/UE/TheSagaCutter.uproject")" \
    /Game/Vikings/Maps/Map_Saga -game -windowed -ResX=640 -ResY=360 -nosound -nosplash -benchmark -fps=30 \
    -ExecCmds="<COMMAND>" -abslog="$LOG" >/dev/null 2>&1 &
  ```
  Start it in the background, wait until the expected log line appears in `UE/Saved/Logs/navcheck.log` (poll with an until-loop, time limit given per step), then stop it:
  ```bash
  powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='UnrealEditor.exe'\" | Where-Object { \$_.CommandLine -like '*navcheck.log*' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }"
  ```
  Never kill other `UnrealEditor.exe` processes. Add `-NoPrologue` for the test commands (`TIMBERTEST`, `NAVTEST`); do not add it for full autoplay.
- Steps S1 to S7 all compile the same module, so they run strictly one after another (see deps). Only S8 uses the editor.

## Steps

### S1 — Add the nav grid (`FTSCNavGrid`)   [tricky]   deps: none ✅
Files: `UE/Source/TheSagaCutter/TSCNav.h` (new), `UE/Source/TheSagaCutter/TSCNav.cpp` (new)

Do: A self-contained struct, no UObject, no dependency on game-mode headers. Game thread only. Declaration (keep these names and signatures; add private helpers as needed):

```cpp
namespace TSCNavLayer { enum Type { People = 0, Wildlife = 1, Num = 2 }; }

struct FTSCNavGrid
{
	static constexpr int32 SUB = 4;                                   // nav cells per grid cell side
	static constexpr int32 PadW = 10, PadE = 2, PadN = 4, PadS = 4;   // margin in grid cells
	static constexpr float WallRadius = 22.f;    // body radius against obstacles; must stay below half a nav cell
	static constexpr float TrunkRadius = 45.f;
	static constexpr float LineHalfWidth = 30.f;

	void Init(int32 InGW, int32 InGH, float InCell);
	// ----- sources (each marks the grid dirty; the masks are baked lazily on the next query) -----
	void SetCoarse(int32 Cx, int32 Cy, bool bBlock);              // grid cell coords, may lie in the margin; also clears the tree flag
	void SetTree(int32 Cx, int32 Cy, const FVector& TrunkPos);    // call after SetCoarse(Cx, Cy, true)
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
};
```

Behaviour:
- Domain. Coarse size `CW = GW + PadW + PadE`, `CH = GH + PadN + PadS`; nav size `CW*SUB` x `CH*SUB`; nav cell = `InCell / SUB` (50 cm). World origin of nav cell (0,0) corner = `((-(GW/2) - PadW) * InCell, (-(GH/2) - PadN) * InCell)` (integer division, same as `ATSCGameMode::CellCorner`). Coarse cell `(Cx, Cy)` is stored at `(Cx + PadW, Cy + PadN)`; calls outside the domain are ignored. All Z values are ignored on input; outputs have Z = 0.
- Sources: `Coarse` (uint8 per coarse cell), `Tree` flag + trunk position per coarse cell, and a prop list (box or disc, enabled flag). `bPropsActive` defaults to true.
- Bake (private, `const`, masks are `mutable`; run when dirty at the start of any query): for each layer a `TArray<uint8>` mask.
  - People: every blocked coarse cell fills its 4 x 4 nav cells.
  - Wildlife: same, but coarse cells with the tree flag are skipped; instead every nav cell whose centre is within `TrunkRadius` of the trunk position is blocked, plus the cell containing the trunk position.
  - Both layers, if `bPropsActive`: every enabled prop blocks the nav cells whose centre lies inside the box / disc, plus the cell containing its centre. Keep a separate prop mask for `IsPropCell`.
- `IsFree(P, R)`: `P` not blocked; if `R > 0` also the 8 points `P + R*(±1, 0)`, `P + R*(0, ±1)`, `P + 0.7071*R*(±1, ±1)`.
- `LineFree(A, B)`: sample the segment every 12.5 cm on three parallel lines (offset 0 and ±`LineHalfWidth` sideways), including both ends; false if any sample is blocked.
- `Slide(From, To, R)`:
  - Escape: if `!IsFree(From, R)`, return `To` when `!IsBlockedAt(To) || IsBlockedAt(From)`, else `From`.
  - Otherwise split the move into sub-steps of at most 20 cm. For each: take the full sub-step if `IsFree(Next, R)`; else try the X-only and the Y-only part (skip a part shorter than 0.01), take the free one (the longer if both are free); if neither is free, stop. Return the position reached.
- `NearestFree(P)`: clamp `P`'s cell into the domain; if it is free return `P` itself when `P` was inside the domain (else that cell's centre). Otherwise search square rings up to 12 cells out and return the centre of the free cell nearest to `P`; false if none.
- `FindPath(From, To, Out)`: `Out.Reset()` first.
  - Start cell = cell of `NearestFree(From)`; goal cell = cell of `NearestFree(To)`. If there is no start cell return false with `Out` empty. If there is no goal cell use the clamped cell of `To` as an unreachable goal.
  - A*: 8 neighbours, step cost 10 / 14, octile heuristic, a diagonal step only when both orthogonal neighbours are free. Binary heap (`TArray` `HeapPush` / `HeapPop`). Scratch arrays (`G`, `Parent`, visit stamp) are `mutable` members sized once, with a generation counter instead of clearing.
  - Track the expanded node with the smallest heuristic (ties: smaller G). If the goal is not reached, build the path to that node and return false; otherwise return true.
  - Waypoints: cell centres from start to end. If `From`'s own cell is blocked (or outside), keep the start cell centre as the first waypoint; otherwise drop it (the unit starts at `From`). If the goal was reached and `To`'s own cell is the goal cell, replace the last centre with `To` (X, Y exact). If start cell == end cell the path is that single last point.
  - String pulling, forward greedy: anchor = `From` (or the first waypoint when `From` was blocked, which is emitted first); extend to the next waypoint while `LineFree(anchor, candidate)` holds; on the first failure emit the last good waypoint and make it the anchor. Always emit the last waypoint.
- `Flood(From, Layer, OutCost)`: Dijkstra from the cell of `NearestFree(From)` with the same neighbour rules, cost in cm (50 / 70.7). `OutCost` is sized to all nav cells, unreached = `MAX_flt`. `CostAt` returns `MAX_flt` outside the domain.

Accept: `sh Scripts/build_editor.sh` succeeds (the struct is not used yet).

### S2 — Wire the nav grid into the game mode   [routine]   deps: S1 ✅
Files: `UE/Source/TheSagaCutter/TSCGameMode.h`, `UE/Source/TheSagaCutter/TSCGameMode.cpp`

Do:
1. `TSCGameMode.h`: `#include "TSCNav.h"`. Add private member `FTSCNavGrid Nav;` next to `TArray<uint8> Blocked;`. Add public `const FTSCNavGrid& GetNav() const { return Nav; }`. Change the declaration to `bool FindPath(const FVector& From, const FVector& To, TArray<FVector>& Out, int32 Layer = 0) const;`. Delete the declarations of `NearestFree` and `LineFree`.
2. `TSCGameMode.cpp`:
   - Delete the bodies of `ATSCGameMode::NearestFree` and `ATSCGameMode::LineFree`. Replace the whole body of `FindPath` with `return Nav.FindPath(From, To, Out, Layer);`.
   - `SetBlocked` becomes:
     ```cpp
     if (InGrid(Cx, Cy)) { Blocked[Cy * GW + Cx] = bBlock ? 1 : 0; }
     Nav.SetCoarse(Cx, Cy, bBlock); // also the margin outside the grid (forest edge trees, wall arcs)
     ++GridVersion;
     ```
   - `BuildWorld`, right after `Blocked.Init(0, GW * GH);`:
     ```cpp
     Nav.Init(GW, GH, CELL);
     // Margin outside the grid: open only west of the settlement's middle and outside the wall curve (forest animals live there).
     for (int32 Y = -FTSCNavGrid::PadN; Y < GH + FTSCNavGrid::PadS; ++Y)
     {
     	for (int32 X = -FTSCNavGrid::PadW; X < GW + FTSCNavGrid::PadE; ++X)
     	{
     		if (!InGrid(X, Y) && (X >= 19 || WallShape(X + 0.5f, Y + 0.5f) < 1.f)) { Nav.SetCoarse(X, Y, true); }
     	}
     }
     ```
   - In the tree loop, after the existing `SetBlocked(X, Y, true);` (the one following `BuildTree(T, X * 57 + Y * 13, true);`): add `Nav.SetTree(X, Y, T->GetActorLocation());`.
   - After `SetBlocked(TreeX, TreeY, true);` (the event spruce): add `Nav.SetTree(TreeX, TreeY, CellCenter(TreeX, TreeY));`.
   - `ReserveTimber`: one flood instead of one path search per spot. Before the `for (FTSCTimberTree& T : TimberTrees)` loop add `TArray<float> Reach; Nav.Flood(Worker->Loc2D(), TSCNavLayer::People, Reach);`. Replace the three lines from `TArray<FVector> Route;` through the `Cost` accumulation loop with:
     ```cpp
     if (Nav.IsBlockedAt(Spot, TSCNavLayer::People)) { continue; }
     const float Cost = Nav.CostAt(Reach, Spot);
     if (Cost >= MAX_flt) { continue; }
     ```
     Keep `if (Cost < BestCost) { ... }` and the coarse `IsBlocked` checks unchanged.
3. Do not touch `TSCUnit.*` in this step (its `GM->FindPath(Me, Dest, Path)` calls still compile through the default argument).

Accept: build succeeds. Headless run with `-NoPrologue -ExecCmds="TSCAuto TIMBERTEST x 1"`, wait up to 180 s for `[TimberTest] RESULT`; the line must read `failed=0`. (If the baseline already fails some check, run `git stash`, rebuild, record the baseline numbers, `git stash pop`, and require "no new failures".)

### S3 — Bake level props into the nav grid   [tricky]   deps: S2 ✅
Files: `UE/Source/TheSagaCutter/TSCGameMode.h`, `UE/Source/TheSagaCutter/TSCGameMode.cpp`

Do:
1. Header: `UPROPERTY() TArray<TObjectPtr<AActor>> NavPropActors;` and private `void BakeNavProps();`.
2. `CollectLevelProps`: right after the mushroom `continue` block (before `const FVector P = A->GetActorLocation();`) add `NavPropActors.Add(A);`. This collects every visible `AStaticMeshActor` and every `/Game/Props/` Blueprint actor in the level, inside and outside the wall.
3. `BuildWorld`: call `BakeNavProps();` immediately before the `// ----- people -----` comment.
4. `BakeNavProps()`:
   - Key points (must stay free and reachable): `HallDoor()`, `CellF(10.4f, 10.5f)`, `CellF(14.4f, 20.3f)`, `CellF(18.f, 14.6f)`, `CellF(17.f, 14.4f)`, `CellF(9.9f, 12.f)`, `CellF(7.5f, 12.f)`, `CellF(3.5f, 12.f)`, `CellF(0.5f, 12.f)`, `CellF(26.4f, 12.f)`, `CellF(31.5f, 12.f)`. First compute the baseline: `Nav.Flood(HallDoor(), TSCNavLayer::People, Cost)` and remember which key points have `CostAt < MAX_flt` and are not blocked.
   - For each actor in `NavPropActors` (skip null):
     - Skip the actor when any of its static meshes' path contains `VegetablePlant` or `VegetableSprout` (garden crops are walked through).
     - Blueprint prop that is not a fence (`!Cast<AStaticMeshActor>(A) && !IsFenceProp(A)`): one disc at the actor location, radius 40 if the class path contains `Brazier`, else 30. (Torches and banners are poles with wide overhead bounds; only the foot blocks.)
     - Otherwise (static mesh actors and fence Blueprints): for each `UStaticMeshComponent` of the actor (`GetComponents`) that has a mesh and is visible: skip it when its bottom `Bounds.Origin.Z - Bounds.BoxExtent.Z` is more than 140 above `GroundReliefAt(Bounds.Origin)` (hangs overhead). Let `LB = Mesh->GetBoundingBox()`, `T = GetComponentTransform()`. Centre = `T.TransformPosition(LB.GetCenter())`. If `FMath::Abs(T.GetUnitAxis(EAxis::Z).Z) < 0.8f` (tilted or lying prop): axis-aligned box, `AxisX = (1, 0)`, half extent = `Bounds.BoxExtent` XY. Else `AxisX` = the 2D normalised world X axis of `T`, half extent = `(LB.GetExtent().X * |Scale.X|, LB.GetExtent().Y * |Scale.Y|)`. Add 10 cm to both half extents, clamp each to at least 15, then `Nav.AddPropBox(...)`.
     - Guard: after adding this actor's props, flood again from `HallDoor()`. If a key point that was fine in the baseline is now blocked or unreachable, call `Nav.SetPropEnabled(Idx, false)` for every prop of this actor and log `UE_LOG(LogSaga, Warning, TEXT("[Nav] prop %s ignored: it would block a key point"), *A->GetName());`.
   - End: `++GridVersion;` and `UE_LOG(LogSaga, Display, TEXT("[Nav] props: %d obstacles from %d level actors, %d actors ignored"), ...)`.
5. Props are hidden during the prologue. In `StartPrologue()` add `Nav.SetPropsActive(false); ++GridVersion;`. In `EndPrologue()` add `Nav.SetPropsActive(true);` just before the existing `++GridVersion;  // everyone plans a fresh path from home`.

Accept: build succeeds; headless `-NoPrologue -ExecCmds="TSCAuto TIMBERTEST x 1"` log contains one `[Nav] props:` line with a non-zero obstacle count, and `[TimberTest] RESULT` with `failed=0` (or no new failures).

### S4 — Route unit movement through the nav grid   [tricky]   deps: S3 ✅
Files: `UE/Source/TheSagaCutter/TSCUnit.h`, `UE/Source/TheSagaCutter/TSCUnit.cpp`

Do:
1. `TSCUnit.h`: `#include "TSCNav.h"`. Add private members:
   ```cpp
   /** Body radius against other units (cm). */
   float NavRadius = 28.f;
   int32 NavLayer() const { return IsWildlife() ? TSCNavLayer::Wildlife : TSCNavLayer::People; }
   /** Moves by Delta (2D) on the nav grid: slides along obstacles, never enters a blocked cell, respects Zone. Returns the distance moved. */
   float MoveNav(const FVector& Delta, float WallRadius);
   /** A push from the crowd (unit separation); same rules as MoveNav. */
   void Nudge(const FVector& Delta);
   bool bPathOK = false;
   float RepathCooldown = 0.f;
   float StuckClock = 0.f;
   int32 StuckCount = 0;
   FVector StuckRef = FVector::ZeroVector;
   float CrowdWait = 0.f;
   float LastStepTime = -1000.f;
   /** Dev check (tsc.NavCheck): was inside a blocked nav cell at the last check. */
   bool bNavWasBlocked = true;
   ```
2. `Init`: after the role `switch`, set `NavRadius` by role: Fox 18; Husky 22; Wolf 26; Worker, Hunter, Woodcutter, Healer, Shield, Raider 28; Creature, Boar, Deer 30; Stag 34; Cow 40; Bull 44.
3. `Teleport`: also set `bNavWasBlocked = true; StuckCount = 0; CrowdWait = 0.f; PathVersion = -1;`.
4. Add:
   ```cpp
   float ATSCUnit::MoveNav(const FVector& Delta, float WallRadius)
   {
   	const FVector Loc = GetActorLocation();
   	const FVector From(Loc.X, Loc.Y, 0.f);
   	const FVector To = GM->GetNav().Slide(From, From + FVector(Delta.X, Delta.Y, 0.f), WallRadius, NavLayer());
   	if (Zone != ETSCZone::Free && ZoneAllows(From) && !ZoneAllows(To)) { bZoneBlocked = true; return 0.f; } // the palisade is the limit
   	const float Moved = FVector::Dist2D(From, To);
   	if (Moved > 0.01f) { SetActorLocation(FVector(To.X, To.Y, Loc.Z)); } // keep the wading depth
   	return Moved;
   }
   void ATSCUnit::Nudge(const FVector& Delta)
   {
   	const bool bSaved = bZoneBlocked;
   	MoveNav(Delta, FTSCNavGrid::WallRadius);
   	bZoneBlocked = bSaved;
   }
   ```
5. Replace the whole body of `StepToward` with this logic (same signature, same meaning of the return value):
   ```cpp
   bZoneBlocked = false;
   const FVector Me = Loc2D();
   const float Now = GM->GetWorldTime();
   if (Now - LastStepTime > 0.25f) { StuckClock = 0.f; StuckCount = 0; StuckRef = Me; } // was not walking: start the stuck watch afresh
   LastStepTime = Now;
   const float DestDist = FVector::Dist2D(Me, Dest);
   if (DestDist <= StopDist)
   {
   	StuckClock = 0.f; StuckCount = 0; CrowdWait = 0.f; StuckRef = Me;
   	return true;
   }
   const FTSCNavGrid& Nav = GM->GetNav();
   const int32 Layer = NavLayer();
   RepathCooldown = FMath::Max(0.f, RepathCooldown - Dt);
   bool bReplan = PathVersion != GM->GridVersion || FVector::Dist2D(PathDest, Dest) > 60.f;
   // Pushed off the planned line (crowd, wall slide): plan again from here, at most about three times a second.
   if (!bReplan && RepathCooldown <= 0.f && PathIdx < Path.Num() && Nav.IsFree(Me, 0.f, Layer) && !Nav.LineFree(Me, Path[PathIdx], Layer)) { bReplan = true; }
   if (bReplan)
   {
   	Path.Reset();
   	bPathOK = GM->FindPath(Me, Dest, Path, Layer);
   	PathIdx = 0; PathDest = Dest; PathVersion = GM->GridVersion; RepathCooldown = 0.35f;
   }
   while (PathIdx < Path.Num() - 1 && FVector::Dist2D(Me, Path[PathIdx]) < 24.f) { ++PathIdx; }
   // The path ends at the best reachable point (Dest itself whenever Dest is free and reachable): its end is "arrived".
   if (Path.IsEmpty() || (PathIdx >= Path.Num() - 1 && FVector::Dist2D(Me, Path.Last()) < 12.f))
   {
   	StuckClock = 0.f; StuckCount = 0; StuckRef = Me;
   	return true;
   }
   // Crowd: others stand where this unit wants to be. Close enough counts for a moment, then it tries again.
   if (CrowdWait > 0.f) { CrowdWait -= Dt; return true; }
   StuckClock += Dt;
   if (StuckClock >= 0.6f)
   {
   	const bool bStuck = FVector::Dist2D(Me, StuckRef) < 0.25f * Speed * StuckClock;
   	StuckCount = bStuck ? StuckCount + 1 : 0;
   	StuckRef = Me; StuckClock = 0.f;
   	if (bStuck && (DestDist <= StopDist + 120.f || FVector::Dist2D(Me, Path.Last()) <= 120.f))
   	{
   		CrowdWait = 1.5f; StuckCount = 0;
   		return true;
   	}
   	if (StuckCount >= 3) { PathVersion = -1; StuckCount = 0; } // plan again on the next step
   }
   const FVector Next = Path[FMath::Min(PathIdx, Path.Num() - 1)];
   FVector Dir = Next - Me;
   Dir.Z = 0.f;
   const float Dist = Dir.Size();
   if (Dist > 1.f)
   {
   	Dir /= Dist;
   	const float StepLen = FMath::Min(Dist, Speed * Dt);
   	// The last stretch may hug a wall when the goal itself does (work spots next to trees and walls).
   	const bool bFinal = PathIdx >= Path.Num() - 1 && Dist < 60.f;
   	MoveNav(Dir * StepLen, bFinal ? 0.f : FTSCNavGrid::WallRadius);
   	if (bZoneBlocked)
   	{
   		Path.Reset(); // as before: the caller picks another goal
   		return false;
   	}
   	const FRotator Want = Dir.Rotation();
   	SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Want.Yaw, 0.f), Dt, 10.f));
   }
   return false;
   ```
6. `SimTimberWork`: change `GM->FindPath(Loc2D(), Spot, CheckPath)` to pass `NavLayer()` as the fourth argument. No other caller changes: every order keeps calling `StepToward`.
7. In `SimWildlife`, the failsafe block that calls `Teleport(HoldPos); Path.Reset();` stays as is.

Accept: build succeeds; headless `TSCAuto TIMBERTEST x 1` (`-NoPrologue`) reports `failed=0` (or no new failures). Then a headless full run `-ExecCmds="TSCAuto LALA HM 8"` (no `-NoPrologue`) must reach a log line containing `[Epilogue]` within 20 minutes and have no `Fatal error`. If it stalls, the usual cause is an order waiting for an arrival that never comes: find the unit and fix the arrival rule in `StepToward`, not the order code.

### S5 — Keep units apart: separation pass and sidestep steering   [tricky]   deps: S4 ✅
Files: `UE/Source/TheSagaCutter/TSCGameMode.h`, `UE/Source/TheSagaCutter/TSCGameMode.cpp`, `UE/Source/TheSagaCutter/TSCUnit.cpp`

Do:
1. Header, public (services for units): `FVector SteerAround(const ATSCUnit* U, const FVector& Me, const FVector& Dir) const;` Private: `void SeparateUnits(float Dt);`.
2. "Active" unit = `U && U->IsAlive() && !U->bSheltered && !U->IsHidden()`. Both functions look at `Units` and `Wildlife` together.
3. `SeparateUnits(Dt)`:
   - Collect active units into a local array; allocate a parallel `TArray<FVector> Push` of zeros.
   - For every pair `(A, B)`: `Min = A->NavRadius + B->NavRadius`, `D` = 2D distance. If `D >= Min` skip. Normal `N` from A to B; if `D < 1` use `N = (cos(i * 2.4), sin(i * 2.4), 0)` with `i` the index of A (deterministic). `Pen = Min - D`. A downed unit (`bDowned`) is not moved and the other takes the whole `Pen`; otherwise each takes half. `Push[A] -= N * share`, `Push[B] += N * share`.
   - Apply: for each unit with a non-zero push, `Delta = Push * FMath::Min(1.f, 12.f * Dt)`, clamp its length to `240.f * Dt`, call `U->Nudge(Delta)`. Do not rotate units.
4. Call `SeparateUnits(Dt);` in `Tick` inside the sub-step loop, right after the `for (ATSCUnit* A : Wildlife) ... A->Sim(Dt)` line; and in `SimPrologue` right after `for (int32 I = 0; I < ProLanded; ++I) { Units[I]->Sim(Dt); }`.
5. `SteerAround(U, Me, Dir)` (`Dir` is 2D unit length; returns a 2D unit vector):
   - `Perp = FVector(-Dir.Y, Dir.X, 0)`. `Sum = 0`.
   - For each active `O != U`, skipping `U->Target.Get()` and `U->Leader.Get()`: `V = O->Loc2D() - Me`, `D = |V|`, `R = U->NavRadius + O->NavRadius`. Skip if `D > R + 60` or `D < 1`. `Ahead = V | Dir`; skip if `Ahead <= 0`. `Lat = V | Perp`; skip if `|Lat| > R + 10`. `W = 1 - clamp((D - R) / 60, 0, 1)`. `Side = Lat > 4 ? -1 : 1` (an obstacle on the +Perp side sends the unit to -Perp; dead ahead always goes to +Perp, so two units meeting head-on pass each other). `Sum += Side * W * 1.2`.
   - `Out = (Dir + Perp * clamp(Sum, -1.5, 1.5)).GetSafeNormal2D()`; return `Dir` if `Out` is nearly zero.
6. `TSCUnit.cpp`, `StepToward`: replace the single line `MoveNav(Dir * StepLen, bFinal ? 0.f : FTSCNavGrid::WallRadius);` with:
   ```cpp
   const FVector MoveDir = bFinal ? Dir : GM->SteerAround(this, Me, Dir);
   float Moved = MoveNav(MoveDir * StepLen, bFinal ? 0.f : FTSCNavGrid::WallRadius);
   if (!bZoneBlocked && Moved < 0.2f * StepLen && !MoveDir.Equals(Dir)) { Moved = MoveNav(Dir * StepLen, FTSCNavGrid::WallRadius); } // the sidestep was walled in
   ```
   Facing keeps using `Dir`.

Accept: build succeeds; `TSCAuto TIMBERTEST x 1` still `failed=0` (or no new failures); headless `TSCAuto LALA HM 8` reaches `[Epilogue]` within 20 minutes with no `Fatal error`.

### S6 — Dev checks: violation counter, debug draw, NAVTEST   [routine]   deps: S5 ✅ (7/8; fix in S6b)
Files: `UE/Source/TheSagaCutter/TSCGameMode.h`, `UE/Source/TheSagaCutter/TSCGameMode.cpp`

Do (everything inside `#if !UE_BUILD_SHIPPING` except declarations, which need no guard; UFUNCTIONs must not sit inside preprocessor blocks):
1. Console variables in the anonymous namespace next to `CVarPrologue`: `tsc.NavCheck` (int32, default 1, "counts units that step into a blocked nav cell") and `tsc.NavDebug` (int32, default 0, "draws the nav grid, unit radii and paths").
2. Header: private `void CheckNav(); void DrawNavDebug(); void TestNav(); int32 NavViolations = 0; int32 NavDeepOverlaps = 0; float NavMaxPen = 0.f; int32 NavCheckVersion = -1; float NavStatsTime = 0.f;` and public `UFUNCTION(Exec) void TSCNavStats();`.
3. `CheckNav()`, called in `Tick` right after the `SeparateUnits(Dt);` added in S5 when `tsc.NavCheck != 0`:
   - If `NavCheckVersion != GridVersion`: set every active unit's `bNavWasBlocked` to its current state (`Nav.IsBlockedAt(U->Loc2D(), U->NavLayer())`), store the version, return (the map changed under them; not a violation).
   - Else for each active unit: `bIn` = current state. If `bIn && !U->bNavWasBlocked`: `++NavViolations` and, for the first 30, `UE_LOG(LogSaga, Warning, TEXT("[Nav] VIOLATION %s role=%d entered a blocked cell at %s"), *U->GetName(), (int32)U->Role, *U->Loc2D().ToString());`. Then `U->bNavWasBlocked = bIn`.
   - For each active pair with `D < 0.5f * (Ra + Rb)`: `++NavDeepOverlaps`, `NavMaxPen = max(NavMaxPen, Ra + Rb - D)`.
   - Every 30 s of `WorldTime` call `TSCNavStats()`.
4. `TSCNavStats()`: `UE_LOG(LogSaga, Display, TEXT("[Nav] STATS violations=%d deepOverlaps=%d maxPen=%.0f"), ...)` (empty body in shipping).
5. `DrawNavDebug()`, called once per `Tick` when `tsc.NavDebug != 0` (`#include "DrawDebugHelpers.h"`): for every People-layer blocked nav cell that has at least one free 4-neighbour, `DrawDebugBox` (half extent 24, 24, 2) at `Nav.CellCenter(X, Y)` with Z = `GroundReliefAt(...) + 20`, red, or magenta when `Nav.IsPropCell(X, Y)`; for each active unit a flat `DrawDebugCircle` of `NavRadius` (white) and green `DrawDebugLine`s along `U->Path` from `U->PathIdx`. Lifetime 0 (one frame).
6. `TestNav()`, started by `TSCAuto NAVTEST x 1`: in `TSCAuto`, next to the `TIMBERTEST` line, add `if (Takes.Equals(TEXT("NAVTEST"), ESearchCase::IgnoreCase)) { TestNav(); return; }`. Model it on `TestTimber` (same `Check` lambda, prefix `[NavTest]`, final line `[NavTest] RESULT passed=%d failed=%d`). Start with `if (Phase == ETSCPhase::Title) { StartGame(); } Phase = ETSCPhase::Day;`. Helper: `Run(Seconds, Each)` loops in 0.05 s steps: `Sim(0.05f)` on the test units, `SeparateUnits(0.05f)`, then `Each()`. Test units are `SpawnUnit(ETSCRole::Worker, TEXT("NavTest"), Pos)` given `OrderHold(Goal)`; at the end `Units.Remove(U); U->Destroy();`. Layer `L = TSCNavLayer::People`. Checks:
   - "path goes around a building": `FindPath(CellF(20.5f, 6.f), CellF(23.5f, 6.f), P)` is true, `P.Num() >= 2`, `P.Last()` within 1 cm of the goal, no point blocked, every leg (from the start point on) passes `Nav.LineFree`.
   - "blocked goal snaps to free ground": `FindPath(CellF(20.5f, 6.f), CellF(22.f, 6.f), P)` (goal inside the house at cells 21..22 x 5..6) is true, `P.Last()` is not blocked and within 250 cm of the goal.
   - "closed gate has no way round": if `!bGateClosed`, `SetBlocked(GateX, GateY, true); SetBlocked(GateX, GateY + 1, true);` then `FindPath(CellF(10.5f, 12.f), CellF(5.f, 12.f), P)` must be false with `P` non-empty and `IsInsideSettlement(P.Last())`; restore both cells to false. (Skip the block / restore when the gate is already closed.)
   - "walker never enters a blocked cell": one unit from `CellF(20.5f, 6.f)` holding `CellF(23.5f, 6.f)`, `Run(40 s)`; passes if `Nav.IsBlockedAt(U->Loc2D(), L)` was never true and `U->AtHoldPos()` at the end.
   - "head-on walkers pass": A from `CellF(23.f, 7.6f)` to `CellF(25.4f, 7.6f)`, B the reverse, `Run(25 s)`; both `AtHoldPos()` at the end and their distance never below 28.
   - "six on one spot spread out": six units spawned at `CellF(24.2f, 7.6f)` all holding that point, `Run(8 s)`; smallest pairwise distance at the end >= 40.
   - "wildlife passes between trunks": some standing `TimberTrees` entry with `Cell.X <= 5` exists for which `Nav.IsBlockedAt(T.Base, Wildlife)` is true and at least one of the four points `CellCenter(T.Cell) + (±90, ±90)` is blocked in People but free in Wildlife.
   - "props are obstacles": `Nav.NumProps() > 0` and every enabled prop's centre is blocked in People.

Accept: build succeeds; headless `-NoPrologue -ExecCmds="TSCAuto NAVTEST x 1"`, wait up to 300 s for `[NavTest] RESULT`; require `failed=0`. A failing check means a bug in S1 to S5: report which check failed and the numbers, do not weaken the check.

### S7 — Full-run verification and tuning   [tricky]   deps: S6 ✅
Files: none expected; fixes go to `TSCNav.cpp`, `TSCUnit.cpp` or `TSCGameMode.cpp` as findings require

Do:
1. Headless `-NoPrologue -ExecCmds="TSCAuto TIMBERTEST x 1"` and `-ExecCmds="TSCAuto NAVTEST x 1"`: both `failed=0`.
2. Headless full runs, no `-NoPrologue`: `TSCAuto LALA HM 8` and `TSCAuto AAAA HM 8` (each: wait for `[Epilogue]`, limit 20 minutes, then stop the process). In each log require:
   - no `Fatal error`;
   - zero `[Nav] VIOLATION` lines;
   - the last `[Nav] STATS` line before `[Epilogue]`: `violations=0`, `maxPen` under 45; report `deepOverlaps`;
   - the same `[Saga]` story milestones as a baseline run appear (scenes start and resolve, night starts, epilogue). To get the baseline, read `UE/Saved/MovieRenders/*/events.txt` if present; otherwise just confirm every scene and the night are reached.
3. If a check fails, find the cause from the log (unit name, role, position) and fix the movement code. Allowed tuning knobs: `NavRadius` per role, the separation rate (12 / s) and cap (240 cm/s), steering weight (1.2) and range (60), `CrowdWait`, stuck thresholds, prop radii and padding. Not allowed: changing orders, goals or scene scripts.

Accept: all of the above hold; report the final numbers from both runs.

### S8 — Editor check of the nav grid against the visible world (EDITOR, run alone)   [routine]   deps: S7 ✅
Files: none expected (prop rules in `BakeNavProps` if something is wrong)

Do: Use the ue-editor skill (`ue_start` offscreen, map `/Game/Vikings/Maps/Map_Saga`; only one agent drives the editor). Start PIE (`EditorToolset.EditorAppToolset` `StartPIE`), send `TSCAuto LLLL HM 2` to start a day, then `tsc.NavDebug 1`. Take viewport screenshots of the settlement and of the forest edge. Check:
- magenta cells sit under real props (crates, furniture, braziers, torch and banner feet, altar, fence) and nowhere else;
- no red or magenta cells on the road, in the gate opening (while open), on the dock planks, in front of the hall door or on the garden;
- the white unit circles do not overlap deeply, and green paths bend around buildings.
Then `tsc.NavDebug 0`, stop PIE, `ue_stop`. If a prop footprint is wrong, adjust the rule for that prop kind in `BakeNavProps`, rebuild (editor closed) and repeat S7 step 1 and this check.

Accept: screenshots show the three points above; attach their paths in the report.

### S6b — Fix: paths hug walls and string pulling does not shorten them   [tricky]   deps: S6 ✅
Files: `UE/Source/TheSagaCutter/TSCNav.h`, `UE/Source/TheSagaCutter/TSCNav.cpp`

Found in S6: NAVTEST "path goes around a building" fails. A* has no clearance cost, so the path runs along cell centres 25 cm from the house wall, and `LineFree`'s ±30 cm side lines touch the blocked cells. String pulling then cannot merge any leg, so paths are long zigzags of 50 cm steps along walls.

Do:
1. `LineHalfWidth` = `WallRadius` (22, below the 25 cm half cell), so `LineFree` tests the same body width that `Slide` moves with. Keep the constant name; set `static constexpr float LineHalfWidth = WallRadius;`.
2. A* clearance cost: precompute (during Bake, per layer, `mutable TArray<uint8>`) a "near wall" flag for every free nav cell that has a blocked cell among its 8 neighbours. Entering such a cell costs +6 on top of the 10 / 14 step cost (the heuristic stays admissible). Use the same extra cost in `Flood` (in cm: +30). This keeps routes off walls where there is room, while narrow passages stay usable.
3. Do not change anything else in S1–S6, and do not change NAVTEST check conditions.

Accept: build succeeds; headless `-NoPrologue` `TSCAuto NAVTEST x 1` → `failed=0`; `TSCAuto TIMBERTEST x 1` → `failed=0`; headless full `TSCAuto LALA HM 8` reaches `[Epilogue]`, no `Fatal error`; report the epilogue fell/lived numbers and the last `[Nav] STATS` line.

## Final check

1. `sh Scripts/build_editor.sh` (editor closed): `Result: Succeeded`, no errors.
2. Headless: `TSCAuto TIMBERTEST x 1` and `TSCAuto NAVTEST x 1` both end with `failed=0`.
3. Headless `TSCAuto LALA HM 8`: reaches `[Epilogue]`; `grep -c "\[Nav\] VIOLATION"` on the log is 0; last `[Nav] STATS` has `violations=0`.
4. In PIE at normal speed, watch one day and the night: villagers go around the longhouse and houses instead of through them, nobody crosses the palisade or the closed gate, creatures queue at the gate without stacking on one point, raiders come up from the dock around the crates, deer and wolves move between the trees, and the title prologue still plays through to the built village.
5. `git status`: only `TSCNav.h`, `TSCNav.cpp` added and `TSCGameMode.h/.cpp`, `TSCUnit.h/.cpp` changed (plus the plan file). Nothing committed.

## Risks / open questions

1. Prop placement is unknown from code (the map is binary). A prop may sit on a path or a work spot. Default: the key-point guard in S3 drops any prop that blocks a scripted point; anything else is routed around; S8 is the visual check.
2. Shields (`SM_Shield_*`) may hang on walls or stand on the ground. Default: treat like any static mesh; the "bottom more than 140 cm above ground" rule drops hung ones.
3. Night balance can shift a little: attackers now walk around obstacles and jostle at the gate and the hall door, so they arrive slightly later and fewer strike at once. Default: accept; do not retune damage or HP. Report the night outcome of the S7 runs so the user can judge.
4. Prologue: separation also runs there. It is time-scripted, so it should only look different (settlers no longer overlap). Default: keep it on; if the prologue visibly breaks, remove the `SeparateUnits` call from `SimPrologue` only.
5. Shared assault slots (`GetAssaultGoal` gives about 10 slots for any number of attackers) now rely on the crowd-arrival rule (`StopDist + 120`). Default: keep the slots as they are (AI logic is out of scope); if attackers visibly idle behind the front row, raise the slack from 120 to 180.
6. Sub-cell precision: obstacles are 50 cm cells and the wall body radius is 22 cm, so a large animal (bull, radius 44 against units) can visually brush a wall. Default: accept.
7. Villagers still walk through the garden crops and across fallen timber (their cells are free by design today). Default: unchanged.
8. Off-grid starts beyond the 10-cell west margin (very large creature waves spawn further west) fall back to a straight walk to the nearest free cell, as today. Default: accept; raise `PadW` if a wave is ever seen crossing trees there.
9. `-game` headless runs open a small window; `-nullrhi` was not tried. Default: use the windowed command given in Context.
