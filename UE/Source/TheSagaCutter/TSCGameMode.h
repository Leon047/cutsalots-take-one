// The Saga Cutter - game jam project.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TSCBuilders.h"
#include "TSCUnit.h"
#include "TSCNav.h"
#include "TSCGameMode.generated.h"

class ATSCPrimActor;
class ATSCOcean;
class ATSCTerrain;
class UAudioComponent;
class USoundBase;
class USoundAttenuation;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class USkyLightComponent;

enum class ETSCPhase : uint8 { Title, Day, Night, Epilogue };
enum class ETSCDogSound : uint8 { PlayBark, Pant, Growl, AttackBark, AlertBark };
enum class ETSCDayOnePrompt : uint8 { None, Wood, Food, Build, Cut };

/** Editable regions of the map; every object in a region has a Living and an Ashen version. */
namespace TSCRegion
{
	enum Type { None = 0, Forest = 1, Garden = 2, Grove = 3, Dock = 4 };
}

/** One editable scene: a scripted key event with two takes. */
struct FTSCScene
{
	FString Title;
	FString Alert;        // banner while the scene plays
	FString LivingName;
	FString LivingText;   // what happens if the scene is left as it is
	FString AshenName;
	FString AshenText;    // the alternate take: what it solves and what it costs
	int32 Region = 0;
	bool bStarted = false;
	bool bResolved = false;
	bool bAshen = false;
	bool bCutOpenable = false;
	float Window = 0.f;   // seconds left to press CUT!
	float Timer = 0.f;
	int32 Step = 0;
};

struct FTSCTracer
{
	FVector From;
	FVector To;
	FLinearColor Color;
	float Time;
};

struct FTSCToast
{
	FString Text;
	float Time;
};

struct FTSCEmber
{
	TObjectPtr<ATSCPrimActor> Actor;
	FVector Vel;
	float Life;
};

struct FTSCPlot
{
	FIntPoint Cell;
	bool bAvailable = false;
	bool bUsed = false;
	TSC::EBuilding Type = TSC::EBuilding::House;
	float Progress = 0.f;
	TObjectPtr<ATSCPrimActor> Marker;
	TObjectPtr<ATSCPrimActor> Building;
};

/** Runtime timber state. Scripted/prologue trees are deliberately not registered here. */
struct FTSCTimberTree
{
	TWeakObjectPtr<ATSCPrimActor> Tree;
	TWeakObjectPtr<ATSCUnit> Owner;
	FIntPoint Cell;
	FVector Base = FVector::ZeroVector;
	FVector WorkSpot = FVector::ZeroVector;
	FVector FallDir = FVector::ForwardVector;
	FQuat Rotation = FQuat::Identity;
	int32 Stage = 0; // standing, fallen, exhausted; persists across a worker cancellation
	float FallTime = 0.f;
};

/**
 * The whole game: builds the clearing out of primitives, runs the three days and the final night, the two-resource
 * economy, the scripted scenes with the CUT! mechanic, and assembles the skald's epilogue.
 */
UCLASS()
class THESAGACUTTER_API ATSCGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATSCGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ----- tunables ---------------------------------------------------------------------------------------------------
	/** Edits available for the whole game. */
	UPROPERTY(EditAnywhere, Category = "Saga")
	int32 MaxEdits = 3;
	UPROPERTY(EditAnywhere, Category = "Saga")
	float DayLength = 135.f;
	UPROPERTY(EditAnywhere, Category = "Saga")
	float NightLength = 105.f;
	/** Seconds between economy ticks. */
	UPROPERTY(EditAnywhere, Category = "Saga")
	float EconomyInterval = 5.f;

	// ----- music (Sound Cues pick a random take on each play) ---------------------------------------------------------
	/** Title screen, and the epilogue after the victory sting or a fallen hall. */
	UPROPERTY(EditAnywhere, Category = "Saga|Music")
	TObjectPtr<USoundBase> MenuMusic;
	UPROPERTY(EditAnywhere, Category = "Saga|Music")
	TObjectPtr<USoundBase> DayMusic;
	UPROPERTY(EditAnywhere, Category = "Saga|Music")
	TObjectPtr<USoundBase> NightMusic;
	/** One-shot sting when the hall survives the night. */
	UPROPERTY(EditAnywhere, Category = "Saga|Music")
	TObjectPtr<USoundBase> VictoryMusic;
	UPROPERTY(EditAnywhere, Category = "Saga|Music")
	float MusicVolume = 0.6f;
	UPROPERTY(EditAnywhere, Category = "Saga|Music")
	float MusicFadeTime = 1.5f;

	// ----- state read by the HUD --------------------------------------------------------------------------------------
	ETSCPhase Phase = ETSCPhase::Title;
	int32 Day = 1;
	float DayTime = 0.f;
	float NightTime = 0.f;
	float Wood = 70.f;
	float Food = 70.f;
	int32 WoodRate = 0;
	int32 FoodRate = 0;
	int32 Pop = 0;
	int32 PopCap = 18;
	int32 EditsLeft = 3;
	int32 NumWood = 0;
	int32 NumFood = 0;
	int32 NumIdle = 0;
	float SpeedMul = 1.f;

	TArray<FTSCScene> Scenes;
	int32 ActiveScene = -1;
	bool bCutOpen = false;
	int32 SelectedTake = 0;
	/** > 0 while the world is changing saga after an edit. */
	float TransitionTimer = 0.f;
	FString TransitionTitle;

	FString Banner;
	float BannerTimer = 0.f;
	TArray<FTSCToast> Toasts;
	TArray<FTSCTracer> Tracers;
	TArray<FString> Epilogue;
	FString EpilogueTitle;

	float GateHP = 0.f;
	float GateMaxHP = 1.f;
	float HallHP = 0.f;
	float HallMaxHP = 1.f;
	bool bGateClosed = false;
	bool bGateBroken = false;
	/** Seconds the leaves stay open after the last villager walked through the barred gate. */
	float GateVillagerLinger = 0.f;
	bool bHammerBlessing = false;
	bool bStagBlessing = false;

	UPROPERTY()
	TArray<TObjectPtr<ATSCUnit>> Units;
	/** Animals (separate from Units): prey looks here for wolves. */
	const TArray<TObjectPtr<ATSCUnit>>& GetWildlife() const { return Wildlife; }

	TArray<FTSCPlot> Plots;

	// ----- prologue behind the title screen (read by the HUD) ---------------------------------------------------------
	/** True while the landfall story plays behind the title. */
	bool bPrologue = false;
	FString ProCaption;
	float ProCaptionAlpha = 0.f;
	/** 0..1 black fade over the whole screen (loop restart). */
	float ProFade = 0.f;

	// ----- actions from the HUD ---------------------------------------------------------------------------------------
	void StartGame();
	void RestartGame();
	void AdjustJob(ETSCJob Job, int32 Delta);
	int32 BuildCost(TSC::EBuilding Type) const;
	bool CanBuild(TSC::EBuilding Type) const;
	int32 FreePlot() const;
	void StartBuild(TSC::EBuilding Type);
	bool CanCut() const;
	void PressCut();
	void SelectTake(int32 Take) { SelectedTake = Take; }
	void ConfirmCut();
	ETSCDayOnePrompt GetDayOnePrompt() const;
	bool IsFirstCutFramed() const;
	void ToggleSpeed();
	bool IsFrozen() const { return bCutOpen || TransitionTimer > 0.f || Phase == ETSCPhase::Title || Phase == ETSCPhase::Epilogue; }
	FString PhaseLabel() const;
	float PhaseFrac() const;

	// ----- services for units -----------------------------------------------------------------------------------------
	bool FindPath(const FVector& From, const FVector& To, TArray<FVector>& Out, int32 Layer = 0) const;
	const FTSCNavGrid& GetNav() const { return Nav; }
	/** Sidestep steering: Dir (2D unit) bent away from other units just ahead of U at Me. Returns a 2D unit vector. */
	FVector SteerAround(const ATSCUnit* U, const FVector& Me, const FVector& Dir) const;
	ATSCPrimActor* ReserveTimber(ATSCUnit* Worker, FVector& OutSpot);
	bool ValidateTimber(ATSCUnit* Worker, ATSCPrimActor* Tree, FVector& OutSpot) const;
	/** Returns 1 after felling, 2 after removing, 0 for an invalid claim. No resource award here. */
	int32 CompleteTimberStage(ATSCUnit* Worker, ATSCPrimActor* Tree);
	void ReleaseTimber(ATSCUnit* Worker);
	void SimTimber(float Dt);
	void TestTimber();
	int32 GridVersion = 0;
	bool CanReach(const ATSCUnit* A, const ATSCUnit* B) const;
	/** True if the world position is inside the palisade (the shore side in the east counts as inside up to the wall's end). */
	bool IsInsideSettlement(const FVector& P) const;
	/** Torvald, the lord of the settlement. */
	const ATSCUnit* GetJarl() const { return Shield; }
	void GetAssaultGoal(const ATSCUnit* U, FVector& OutPos, ETSCStructure& OutStruct) const;
	void DamageStructure(ETSCStructure S, float Amount);
	void AddTracer(const FVector& From, const FVector& To, const FLinearColor& Col);
	void OnUnitDied(ATSCUnit* U);
	void PlayDogSound(const ATSCUnit& Dog, ETSCDogSound Cue);
	void PlayAxeImpact(const FVector& Location, bool bWood);
	void Toast(const FString& Text);
	/** Visual only: how deep a unit standing at P is in the shallows (0 on land and on standing dock planks). */
	float GroundReliefAt(const FVector& P) const;
	float WadeDepthAt(const FVector& P) const;

	/** A footstep in the shallows at P moving along Dir. */
	void WadeStep(const FVector& P, const FVector& Dir);

	/** Test helper: plays the game by itself. Take pattern is a string of L/A per scene, e.g. "LALA". */
	UFUNCTION(Exec)
	void TSCAuto(const FString& Takes, const FString& Build, float Speed);
	/** Dev-only: "ru" switches the UI to Russian, anything else to English. No-op without UE/DevLoc/ru.tsv. */
	UFUNCTION(Exec)
	void TSCLang(const FString& Lang);
	UFUNCTION(Exec)
	void TSCLocMisses();
	UFUNCTION(Exec)
	void TSCSkip();
	/** Debug helper: jump to the start of day 1, 2 or 3 while keeping village progress. */
	UFUNCTION(Exec)
	void TSCDay(int32 TargetDay);
	/** Test helper: press CUT!, select a take (0/1) and optionally confirm. */
	UFUNCTION(Exec)
	void TSCCut(int32 Take, int32 bConfirm);
	/** Dev-only: logs the nav check counters (units that entered blocked cells, deep unit overlaps). */
	UFUNCTION(Exec)
	void TSCNavStats();

	static constexpr int32 GW = 34;
	static constexpr int32 GH = 24;

private:
	TArray<FTSCTimberTree> TimberTrees;

	// world
	void BuildAtmosphere();
	void BuildWorld();
	ATSCPrimActor* SpawnPrim(const FVector& Loc, int32 Region = 0);
	ATSCUnit* SpawnUnit(ETSCRole InRole, const FString& Name, const FVector& Loc);
	/** An animal roaming around Loc (Region: it turns Ashen with that part of the world). */
	ATSCUnit* SpawnWildlife(ETSCRole InRole, const FVector& Loc, float Radius, int32 Region);
	/** Deer and wolves in the forest, foxes on its edges, the stag in the forest, Hakon's husky. */
	void SpawnAllWildlife();
	FVector CellCenter(int32 Cx, int32 Cy) const;
	FVector CellCorner(int32 Cx, int32 Cy) const;
	FVector CellF(float Cx, float Cy) const;
	bool InGrid(int32 Cx, int32 Cy) const { return Cx >= 0 && Cy >= 0 && Cx < GW && Cy < GH; }
	bool IsBlocked(int32 Cx, int32 Cy) const { return !InGrid(Cx, Cy) || Blocked[Cy * GW + Cx] != 0; }
	void SetBlocked(int32 Cx, int32 Cy, bool bBlock);
	void BlockRect(int32 Cx, int32 Cy, int32 W, int32 H, bool bBlock = true);
	FIntPoint WorldToCell(const FVector& P) const;

	// flow
	void SimDay(float Dt);
	void SimNight(float Dt);
	void StartDay(int32 InDay);
	void StartNight();
	void EndNight(bool bHallFell);
	void BuildEpilogue(bool bHallFell);
	void EconomyTick();
	void ComputeRates(float& OutWood, float& OutFood) const;
	void UpdateJobs();
	/** Sends a named hero (Hakon, Sigrun, Torvald) on his own patrol loop inside the settlement; the three loops never meet. */
	void OrderPatrolRoute(ATSCUnit* U);
	void SimConstruction(float Dt);
	void UpdateAtmosphere(float Dt);
	void SimEmbers(float Dt);
	void SpawnEmbers(int32 Region, int32 Count);
	void SetGate(bool bClosed);
	/** Bars or frees the two gate cells for the enemy nav layer only. */
	void SetGateCells(bool bBar);
	/** Blocks the cells under the fallen gate spruce, or frees exactly those again. */
	void SetSpruceFallen(bool bFallen);
	/** Where the woodcutters stand to saw up the fallen spruce (I-th spot) and the trunk point they face. */
	void SpruceCutSpot(int32 I, FVector& OutStand, FVector& OutFace) const;
	void SimSpruceClearing(float Dt);
	void FinishSpruceClearing();
	void ShowBanner(const FString& Text, float Time);
	void AutoPlay(float Dt);
	/** Cross-fades to Sound; does nothing if it is already the current track. */
	void PlayMusic(USoundBase* Sound);

	UPROPERTY() TObjectPtr<UAudioComponent> MusicComp;
	UPROPERTY() TObjectPtr<USoundBase> CurrentMusic;
	UPROPERTY() TArray<TObjectPtr<USoundBase>> DogBarks;
	UPROPERTY() TObjectPtr<USoundBase> DogPant;
	UPROPERTY() TObjectPtr<USoundBase> DogHowl;
	UPROPERTY() TObjectPtr<USoundBase> DogGrowl;
	UPROPERTY() TObjectPtr<USoundBase> DogAttackBark;
	UPROPERTY() TObjectPtr<USoundAttenuation> DogAttenuation;
	UPROPERTY() TObjectPtr<USoundBase> BoarFleeSound;
	UPROPERTY() TObjectPtr<USoundBase> AxeWoodImpact;
	UPROPERTY() TObjectPtr<USoundBase> AxeEnemyImpact;
	UPROPERTY() TObjectPtr<USoundAttenuation> AxeAttenuation;
	FTimerHandle MusicTimer;

	// scenes
	void DefineScenes();
	void StartScene(int32 Idx);
	void SimScene(int32 Idx, float Dt);
	void ResolveLiving(int32 Idx);
	void ApplyAshen(int32 Idx);
	void FinishScene(int32 Idx);
	void TransitionRegion(int32 Region, const FVector& Origin);

	// prologue: the finished world is rewound (hidden, unbuilt, forested) and played forward behind the title
	void CollectLevelProps();
	void BuildPrologueExtras();
	void StartPrologue();
	void SimPrologue(float Dt);
	void EndPrologue();
	void ResetVillagers();
	void SetCameraView(const FVector& Loc, float Arm);

	UPROPERTY() TObjectPtr<ATSCPrimActor> Longship;
	/** Pivot at the foot of the longship's sail: Z scale 1 = hoisted, ~0 = lowered onto the deck. */
	UPROPERTY() TObjectPtr<USceneComponent> LongshipSail;
	void SetSailHoisted(float Alpha);
	FVector ShipMooring = FVector::ZeroVector;
	UPROPERTY() TArray<TObjectPtr<ATSCPrimActor>> WallSegs;
	UPROPERTY() TArray<TObjectPtr<ATSCPrimActor>> ProBuildings;
	UPROPERTY() TArray<TObjectPtr<ATSCPrimActor>> ProDressing;
	UPROPERTY() TArray<TObjectPtr<AActor>> ProLevelProps;
	/** The fence outside the wall (along the road from the gate), nearest to the gate first, with its level scale. Built at the end of the prologue. */
	UPROPERTY() TArray<TObjectPtr<AActor>> ProRoadFence;
	TArray<FVector> ProRoadFenceScale;
	/** Every visible level prop (static mesh actors and /Game/Props/ Blueprints), baked into the nav grid as obstacles. */
	UPROPERTY() TArray<TObjectPtr<AActor>> NavPropActors;
	void BakeNavProps();
	/** Soft unit separation: overlapping units are pushed apart through the nav grid. Runs after all Sim calls. */
	void SeparateUnits(float Dt);
	/** Dev checks (tsc.NavCheck, tsc.NavDebug, TSCAuto NAVTEST). */
	void CheckNav();
	void DrawNavDebug();
	void TestNav();
	int32 NavViolations = 0;
	int32 NavDeepOverlaps = 0;
	float NavMaxPen = 0.f;
	int32 NavCheckVersion = -1;
	float NavStatsTime = 0.f;
	UPROPERTY() TArray<TObjectPtr<ATSCPrimActor>> ProTrees;
	TArray<float> ProTreeFell;      // time each tree starts to fall
	TArray<float> ProTreeYaw;       // direction it falls in
	UPROPERTY() TArray<TObjectPtr<ATSCUnit>> ProWatchers;
	TArray<FVector> UnitHome;       // spawn position of Units[i] at the end of BuildWorld
	int32 NumHomeUnits = 0;
	float ProTime = 0.f;
	float ProDusk = 0.f;
	int32 ProLanded = 0;
	int32 ProStage = 0;
	FVector ProCamLoc = FVector::ZeroVector;
	float ProCamArm = 16500.f;

	TArray<uint8> Blocked;
	FTSCNavGrid Nav;
	UPROPERTY()
	TArray<TObjectPtr<ATSCPrimActor>> Prims;
	TArray<FTSCEmber> Embers;

	// special objects
	UPROPERTY() TObjectPtr<ATSCPrimActor> Gate;
	TArray<USceneComponent*> GateLeaves;
	float GateOpenAlpha = 1.f;
	UPROPERTY() TObjectPtr<ATSCPrimActor> EventTree;
	TArray<FIntPoint> SpruceCells;
	UPROPERTY() TObjectPtr<ATSCPrimActor> Garden;
	TArray<int32> CropParts;
	UPROPERTY() TObjectPtr<ATSCPrimActor> DockActor;
	TArray<int32> CrateParts;
	UPROPERTY() TObjectPtr<ATSCPrimActor> RaiderBoat;
	UPROPERTY() TObjectPtr<ATSCOcean> Ocean;
	UPROPERTY() TObjectPtr<ATSCTerrain> Terrain;
	UPROPERTY() TArray<TObjectPtr<ATSCPrimActor>> Flotsam;
	bool bProLandingPlayed = false;
	FVector AltarPos = FVector::ZeroVector; // the stone altar (SM_Altar) in the level: where the villagers pray
	bool bAltarFound = false;
	UPROPERTY() TObjectPtr<ATSCPrimActor> AltarGlow; // carries the altar's blessing lights when the level has no braziers
	/** The two braziers by the altar (BP_Brazier_01): [0] burns for the Hammer's blessing, [1] for the Stag's. Cold until asked. */
	UPROPERTY() TObjectPtr<AActor> AltarBraziers[2];
	float BrazierSettleTime = 0.6f; // the Blueprints light themselves on BeginPlay: put them out again once they have
	void SetBrazierLit(int32 Which, bool bLit);

	// named villagers and event actors
	UPROPERTY() TObjectPtr<ATSCUnit> Hunter;
	UPROPERTY() TObjectPtr<ATSCUnit> Woodcutter;
	UPROPERTY() TObjectPtr<ATSCUnit> Healer;
	UPROPERTY() TObjectPtr<ATSCUnit> Shield;
	UPROPERTY() TObjectPtr<ATSCUnit> Boar;
	UPROPERTY() TArray<TObjectPtr<ATSCUnit>> EventUnits;
	UPROPERTY() TArray<TObjectPtr<ATSCUnit>> Workers;
	/** Animals from Content/Animals: kept out of Units (no fights, no night clean-up), simulated alongside them. */
	UPROPERTY() TArray<TObjectPtr<ATSCUnit>> Wildlife;
	/** Cows, the bull and the village dog: not there until the village is built (prologue). */
	UPROPERTY() TArray<TObjectPtr<ATSCUnit>> FarmAnimals;
	/** Hakon's husky: appears with him when he lands (prologue). */
	UPROPERTY() TObjectPtr<ATSCUnit> HunterDog;

	// atmosphere
	UPROPERTY() TObjectPtr<AActor> AtmoActor;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> Fill;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> TopFill;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY() TObjectPtr<UPostProcessComponent> PostFx;

	// economy modifiers set by the scenes
	float ForestMul = 1.f;
	float GardenMul = 1.f;
	float GroveBonus = 3.f;
	float DockBonus = 3.f;
	float DayFoodMul = 1.f;
	int32 ExtraSawmills = 0;
	int32 ExtraHealers = 0;
	int32 HousesBuilt = 0;
	/** After the gate fight: the woodcutters saw up the fallen spruce and haul it off before the west gate opens. */
	bool bClearingSpruce = false;
	float SpruceClear = 0.f;           // 0..1
	FVector SpruceFullScale = FVector::OneVector;
	UPROPERTY() TArray<TObjectPtr<ATSCPrimActor>> SprucePieces;
	bool bShieldWounded = false;
	bool bHungryNight = false;
	bool bFedNight = false;
	float StartNightWood = 0.f;
	int32 NightDefendersLost = 0;
	int32 NightVillagersAtStart = 0;
	bool bWestBreached = false;
	bool bEastBreached = false;
	int32 EnemiesSlain = 0;
	int32 CreatureWaves[3] = { 3, 3, 4 };
	int32 RaiderWaves[2] = { 3, 3 };
	TArray<FString> BuiltNames;

	float EconomyTimer = 0.f;
	float WorldTime = 0.f;
	float AshAmount = 0.f;      // 0..1, how much of the world is Ashen (drives the colour drain)
	float FlashTimer = 0.f;
	int32 DayFlags = 0;
	bool bCameraSet = false;

	// autoplay (testing)
	bool bAuto = false;
	bool bDayOneOnboarding = false;
	bool bAssignedWoodOnDayOne = false;
	bool bAssignedFoodOnDayOne = false;
	bool bTriedFirstCut = false;
	bool bFramingFirstCut = false;
	FString AutoTakes;
	FString AutoBuild;

public:
	float GetFlash() const { return FlashTimer; }
	float GetWorldTime() const { return WorldTime; }
	FVector HallDoor() const { return CellF(17.f, 13.6f); }
	float WallX() const { return CellCorner(8, 0).X; }
};
