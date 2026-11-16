// The Saga Cutter - game jam project.

#pragma once

#include "CoreMinimal.h"
#include "TSCPrimActor.h"
#include "TSCNav.h"
#include "TSCUnit.generated.h"

class ATSCGameMode;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UMeshComponent;
class UAnimSequence;
class UMaterialInstanceDynamic;

/** Animation clips shared by the Viking characters (Content/Characters/viking_male, viking_female, viking_lord). */
enum class ETSCAnim : uint8 { Idle, Walk, Run, WalkCarry, RunCarry, Slash, Shoot, Punch, PickUp, Hit, Death, Num };

enum class ETSCTeam : uint8 { Villager, Creature, Raider, Animal };
/** Deer..Bull: wildlife and livestock from Content/Animals (team Animal, never fight, not targeted). Keep Deer first: IsWildlife() is Role >= Deer. */
enum class ETSCRole : uint8 { Worker, Hunter, Woodcutter, Healer, Shield, Creature, Raider, Boar, Deer, Stag, Fox, Wolf, Husky, Cow, Bull };
enum class ETSCJob : uint8 { Idle, Wood, Food };
enum class ETSCNeed : uint8 { None, Wood, Leaf, Heal };
/** Wander: roam around HoldPos (graze, rest, move on). Follow: keep near Leader (the hunter's dog). */
/** Patrol: the named heroes walk a closed loop of waypoints round the settlement (no work, no gathering). */
enum class ETSCOrder : uint8 { Work, Hold, Shelter, Leave, Assault, Wander, Follow, Patrol };
enum class ETSCStructure : uint8 { None, Gate, Longhouse };
/** Wildlife confinement relative to the palisade: Outside = forest animals (never cross the wall inwards), Inside = livestock (never leave). */
enum class ETSCZone : uint8 { Free, Outside, Inside };

/** An arrow mesh in flight from the hunter's bow to the target (purely visual, damage is dealt on release). */
struct FTSCArrowFlight
{
	FVector From = FVector::ZeroVector;
	FVector To = FVector::ZeroVector;
	/** 0..1 progress; >= 1 means finished and the component can be reused. */
	float T = 1.f;
	float Dur = 0.3f;
	/** True while the arrow is nocked on the bow being drawn (it follows the bow and has not been loosed yet). */
	bool bNocked = false;
};

/**
 * A villager, raider, mushroom creature or animal: a full 3D figure made of primitives that moves freely over the grid.
 * Animated by moving its parts (bobbing walk, swinging arm, drifting tendrils). Simulated by the game mode, not by Tick.
 */
UCLASS()
class ATSCUnit : public ATSCPrimActor
{
	GENERATED_BODY()

public:
	void Init(ATSCGameMode* InGM, ETSCRole InRole, const FString& InName);
	void Sim(float Dt);
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	bool IsTimberWorker() const;
	void CancelTimber();

	bool IsAlive() const { return !bDead && !bGone; }
	/** Alive and on the map (not sheltering indoors). */
	bool IsTargetable() const { return IsAlive() && !bSheltered; }
	void Hurt(float Amount);
	void Kill();

	void SetJob(ETSCJob InJob);
	void SetArmed(bool bInArmed);
	void OrderWork(const FVector& A, const FVector& B);
	void OrderHold(const FVector& Pos);
	void OrderShelter(const FVector& Pos);
	void OrderLeave(const FVector& Pos);
	void OrderAssault();
	/** Walk the closed loop Route over and over (starting at its nearest waypoint), pausing at every other corner to look around. */
	void OrderPatrol(const TArray<FVector>& Route);
	/** Wildlife: roam within Radius of Center. */
	void OrderWander(const FVector& Center, float Radius);
	/** Wildlife: stay close behind InLeader (falls back to wandering where it stood if the leader is gone). */
	void OrderFollow(ATSCUnit* InLeader);
	bool IsWildlife() const { return Role >= ETSCRole::Deer; }
	void Teleport(const FVector& Pos);
	FVector Loc2D() const { FVector L = GetActorLocation(); L.Z = 0.f; return L; }
	bool AtHoldPos() const;
	/** The husky has run up: this hero stops, turns to it and crouches to scratch it for Duration seconds. False if he is busy (not patrolling, fighting) or the dog is too far. */
	bool BeginPet(const FVector& DogPos, float Duration);
	/** Scripted figures that are not simulated: only the idle animation. */
	void Idle(float Dt);

	virtual void Refresh() override;

	ETSCTeam Team = ETSCTeam::Villager;
	ETSCRole Role = ETSCRole::Worker;
	ETSCJob Job = ETSCJob::Idle;
	ETSCNeed Need = ETSCNeed::None;
	ETSCOrder Order = ETSCOrder::Hold;
	/** Wildlife: which side of the palisade the animal must stay on (checked on every step). */
	ETSCZone Zone = ETSCZone::Free;
	FString DisplayName;
	bool bNamed = false;

	float HP = 60.f;
	float MaxHP = 60.f;
	float Damage = 8.f;
	float AttackRange = 120.f;
	float AttackInterval = 1.2f;
	float AggroRange = 700.f;
	float Speed = 190.f;
	bool bFights = false;
	bool bDefender = false;
	/** Scripted moments: cannot be hurt. */
	bool bInvulnerable = false;
	/** Scripted work (sawing up the fallen spruce): a plain chop-and-carry loop between WorkA and WorkB, no forest tree reservation. */
	bool bScriptWork = false;
	/** Where to face while chopping at WorkA (zero = keep the walking direction). */
	FVector WorkFace = FVector::ZeroVector;
	/** Seconds the health bar stays visible after a fight. */
	float BarTimer = 0.f;
	float Height = 120.f;

	bool bDead = false;
	bool bGone = false;
	bool bSheltered = false;
	bool bArmed = false;
	/** Lying down (ill or wounded), not moving. */
	bool bDowned = false;

private:
	friend class ATSCGameMode; // the dev-only integration check drives the real work cycle
	void BuildFigure();
	bool SimTimberWork(float Dt);
	TWeakObjectPtr<ATSCPrimActor> TimberTarget;
	float TimberRetry = 0.f;
	float TimberStageDuration = 0.f;
	int32 TimberPathVersion = -1;
	/** Humans: a skinned Viking (random man / woman, Torvald is the lord). Returns false if the assets are missing (then primitives are used). */
	bool BuildViking(const FLinearColor& Tunic);
	/** Wildlife: the skinned animal from Content/Animals/<Name>. False if missing (then nothing is shown). */
	bool BuildAnimal();
	/** Wildlife movement (Wander / Follow orders). Returns true while moving. */
	bool SimWildlife(float Dt);
	/** Weapon mesh on its hand socket (or the hand bone if the socket is missing), oriented, sized and gripped per Def.
	 *  bArmedOnly: shown only while armed; otherwise it is the always-held item (bow, staff). */
	UStaticMeshComponent* AddWeapon(const struct FTSCWeaponDef& Def, bool bArmedOnly);
	/** MIDs for every material slot of a mesh, so Refresh blends them to the Ashen look. */
	void AddTintedMaterials(UMeshComponent* Comp);
	/** Hunter: launch a visible arrow towards To. False if there is no arrow mesh (caller draws the tracer line). */
	bool FireArrow(const FVector& To);
	/** Hunter: nock an arrow and draw the bow; the arrow is loosed (and the hit lands) when the draw ends. */
	void BeginDraw(ATSCUnit* At);
	/** Abort the draw without shooting (target lost, unit downed). */
	void CancelDraw();
	/** Index of a free arrow in the pool (made if none is), or INDEX_NONE without the arrow mesh. */
	int32 AcquireArrow();
	/** Turn arrow Idx into a flight from its current place to To. */
	void LoosenArrow(int32 Idx, const FVector& To);
	void UpdateArrows(float Dt);
	/** Plays an animation on the Viking mesh. Loops keep running if already playing. Returns the clip length (0 if missing). */
	float PlayAnim(ETSCAnim Id, bool bLoop, float Rate = 1.f);
	/** Attack / work swing: arm swing for primitive figures, a one-shot clip for the Viking mesh.
	 *  Window > 0 speeds the clip up so it fits into that many seconds. */
	void StartSwing(float ArmTime, float Window, ETSCAnim Clip);
	bool StepToward(const FVector& Dest, float Dt, float StopDist);
	ATSCUnit* FindTarget() const;
	/** True when nothing solid (palisade, closed gate, fence, building) stands on the line between this archer and T. */
	bool HasShot(const ATSCUnit* T) const;
	void Animate(float Dt, bool bMoving);
	/** Sinks the unit into the shallows (visual only) and throws splashes while it walks there. */
	void UpdateWading(float Dt);
	float WadeStepTimer = 0.f;
	FVector LastWadePos = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<ATSCGameMode> GM;

	TWeakObjectPtr<ATSCUnit> Target;
	/** Scripted prey: while it lives, this unit attacks only it (the spruce-scene fog-born go for Hakon alone). */
	TWeakObjectPtr<ATSCUnit> Quarry;
	/** Mourning: seconds left standing still, facing MournAt, before the unit goes back to its order (Hakon's fall). */
	float MournTimer = 0.f;
	FVector MournAt = FVector::ZeroVector;
	/** Follow order: the unit to trail (the husky follows Hakon). */
	TWeakObjectPtr<ATSCUnit> Leader;
	/** Wander order: roaming radius around HoldPos, current goal and the pause before moving on. */
	float WanderRadius = 0.f;
	FVector WanderGoal = FVector::ZeroVector;
	float WanderPause = 0.f;
	/** Wildlife moving fast (gallop) rather than walking. */
	bool bGallop = false;
	/** Set by StepToward when the next step would cross the palisade against Zone (the animal stays put and picks a new goal). */
	bool bZoneBlocked = false;
	/** Village dog playing with the jarl: seconds left, pause before it may do so again, and its angle on the circle around him. */
	float PlayTimer = 0.f;
	float PlayCooldown = 0.f;
	float PlayAngle = 0.f;
	float DogRunTime = 0.f;
	float DogSoundCooldown = 0.f;
	bool bDogGrowled = false;
	/** A husky stops and barks at any boar within this distance. */
	float BoarAlertRadius = 650.f;
	/** Hakon's husky being scratched: seconds left and where it stands in front of him. */
	float PetTimer = 0.f;
	FVector PetSpot = FVector::ZeroVector;
	/** True if P is on the side of the palisade this animal is allowed on. */
	bool ZoneAllows(const FVector& P) const;
	/** A random wander goal around HoldPos that Zone allows (falls back to the current spot). */
	FVector PickWanderGoal() const;
	/** Prey (deer, stag, fox): seconds left running from FleeFrom (the last wolf seen within range). */
	float FleeTimer = 0.f;
	FVector FleeFrom = FVector::ZeroVector;
	/** Walking speed from Init (fleeing and following change Speed temporarily). */
	float BaseSpeed = 190.f;
	/** Patrol order: the loop, the waypoint being walked to, the pause at a corner and the time spent on this leg (skips an unreachable one). */
	TArray<FVector> PatrolRoute;
	int32 PatrolIdx = 0;
	float PatrolPause = 0.f;
	float PatrolLegTime = 0.f;
	FVector WorkA = FVector::ZeroVector;
	FVector WorkB = FVector::ZeroVector;
	FVector HoldPos = FVector::ZeroVector;
	int32 WorkPhase = 0;
	float WorkTimer = 0.f;

	TArray<FVector> Path;
	int32 PathIdx = 0;
	FVector PathDest = FVector::ZeroVector;
	int32 PathVersion = -1;
	/** Body radius against other units (cm). */
	float NavRadius = 28.f;
	int32 NavLayer() const
	{
		return IsWildlife() ? TSCNavLayer::Wildlife : (Team == ETSCTeam::Creature || Team == ETSCTeam::Raider) ? TSCNavLayer::Hostile : TSCNavLayer::People;
	}
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
	/** The destination CrowdWait was set for; a different destination cancels the wait. */
	FVector CrowdDest = FVector::ZeroVector;
	float LastStepTime = -1000.f;
	/** Dev check (tsc.NavCheck): was inside a blocked nav cell at the last check. */
	bool bNavWasBlocked = true;

	/** Worker self-defence by day: seconds the weapons stay out and the pause between enemy checks. */
	float SelfDefenseHold = 0.f;
	float SelfDefenseThink = 0.f;
	float AttackTimer = 0.f;
	float SwingTimer = 0.f;
	bool bUsesCombatAxe = false;
	float AnimPhase = 0.f;
	float DeathTimer = 0.f;
	float ThinkTimer = 0.f;

	UPROPERTY()
	TObjectPtr<USceneComponent> Body;
	UPROPERTY()
	TObjectPtr<USceneComponent> Arm;
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> Tendrils;
	int32 TunicPart = -1;
	TArray<int32> ArmedParts;
	TArray<int32> UnarmedParts;

	/** Viking skeletal mesh (humans only; null for primitive figures). */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;
	UPROPERTY()
	TArray<TObjectPtr<UAnimSequence>> Anims;
	UPROPERTY()
	TObjectPtr<UAnimSequence> CurAnim;
	/** One MID per material slot; the Living colour is blended to its Ashen version in Refresh. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> MeshMIDs;
	TArray<FLinearColor> MeshCols;
	int32 TunicSlot = -1;
	/** Hand bones and weapon size factor of the model in use (weapon fallback bone, see AddWeapon). */
	FName HandR;
	FName HandL;
	float WeaponScale = 1.f;
	/** While > 0 a one-shot clip (attack, hit, work) plays and locomotion does not override it. */
	float OneShotTimer = 0.f;
	float DiagT = 0.f; // TEMP DIAG (remove)

	/** Sword / axe / shield: visible only while armed. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ArmedItems;
	/** Bow or staff: always in hand. Arrows start from it. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> HeldItem;
	/** Workers: split logs / an armful of vegetables held in front of the chest while the carry clip plays (hidden otherwise). */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> CargoLogsComp;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> CargoVegComp;
	/** Mesh held in front of the chest, scaled to SizeFrac of the figure height. Null if the mesh is missing. */
	UStaticMeshComponent* AddCargo(TArrayView<const TCHAR* const> Paths, float SizeFrac);
	/** Shows the work axe while chopping and the cargo while carrying. */
	void UpdateHeld();
	/** Arrow pool, parallel to Arrows. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ArrowComps;
	TArray<FTSCArrowFlight> Arrows;
	/** Bow draw in progress: seconds left, the full duration, the target and the nocked arrow's pool index. */
	float DrawTimer = 0.f;
	float DrawTotal = 0.f;
	TWeakObjectPtr<ATSCUnit> DrawTarget;
	FVector DrawAim = FVector::ZeroVector;
	int32 NockedIdx = INDEX_NONE;
	/** Bow pose: blend weight (0 = the clip, 1 = both arms in the draw pose) and how far the string hand is drawn (0..1). */
	float BowPoseW = 0.f;
	float DrawPull = 0.f;
	/** Called after the animation is evaluated: poses both arms for the draw (two-bone IK on top of the clip). */
	void ApplyBowPose();
};
