// The Saga Cutter - game jam project.

#include "TSCUnit.h"

#include "TSCBuilders.h"
#include "TSCGameMode.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"

using namespace TSC;

namespace
{
	/** A Viking character imported into Content/Characters/<folder>: skeletal mesh + one AnimSequence per clip. */
	struct FVikingModel
	{
		const TCHAR* Folder;
		const TCHAR* Mesh;
		const TCHAR* AnimPrefix;
		/** Hand bones (weapon fallback when a socket is missing) and a multiplier on the weapon size. */
		const TCHAR* HandR;
		const TCHAR* HandL;
		float WeaponScale;
		/** Tripo axolotl model: random size / tint only for unnamed copies. */
		bool bAxolotl;
	};
	const FVikingModel VikingMale{ TEXT("/Game/Characters/viking_male"), TEXT("Viking_Male"), TEXT("Viking_MaleCharacterArmature_"), TEXT("Fist_R"), TEXT("Fist_L"), 1.f, false };
	/** The unnamed villager (Tripo axolotl warrior). Optional: if its mesh is missing the Viking models are used. */
	const FVikingModel AxolotlWarrior{ TEXT("/Game/Characters/axolotl_warrior"), TEXT("Axolotl_Warrior"), TEXT("Axolotl_WarriorCharacterArmature_"), TEXT("RightHand"), TEXT("LeftHand"), 1.f, true };
	/** The four named heroes (Tripo axolotls), each optional: a missing mesh falls back to the Viking models. */
	const FVikingModel AxolotlArcher{ TEXT("/Game/Characters/axolotl_archer"), TEXT("Axolotl_Archer"), TEXT("Axolotl_ArcherCharacterArmature_"), TEXT("RightHand"), TEXT("LeftHand"), 1.f, true };
	const FVikingModel AxolotlDruid{ TEXT("/Game/Characters/axolotl_druid"), TEXT("Axolotl_Druid"), TEXT("Axolotl_DruidCharacterArmature_"), TEXT("RightHand"), TEXT("LeftHand"), 1.f, true };
	const FVikingModel AxolotlBlacksmith{ TEXT("/Game/Characters/axolotl_blacksmith"), TEXT("Axolotl_Blacksmith"), TEXT("Axolotl_BlacksmithCharacterArmature_"), TEXT("RightHand"), TEXT("LeftHand"), 1.f, true };
	const FVikingModel AxolotlJarl{ TEXT("/Game/Characters/axolotl_jarl"), TEXT("Axolotl_Jarl"), TEXT("Axolotl_JarlCharacterArmature_"), TEXT("RightHand"), TEXT("LeftHand"), 1.f, true };
	/** The three raider variants (Tripo masked frog / bird / spotted amphibian), each optional like the others. */
	const FVikingModel AxolotlFrog{ TEXT("/Game/Characters/axolotl_frog"), TEXT("Axolotl_Frog"), TEXT("Axolotl_FrogCharacterArmature_"), TEXT("RightHand"), TEXT("LeftHand"), 1.f, true };
	const FVikingModel AxolotlBird{ TEXT("/Game/Characters/axolotl_bird"), TEXT("Axolotl_Bird"), TEXT("Axolotl_BirdCharacterArmature_"), TEXT("RightHand"), TEXT("LeftHand"), 1.f, true };
	const FVikingModel AxolotlAmphibian{ TEXT("/Game/Characters/axolotl_amphibian"), TEXT("Axolotl_Amphibian"), TEXT("Axolotl_AmphibianCharacterArmature_"), TEXT("RightHand"), TEXT("LeftHand"), 1.f, true };

	/**
	 * Shuffle bag for picking a model variant: random order, every variant once before any repeats, and the first pick
	 * of a refilled bag is never the last pick of the previous one. So units spawned together look different.
	 */
	struct FTSCVariantBag
	{
		TArray<int32> Order;
		int32 Last = INDEX_NONE;

		int32 Next(int32 Num)
		{
			if (Num <= 1) { return 0; }
			if (Order.IsEmpty() || Order.ContainsByPredicate([Num](int32 V) { return V >= Num; }))
			{
				Order.Reset(Num);
				for (int32 I = 0; I < Num; ++I) { Order.Add(I); }
				for (int32 I = Num - 1; I > 0; --I) { Order.Swap(I, FMath::RandRange(0, I)); }
				// Picks pop from the back: keep the previous pick away from the front of the new round.
				if (Order.Last() == Last) { Order.Swap(Num - 1, FMath::RandRange(0, Num - 2)); }
			}
			Last = Order.Pop(EAllowShrinking::No);
			return Last;
		}
	};
	FTSCVariantBag RaiderBag;
	FTSCVariantBag CreatureBag;

	const TCHAR* VikingAnimNames[] = { TEXT("Idle"), TEXT("Walk"), TEXT("Run"), TEXT("Walk_Carry"), TEXT("Run_Carry"),
		TEXT("SwordSlash"), TEXT("Shoot_OneHanded"), TEXT("Punch"), TEXT("PickUp"), TEXT("RecieveHit"), TEXT("Death") };
	static_assert(UE_ARRAY_COUNT(VikingAnimNames) == (int32)ETSCAnim::Num, "One clip name per ETSCAnim");

	/** Colour parameter of the imported FBX materials. */
	const FName DiffuseParam(TEXT("DiffuseColor"));

	/** The MID's DiffuseColor, or white if the material has no such parameter (K2_GetVectorParameterValue would give black). */
	FLinearColor DiffuseOrWhite(UMaterialInstanceDynamic* MID)
	{
		FLinearColor Col = FLinearColor::White;
		if (!MID || !MID->GetVectorParameterValue(FHashedMaterialParameterInfo(DiffuseParam), Col)) { return FLinearColor::White; }
		return Col;
	}

	// Tuning: the FBX faces +Y, units walk along +X.
	constexpr float VikingYaw = -90.f;
	/** Height of the figure in cm (named heroes are 12% taller), same as the old primitive figure. */
	constexpr float VikingHeight = 120.f;
	// Weapon meshes (Content/Weapons). Several candidates where the folder name may still change:
	// the axe folder was created with a Cyrillic "Рђ" (U+0410), the arrow folder is spelt "Errow".
	const TCHAR* const WeaponAxe[] = { TEXT("/Game/Weapons/Axe/Axe_small.Axe_small"), TEXT("/Game/Weapons/Рђxe/Axe_small.Axe_small") };
	const TCHAR* const WeaponSword[] = { TEXT("/Game/Weapons/Sword/Sword.Sword") };
	const TCHAR* const WeaponShield[] = { TEXT("/Game/Weapons/Shield/Shield_Round.Shield_Round") };
	const TCHAR* const WeaponBow[] = { TEXT("/Game/Weapons/Bow/Bow_Wooden.Bow_Wooden") };
	const TCHAR* const WeaponStaff[] = { TEXT("/Game/Weapons/Staff/Staff.Staff") };
	const TCHAR* const WeaponArrow[] = { TEXT("/Game/Weapons/Arrow/Arrow.Arrow"), TEXT("/Game/Weapons/Errow/Arrow.Arrow") };

	// What the workers carry home (the carry clip plays while they walk to the store): split logs and an armful of vegetables.
	const TCHAR* const CargoLogs[] = { TEXT("/Game/Props/WoodenFurniture/SM_WoodenFurniture_05.SM_WoodenFurniture_05") };
	const TCHAR* const CargoVegetables[] = { TEXT("/Game/Props/VegetablePlants/SM_VegetablePlant_03.SM_VegetablePlant_03") };
	/** Longest side of the carried bundle as a fraction of the figure height, and where it is held (in front of the chest, actor space). */
	constexpr float CargoLogsSize = 0.42f;
	constexpr float CargoVegetablesSize = 0.36f;
	const FVector CargoHold(32.f, 0.f, 62.f);

	UStaticMesh* LoadFirstMesh(TArrayView<const TCHAR* const> Paths)
	{
		for (const TCHAR* Path : Paths)
		{
			// FindObject first so a missing candidate path does not log a load warning every spawn.
			if (UStaticMesh* SM = FindObject<UStaticMesh>(nullptr, Path)) { return SM; }
			if (FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(FString(Path))))
			{
				if (UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, Path)) { return SM; }
			}
		}
		return nullptr;
	}

	/** Rotation that turns a mesh so its local LocalX axis lies on +X and LocalZ on +Z. */
	FQuat AxesToXZ(const FVector& LocalX, const FVector& LocalZ)
	{
		return FRotationMatrix::MakeFromXZ(LocalX, LocalZ).ToQuat().Inverse();
	}

	/** Arrow mesh: tip at +Z (fletching at -Z); turned so the tip points along +X, the flight direction. */
	const FQuat ArrowMeshRot = AxesToXZ(FVector(0.f, 0.f, 1.f), FVector(1.f, 0.f, 0.f));
	/** Arrow length as a fraction of the figure height. */
	constexpr float ArrowSize = 0.5f;
	/** How long the hunter draws the bow before the arrow is loosed (s), and how far (cm) the arrow's tail is pulled back. */
	constexpr float BowDrawTime = 0.45f;
	constexpr float BowPullDist = 26.f;

	/**
	 * Wildlife in Content/Animals/<Name>: mesh <Name>, clips <Name>AnimalArmature_<Clip>; Length = nose to tail in cm.
	 * Folder overrides the package folder (the mushroom creatures live in /Game/Characters/mushroom_<key>); with bFitHeight
	 * the Length is the standing height instead. Several entries with the same Role: one is picked at random per unit.
	 */
	struct FTSCAnimalModel
	{
		ETSCRole Role;
		const TCHAR* Name;
		float Length;
		const TCHAR* Attack;
		const TCHAR* Folder = nullptr;
		bool bFitHeight = false;
	};
	const FTSCAnimalModel AnimalModels[] = {
		{ ETSCRole::Boar, TEXT("Boar"), 130.f, TEXT("Attack_Headbutt") },
		{ ETSCRole::Deer, TEXT("Deer"), 140.f, TEXT("Attack_Headbutt") },
		{ ETSCRole::Stag, TEXT("Stag"), 175.f, TEXT("Attack_Headbutt") },
		{ ETSCRole::Fox, TEXT("Fox"), 75.f, TEXT("Attack") },
		{ ETSCRole::Wolf, TEXT("Wolf"), 120.f, TEXT("Attack") },
		{ ETSCRole::Husky, TEXT("Husky"), 100.f, TEXT("Attack") },
		{ ETSCRole::Cow, TEXT("Cow"), 210.f, TEXT("Attack_Headbutt") },
		{ ETSCRole::Bull, TEXT("Bull"), 230.f, TEXT("Attack_Headbutt") },
		// Night enemy (Tripo mushrooms on the Bull skeleton): standing heights in cm, purple is the tallest.
		{ ETSCRole::Creature, TEXT("Mushroom_Purple"), 140.f, TEXT("Attack_Headbutt"), TEXT("/Game/Characters/mushroom_purple"), true },
		{ ETSCRole::Creature, TEXT("Mushroom_Cup"), 85.f, TEXT("Attack_Headbutt"), TEXT("/Game/Characters/mushroom_cup"), true },
		{ ETSCRole::Creature, TEXT("Mushroom_Morel"), 130.f, TEXT("Attack_Headbutt"), TEXT("/Game/Characters/mushroom_morel"), true },
	};
	/** How far (cm) the mushroom's body rams forward and back during the swing. */
	constexpr float CreatureLunge = 55.f;
	// Tuning: the animal FBX are assumed to face +Y like the Vikings; units walk along +X.
	constexpr float AnimalYaw = -90.f;
	/** Prey flees from a wolf closer than this (cm; 3.5 cells). */
	constexpr float FleeRadius = 700.f;
	/** Prey keeps running this long after the last wolf sighting (s). */
	constexpr float FleeLinger = 2.5f;
	/** Forest animals run from any person this close (cm; 3 cells). */
	constexpr float HumanFleeRadius = 600.f;
	/** The village dog notices the jarl from this far (cm; 3.5 cells). */
	constexpr float JarlNoticeRadius = 700.f;
	/** The husky attacks enemies this close to its master (cm; 3 cells). */
	constexpr float GuardRadius = 450.f; // the pet's leash: it fights only enemies this close to its master
	constexpr float PetDuration = 4.f;  // Hakon scratching the husky
	/** Gallop speed while fleeing, relative to the walking speed. */
	constexpr float FleeSpeedMul = 2.6f;
}

/**
 * How a weapon mesh sits in a hand socket. Sockets on the Viking skeletons (made by script, editable in the
 * Skeleton Editor) follow one convention: +Z = where the shaft / blade points, +X = edge / facing / shooting side.
 * Each mesh says which of its local axes map onto those, where it is gripped, and how big it is.
 */
struct FTSCWeaponDef
{
	TArrayView<const TCHAR* const> Paths;
	FName Socket;
	FName FallbackBone;
	/** Local axis of the mesh that goes to the socket's +X / +Z. */
	FVector LocalX;
	FVector LocalZ;
	/** Grip point in mesh units (lands on the socket). */
	FVector Grip;
	/** Longest dimension as a fraction of the figure height (the imported meshes come in mixed scales). */
	float Size;
};

namespace
{
	// Measured from the meshes: sword blade -Y, edges +/-X; axe head -Y, edge +X, origin right under the head;
	// shield face (boss) -Y; bow limbs along Y, bending towards -X (the target side); staff up +Z.
	const FTSCWeaponDef DefSword{ WeaponSword, TEXT("weapon_r"), TEXT("Fist_R"), FVector(1, 0, 0), FVector(0, -1, 0), FVector(0, 0.03f, 0), 0.55f };
	const FTSCWeaponDef DefAxe{ WeaponAxe, TEXT("weapon_r"), TEXT("Fist_R"), FVector(1, 0, 0), FVector(0, -1, 0), FVector(0, 0.45f, 0), 0.45f };
	const FTSCWeaponDef DefShield{ WeaponShield, TEXT("shield_l"), TEXT("Fist_L"), FVector(0, -1, 0), FVector(0, 0, 1), FVector::ZeroVector, 0.4f };
	const FTSCWeaponDef DefBow{ WeaponBow, TEXT("bow_l"), TEXT("Fist_L"), FVector(-1, 0, 0), FVector(0, 1, 0), FVector::ZeroVector, 0.756f };
	const FTSCWeaponDef DefStaff{ WeaponStaff, TEXT("staff_r"), TEXT("Fist_R"), FVector(1, 0, 0), FVector(0, 0, 1), FVector::ZeroVector, 0.5f };
}

void ATSCUnit::Init(ATSCGameMode* InGM, ETSCRole InRole, const FString& InName)
{
	GM = InGM;
	Role = InRole;
	DisplayName = InName;
	bNamed = !InName.IsEmpty();
	AnimPhase = FMath::FRand() * 6.f;

	switch (Role)
	{
	case ETSCRole::Worker: Team = ETSCTeam::Villager; MaxHP = 60.f; Damage = 9.f; AggroRange = 450.f; break; // by day only fights what comes close
	case ETSCRole::Hunter: Team = ETSCTeam::Villager; MaxHP = 130.f; Damage = 13.f; AttackRange = 950.f; AttackInterval = 1.0f; AggroRange = 1000.f; bFights = true; break;
	case ETSCRole::Woodcutter: Team = ETSCTeam::Villager; MaxHP = 90.f; Damage = 14.f; break;
	case ETSCRole::Healer: Team = ETSCTeam::Villager; MaxHP = 50.f; Damage = 4.f; break;
	case ETSCRole::Shield: Team = ETSCTeam::Villager; MaxHP = 170.f; Damage = 13.f; bFights = true; break;
	case ETSCRole::Creature: Team = ETSCTeam::Creature; MaxHP = 44.f; Damage = 7.f; Speed = 105.f; AttackInterval = 1.5f; AggroRange = 600.f; bFights = true; Height = 170.f; AttackRange = 130.f; break;
	case ETSCRole::Raider: Team = ETSCTeam::Raider; MaxHP = 62.f; Damage = 10.f; Speed = 165.f; AttackInterval = 1.25f; AggroRange = 650.f; bFights = true; break;
	case ETSCRole::Boar: Team = ETSCTeam::Animal; MaxHP = 50.f; Damage = 0.f; Speed = 250.f; Height = 80.f; break;
	case ETSCRole::Deer: Team = ETSCTeam::Animal; MaxHP = 40.f; Damage = 0.f; Speed = 130.f; Height = 110.f; break;
	case ETSCRole::Stag: Team = ETSCTeam::Animal; MaxHP = 60.f; Damage = 0.f; Speed = 110.f; Height = 150.f; break;
	case ETSCRole::Fox: Team = ETSCTeam::Animal; MaxHP = 20.f; Damage = 0.f; Speed = 150.f; Height = 50.f; break;
	case ETSCRole::Wolf: Team = ETSCTeam::Animal; MaxHP = 50.f; Damage = 0.f; Speed = 150.f; Height = 80.f; break;
	case ETSCRole::Husky: Team = ETSCTeam::Animal; MaxHP = 40.f; Damage = 7.f; Speed = 230.f; Height = 70.f; AttackRange = 130.f; AttackInterval = 1.0f; AggroRange = 500.f; bFights = true; break;
	case ETSCRole::Cow: Team = ETSCTeam::Animal; MaxHP = 80.f; Damage = 0.f; Speed = 70.f; Height = 140.f; break;
	case ETSCRole::Bull: Team = ETSCTeam::Animal; MaxHP = 110.f; Damage = 0.f; Speed = 80.f; Height = 150.f; break;
	}
	switch (Role)
	{
	case ETSCRole::Fox: NavRadius = 18.f; break;
	case ETSCRole::Husky: NavRadius = 22.f; break;
	case ETSCRole::Wolf: NavRadius = 26.f; break;
	case ETSCRole::Creature: case ETSCRole::Boar: case ETSCRole::Deer: NavRadius = 30.f; break;
	case ETSCRole::Stag: NavRadius = 34.f; break;
	case ETSCRole::Cow: NavRadius = 40.f; break;
	case ETSCRole::Bull: NavRadius = 44.f; break;
	default: NavRadius = 28.f; break; // Worker, Hunter, Woodcutter, Healer, Shield, Raider
	}
	BaseSpeed = Speed;
	HP = MaxHP;
	BuildFigure();
	Refresh();
}

void ATSCUnit::BuildFigure()
{
	Body = AddPivot(FVector::ZeroVector);

	if (Role == ETSCRole::Creature)
	{
		// One of the three mushroom models (random per unit); the primitive figure below is the fallback.
		if (BuildAnimal())
		{
			return;
		}
		// Flattened sphere cap with glowing spots, tendrils hanging down.
		const FLinearColor Pale(0.75f, 0.85f, 0.78f);
		const int32 Cap = AddPart(ETSCShape::Sphere, FVector(0.f, 0.f, 130.f), FRotator::ZeroRotator, FVector(1.05f, 1.05f, 0.5f), Pale, Body);
		SetGlow(Cap, FLinearColor(0.25f, 0.6f, 0.4f), FLinearColor(0.25f, 0.6f, 0.4f));
		AddPart(ETSCShape::Sphere, FVector(0.f, 0.f, 112.f), FRotator::ZeroRotator, FVector(0.7f, 0.7f, 0.3f), Pale * 0.5f, Body);
		for (int32 I = 0; I < 6; ++I)
		{
			const FVector D = FRotator(0.f, I * 60.f + 20.f, 0.f).Vector();
			const int32 Spot = AddPart(ETSCShape::Sphere, FVector(D.X * 30.f, D.Y * 30.f, 147.f), FRotator::ZeroRotator, FVector(0.16f, 0.16f, 0.1f), Pal::Shroom, Body);
			SetGlow(Spot, FLinearColor(2.f, 9.f, 5.f), FLinearColor(2.f, 9.f, 5.f));
		}
		for (int32 I = 0; I < 6; ++I)
		{
			const FVector D = FRotator(0.f, I * 60.f, 0.f).Vector();
			USceneComponent* Tp = AddPivot(FVector(D.X * 26.f, D.Y * 26.f, 112.f), Body);
			const float Len = 0.7f + 0.25f * (I % 3);
			const int32 Td = AddPart(ETSCShape::Cylinder, FVector(0.f, 0.f, -50.f * Len), FRotator::ZeroRotator, FVector(0.07f, 0.07f, Len), Pale * 0.8f, Tp);
			SetGlow(Td, FLinearColor(0.1f, 0.4f, 0.25f), FLinearColor(0.1f, 0.4f, 0.25f));
			Tendrils.Add(Tp);
		}
		return;
	}

	if ((Role == ETSCRole::Boar || IsWildlife()) && BuildAnimal())
	{
		return;
	}

	// Animal asset missing: use a primitive quadruped.
	if (Role == ETSCRole::Boar || IsWildlife())
	{
		const FLinearColor Hide(0.13f, 0.08f, 0.05f);
		AddPart(ETSCShape::Sphere, FVector(0.f, 0.f, 44.f), FRotator::ZeroRotator, FVector(1.0f, 0.56f, 0.56f), Hide, Body);
		AddPart(ETSCShape::Sphere, FVector(46.f, 0.f, 40.f), FRotator::ZeroRotator, FVector(0.46f, 0.4f, 0.4f), Hide * 0.8f, Body);
		AddPart(ETSCShape::Cone, FVector(66.f, 12.f, 40.f), FRotator(-50.f, 0.f, 0.f), FVector(0.07f, 0.07f, 0.22f), FLinearColor(0.8f, 0.8f, 0.7f), Body);
		AddPart(ETSCShape::Cone, FVector(66.f, -12.f, 40.f), FRotator(-50.f, 0.f, 0.f), FVector(0.07f, 0.07f, 0.22f), FLinearColor(0.8f, 0.8f, 0.7f), Body);
		for (int32 I = 0; I < 4; ++I)
		{
			AddPart(ETSCShape::Cylinder, FVector(I < 2 ? 26.f : -26.f, I % 2 ? 14.f : -14.f, 12.f), FRotator::ZeroRotator, FVector(0.1f, 0.1f, 0.24f), Hide * 0.6f, Body);
		}
		return;
	}

	// Stocky human figure: cylinder body, sphere head, cone helmet, round shield, axe on a swinging arm.
	FLinearColor Tunic(0.20f, 0.22f, 0.27f);
	switch (Role)
	{
	case ETSCRole::Hunter: Tunic = FLinearColor(0.10f, 0.28f, 0.10f); break;
	case ETSCRole::Woodcutter: Tunic = FLinearColor(0.50f, 0.20f, 0.05f); break;
	case ETSCRole::Healer: Tunic = FLinearColor(0.62f, 0.60f, 0.52f); break;
	case ETSCRole::Shield: Tunic = FLinearColor(0.10f, 0.18f, 0.45f); break;
	case ETSCRole::Raider: Tunic = FLinearColor(0.07f, 0.05f, 0.05f); break;
	default: break;
	}
	const bool bRaider = Role == ETSCRole::Raider;
	const float S = bNamed ? 1.12f : 1.f;

	if (BuildViking(Tunic))
	{
		bArmed = true;
		SetArmed(bRaider || Role == ETSCRole::Shield);
		return;
	}

	// Fallback (Viking character assets missing): figure made of primitives.
	TunicPart = AddPart(ETSCShape::Cylinder, FVector(0.f, 0.f, 40.f * S), FRotator::ZeroRotator, FVector(0.58f * S, 0.5f * S, 0.62f * S), Tunic, Body);
	AddPart(ETSCShape::Cylinder, FVector(0.f, 0.f, 36.f * S), FRotator::ZeroRotator, FVector(0.6f * S, 0.52f * S, 0.07f), Pal::TimberDark, Body); // belt
	AddPart(ETSCShape::Sphere, FVector(0.f, 0.f, 86.f * S), FRotator::ZeroRotator, FVector(0.4f * S), Pal::Skin, Body);
	AddPart(ETSCShape::Sphere, FVector(9.f, 0.f, 74.f * S), FRotator::ZeroRotator, FVector(0.3f * S, 0.32f * S, 0.26f * S),
		bRaider ? FLinearColor(0.05f, 0.04f, 0.03f) : FLinearColor(0.3f, 0.16f, 0.06f), Body); // beard

	// Helmet (armed) or cap/hood (unarmed).
	const int32 Helm = AddPart(ETSCShape::Cone, FVector(0.f, 0.f, 112.f * S), FRotator::ZeroRotator, FVector(0.42f * S, 0.42f * S, 0.34f * S), Pal::Iron * 1.6f, Body);
	const int32 HelmRim = AddPart(ETSCShape::Cylinder, FVector(0.f, 0.f, 98.f * S), FRotator::ZeroRotator, FVector(0.44f * S, 0.44f * S, 0.05f), Pal::Iron, Body);
	ArmedParts.Add(Helm);
	ArmedParts.Add(HelmRim);
	if (Role == ETSCRole::Hunter || Role == ETSCRole::Healer)
	{
		const int32 Hood = AddPart(ETSCShape::Cone, FVector(-4.f, 0.f, 108.f * S), FRotator(-12.f, 0.f, 0.f), FVector(0.46f * S, 0.46f * S, 0.4f * S), Tunic * 0.8f, Body);
		UnarmedParts.Add(Hood);
	}
	else if (!bRaider)
	{
		const int32 Hair = AddPart(ETSCShape::Sphere, FVector(-5.f, 0.f, 95.f * S), FRotator::ZeroRotator, FVector(0.4f * S, 0.4f * S, 0.3f * S), FLinearColor(0.3f, 0.16f, 0.06f), Body);
		UnarmedParts.Add(Hair);
	}

	// Round shield on the left side.
	const FLinearColor ShieldCol = bRaider ? FLinearColor(0.4f, 0.03f, 0.03f) : (Role == ETSCRole::Shield ? FLinearColor(0.75f, 0.6f, 0.1f) : FLinearColor(0.12f, 0.3f, 0.16f));
	const int32 Sh = AddPart(ETSCShape::Cylinder, FVector(12.f, -34.f * S, 50.f * S), FRotator(0.f, 0.f, 90.f), FVector(0.56f * S, 0.56f * S, 0.06f), ShieldCol, Body);
	const int32 Boss = AddPart(ETSCShape::Sphere, FVector(12.f, -38.f * S, 50.f * S), FRotator::ZeroRotator, FVector(0.16f), Pal::Iron * 2.f, Body);
	ArmedParts.Add(Sh);
	ArmedParts.Add(Boss);

	// Right arm with a tool: axe, or a bow for the hunter.
	Arm = AddPivot(FVector(0.f, 32.f * S, 62.f * S), Body);
	AddPart(ETSCShape::Cube, FVector(14.f, 0.f, -8.f), FRotator(-30.f, 0.f, 0.f), FVector(0.12f, 0.12f, 0.42f), Tunic * 0.8f, Arm);
	if (Role == ETSCRole::Hunter)
	{
		AddPart(ETSCShape::Cube, FVector(34.f, 0.f, 4.f), FRotator::ZeroRotator, FVector(0.05f, 0.05f, 1.0f), Pal::Timber * 1.5f, Arm);
		AddPart(ETSCShape::Cube, FVector(28.f, 0.f, 4.f), FRotator::ZeroRotator, FVector(0.015f, 0.015f, 0.95f), FLinearColor(0.8f, 0.8f, 0.7f), Arm);
	}
	else if (Role != ETSCRole::Healer)
	{
		AddPart(ETSCShape::Cube, FVector(30.f, 0.f, 12.f), FRotator::ZeroRotator, FVector(0.05f, 0.05f, 0.7f), Pal::Timber * 1.4f, Arm);
		AddPart(ETSCShape::Cube, FVector(38.f, 0.f, 40.f), FRotator::ZeroRotator, FVector(0.24f, 0.04f, 0.18f), Pal::Iron * 2.2f, Arm);
	}
	else
	{
		AddPart(ETSCShape::Sphere, FVector(26.f, 0.f, -20.f), FRotator::ZeroRotator, FVector(0.2f), FLinearColor(0.2f, 0.5f, 0.15f), Arm); // herb pouch
	}

	bArmed = true;
	SetArmed(bRaider || Role == ETSCRole::Shield);
}

bool ATSCUnit::BuildViking(const FLinearColor& Tunic)
{
	// Axolotl models (Tripo) are preferred when their assets exist: unnamed workers are the warrior, the four named heroes
	// have their own class model (archer, druid, blacksmith, jarl); raiders are a random frog / bird / amphibian. Anything else stays Viking. Fallback when the
	// axolotl mesh is missing: Torvald (the named shield-bearer) is the lord; named heroes have a fixed sex (Sigrun the Healer
	// is a woman); everyone else (workers, raiders) is a man or a woman at random.
	auto LoadMesh = [](const FVikingModel& M) -> USkeletalMesh*
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), M.Folder, M.Mesh, M.Mesh);
		if (USkeletalMesh* SK = FindObject<USkeletalMesh>(nullptr, *Path)) { return SK; }
		// Quiet: a missing optional model must not log a load warning on every spawn.
		if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path))) { return nullptr; }
		return LoadObject<USkeletalMesh>(nullptr, *Path);
	};
	const FVikingModel* Model = &VikingMale;
	USkeletalMesh* SkMesh = nullptr;
	const FVikingModel* Preferred = nullptr;
	if (bNamed)
	{
		switch (Role)
		{
		case ETSCRole::Hunter: Preferred = &AxolotlArcher; break;
		case ETSCRole::Healer: Preferred = &AxolotlDruid; break;
		case ETSCRole::Woodcutter: Preferred = &AxolotlBlacksmith; break;
		case ETSCRole::Shield: Preferred = &AxolotlJarl; break;
		default: break;
		}
	}
	else if (Role == ETSCRole::Worker)
	{
		Preferred = &AxolotlWarrior;
	}
	else if (Role == ETSCRole::Raider)
	{
		// Raiders are one of three masked variants from a shuffle bag (random, but different ones in a row); a missing mesh
		// tries the next variant, and only with none present do the Vikings below take over.
		const FVikingModel* Variants[] = { &AxolotlFrog, &AxolotlBird, &AxolotlAmphibian };
		const int32 Start = RaiderBag.Next(UE_ARRAY_COUNT(Variants));
		for (int32 K = 0; K < UE_ARRAY_COUNT(Variants) && !SkMesh; ++K)
		{
			const FVikingModel* V = Variants[(Start + K) % UE_ARRAY_COUNT(Variants)];
			SkMesh = LoadMesh(*V);
			if (SkMesh) { Model = V; }
		}
	}
	if (Preferred && !SkMesh)
	{
		SkMesh = LoadMesh(*Preferred);
		if (SkMesh) { Model = Preferred; }
	}
	if (!SkMesh)
	{
		// No axolotl mesh loaded: the male Viking is the last resort.
		Model = &VikingMale;
		SkMesh = LoadMesh(*Model);
	}
	const bool bAxolotl = Model->bAxolotl;
	if (!SkMesh) { return false; }

	const bool bRaider = Role == ETSCRole::Raider;
	const float S = bNamed ? 1.12f : 1.f;

	// Fit the imported mesh to the figure height, feet on the ground.
	const FBox Box = SkMesh->GetImportedBounds().GetBox();
	// The axolotl copies share one texture: vary size a little (and tint below).
	const float Scale = VikingHeight * S * (bAxolotl && !bNamed ? FMath::FRandRange(0.94f, 1.06f) : 1.f) / FMath::Max(Box.Max.Z - Box.Min.Z, 1.f);
	HandR = Model->HandR;
	HandL = Model->HandL;
	WeaponScale = Model->WeaponScale;

	Mesh = NewObject<USkeletalMeshComponent>(this);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetSkeletalMeshAsset(SkMesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Mesh->SetupAttachment(Body);
	Mesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -Box.Min.Z * Scale), FRotator(0.f, VikingYaw, 0.f));
	Mesh->SetRelativeScale3D(FVector(Scale));
	Mesh->RegisterComponent();
	AddInstanceComponent(Mesh);

	// Per-slot colours: "Main" is the tunic (role / job colour), raiders get black hair.
	for (int32 I = 0; I < Mesh->GetNumMaterials(); ++I)
	{
		const UMaterialInterface* Src = Mesh->GetMaterial(I);
		const FString SlotName = Src ? Src->GetName() : FString();
		UMaterialInstanceDynamic* MID = Mesh->CreateDynamicMaterialInstance(I);
		FLinearColor Col = DiffuseOrWhite(MID);
		if (bAxolotl && !bNamed) { Col = FLinearColor(FMath::FRandRange(0.85f, 1.f), FMath::FRandRange(0.85f, 1.f), FMath::FRandRange(0.85f, 1.f)); }
		if (SlotName == TEXT("Main")) { TunicSlot = I; Col = Tunic; }
		else if (bRaider && SlotName == TEXT("Hair")) { Col = FLinearColor(0.05f, 0.04f, 0.03f); }
		MeshMIDs.Add(MID);
		MeshCols.Add(Col);
	}

	Anims.SetNum((int32)ETSCAnim::Num);
	for (int32 I = 0; I < (int32)ETSCAnim::Num; ++I)
	{
		const FString Name = FString(Model->AnimPrefix) + VikingAnimNames[I];
		Anims[I] = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), Model->Folder, *Name, *Name));
	}
	const float IdleLen = PlayAnim(ETSCAnim::Idle, true);
	if (IdleLen > 0.f)
	{
		// Desync idle loops so a crowd does not breathe in unison.
		Mesh->SetPosition(FMath::Fmod(AnimPhase, IdleLen), false);
	}

	// Weapons (Content/Weapons). Hunter: bow, healer: staff (always held). Fighters: shield + sword or axe, picked at
	// random; the lord (Torvald) and Ulf the Woodcutter always carry the axe. Shield and blade show only when armed.
	if (Role == ETSCRole::Hunter)
	{
		UStaticMeshComponent* BowComp = AddWeapon(DefBow, false);
		UE_LOG(LogTemp, Display, TEXT("[Bow] %s bow=%s socket(bow_l)=%d handBone=%s"), *DisplayName, BowComp ? *GetNameSafe(BowComp->GetStaticMesh()) : TEXT("NOT CREATED"),
			Mesh->DoesSocketExist(DefBow.Socket) ? 1 : 0, *HandL.ToString());
		// Both arms are posed for the draw after each animation evaluation (the clip only has a one-handed shot).
		Mesh->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &ATSCUnit::ApplyBowPose));
	}
	else if (Role == ETSCRole::Healer)
	{
		AddWeapon(DefStaff, false);
	}
	else
	{
		const bool bAxe = Role == ETSCRole::Shield || Role == ETSCRole::Woodcutter || FMath::RandBool();
		bUsesCombatAxe = bAxe && AddWeapon(DefAxe, true) != nullptr;
		if (!bUsesCombatAxe)
		{
			AddWeapon(DefSword, true);
		}
		AddWeapon(DefShield, true);
	}
	if (Role == ETSCRole::Worker || Role == ETSCRole::Woodcutter)
	{
		// The work axe in hand while chopping, the bundle of logs / vegetables on the way home (see UpdateHeld).
		AddWeapon(DefAxe, false);
		CargoLogsComp = AddCargo(CargoLogs, CargoLogsSize);
		CargoVegComp = AddCargo(CargoVegetables, CargoVegetablesSize);
		UpdateHeld();
	}
	return true;
}

UStaticMeshComponent* ATSCUnit::AddCargo(TArrayView<const TCHAR* const> Paths, float SizeFrac)
{
	UStaticMesh* SM = LoadFirstMesh(Paths);
	if (!SM || !Mesh) { return nullptr; }
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(SM);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetGenerateOverlapEvents(false);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetupAttachment(Body);
	Comp->RegisterComponent();
	AddInstanceComponent(Comp);
	const FBox Box = SM->GetBoundingBox();
	const float Scale = SizeFrac * VikingHeight / FMath::Max(Box.GetSize().GetMax(), (double)KINDA_SMALL_NUMBER);
	Comp->SetRelativeScale3D(FVector(Scale));
	Comp->SetRelativeLocation(CargoHold - Box.GetCenter() * Scale);
	AddTintedMaterials(Comp);
	Comp->SetVisibility(false);
	return Comp;
}

void ATSCUnit::UpdateHeld()
{
	// Only the woodcutters and workers swap their hand item; for the hunter HeldItem is the bow, for the healer the staff: always held.
	if (Role != ETSCRole::Worker && Role != ETSCRole::Woodcutter) { return; }
	const bool bWood = Role == ETSCRole::Woodcutter || (Role == ETSCRole::Worker && Job == ETSCJob::Wood);
	const bool bFood = Role == ETSCRole::Worker && Job == ETSCJob::Food;
	const bool bCarrying = !bArmed && Order == ETSCOrder::Work && WorkPhase == 1;
	// the axe is out while he works the timber (not on the way home, not in a fight: then the real weapon shows)
	if (HeldItem) { HeldItem->SetVisibility(bWood && !bArmed && !bCarrying); }
	if (CargoLogsComp) { CargoLogsComp->SetVisibility(bWood && bCarrying); }
	if (CargoVegComp) { CargoVegComp->SetVisibility(bFood && bCarrying); }
}

bool ATSCUnit::BuildAnimal()
{
	// Entries of this role; with several (the mushrooms) start at one from a shuffle bag (random, but different ones in a
	// row) and take the first whose mesh exists.
	TArray<const FTSCAnimalModel*> Candidates;
	for (const FTSCAnimalModel& M : AnimalModels) { if (M.Role == Role) { Candidates.Add(&M); } }
	if (Candidates.IsEmpty()) { return false; }
	const int32 Start = Candidates.Num() > 1 ? CreatureBag.Next(Candidates.Num()) : 0;
	const FTSCAnimalModel* Model = nullptr;
	FString Folder;
	USkeletalMesh* SkMesh = nullptr;
	for (int32 K = 0; K < Candidates.Num() && !SkMesh; ++K)
	{
		const FTSCAnimalModel* C = Candidates[(Start + K) % Candidates.Num()];
		const FString F = C->Folder ? FString(C->Folder) : FString::Printf(TEXT("/Game/Animals/%s"), C->Name);
		const FString MeshPath = FString::Printf(TEXT("%s/%s.%s"), *F, C->Name, C->Name);
		if (USkeletalMesh* Found = FindObject<USkeletalMesh>(nullptr, *MeshPath)) { SkMesh = Found; }
		else if (FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(MeshPath))) { SkMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath); }
		if (SkMesh) { Model = C; Folder = F; }
	}
	if (!SkMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Saga] Animal mesh not found for role %d (a primitive stand-in is used)"), (int32)Role);
		return false;
	}

	// Fit nose-to-tail length (mushrooms: standing height), feet on the ground.
	const FBox Box = SkMesh->GetImportedBounds().GetBox();
	const FVector Size = Box.GetSize();
	const float Scale = Model->Length / (Model->bFitHeight ? FMath::Max(Size.Z, 1.0) : (float)FMath::Max3(Size.X, Size.Y, 1.0));
	UE_LOG(LogTemp, Display, TEXT("[Saga] Animal %s: imported size %s, scale %.3f"), Model->Name, *Size.ToString(), Scale);
	if (Role == ETSCRole::Creature) { Height = Size.Z * Scale; }

	Mesh = NewObject<USkeletalMeshComponent>(this);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetSkeletalMeshAsset(SkMesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Mesh->SetupAttachment(Body);
	Mesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -Box.Min.Z * Scale), FRotator(0.f, AnimalYaw, 0.f));
	Mesh->SetRelativeScale3D(FVector(Scale));
	Mesh->RegisterComponent();
	AddInstanceComponent(Mesh);
	AddTintedMaterials(Mesh);

	// Animal clips mapped onto the shared slots: gallop = run, eating = the "work" one-shot while grazing.
	const TCHAR* Clips[(int32)ETSCAnim::Num] = {};
	Clips[(int32)ETSCAnim::Idle] = TEXT("Idle");
	Clips[(int32)ETSCAnim::Walk] = TEXT("Walk");
	Clips[(int32)ETSCAnim::Run] = TEXT("Gallop");
	Clips[(int32)ETSCAnim::Slash] = Model->Attack;
	Clips[(int32)ETSCAnim::PickUp] = TEXT("Eating");
	Clips[(int32)ETSCAnim::Hit] = TEXT("Idle_HitReact_Left");
	Clips[(int32)ETSCAnim::Death] = TEXT("Death");
	if (Role == ETSCRole::Husky)
	{
		Clips[(int32)ETSCAnim::Punch] = TEXT("Idle_2_HeadLow"); // head down: being scratched by its master
	}
	Anims.SetNum((int32)ETSCAnim::Num);
	for (int32 I = 0; I < (int32)ETSCAnim::Num; ++I)
	{
		if (!Clips[I]) { continue; }
		const FString Name = FString::Printf(TEXT("%sAnimalArmature_%s"), Model->Name, Clips[I]);
		Anims[I] = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Folder, *Name, *Name));
	}
	const float IdleLen = PlayAnim(ETSCAnim::Idle, true);
	if (IdleLen > 0.f)
	{
		Mesh->SetPosition(FMath::Fmod(AnimPhase, IdleLen), false);
	}
	return true;
}

void ATSCUnit::OrderWander(const FVector& Center, float Radius)
{
	Order = ETSCOrder::Wander;
	HoldPos = FVector(Center.X, Center.Y, 0.f);
	WanderRadius = Radius;
	WanderGoal = Loc2D();
	WanderPause = FMath::FRandRange(0.f, 4.f);
	Leader = nullptr;
}

bool ATSCUnit::ZoneAllows(const FVector& P) const
{
	if (Zone == ETSCZone::Free || !GM) { return true; }
	return GM->IsInsideSettlement(P) == (Zone == ETSCZone::Inside);
}

FVector ATSCUnit::PickWanderGoal() const
{
	for (int32 Try = 0; Try < 12; ++Try)
	{
		const FVector2D R = FMath::RandPointInCircle(WanderRadius);
		const FVector G = HoldPos + FVector(R.X, R.Y, 0.f);
		if (ZoneAllows(G)) { return G; }
	}
	return Loc2D();
}

void ATSCUnit::OrderFollow(ATSCUnit* InLeader)
{
	Order = ETSCOrder::Follow;
	Leader = InLeader;
	PlayTimer = 0.f;
	PetTimer = 0.f;
	PlayCooldown = FMath::FRandRange(10.f, 25.f); // the first romp comes a little after the landing
}

bool ATSCUnit::BeginPet(const FVector& DogPos, float Duration)
{
	if (Order != ETSCOrder::Patrol || !IsTargetable() || bDowned || Target.IsValid() || !Mesh) { return false; }
	const FVector To = DogPos - Loc2D();
	if (To.Size2D() > 320.f) { return false; }
	SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
	PatrolPause = Duration; // the patrol waits while he crouches to the dog
	OneShotTimer = 0.f;
	const float Len = PlayAnim(ETSCAnim::PickUp, false);
	OneShotTimer = FMath::Min(Len, Duration);
	return Len > 0.f;
}

bool ATSCUnit::SimWildlife(float Dt)
{
	bGallop = false;
	if (Role == ETSCRole::Husky) { Speed = BaseSpeed; } // undo the charge speed from a fight
	auto StartDogPlay = [this]()
	{
		PlayTimer = FMath::FRandRange(6.f, 9.f);
		PlayAngle = FMath::FRandRange(0.f, 2.f * PI);
		if (DogSoundCooldown <= 0.f)
		{
			GM->PlayDogSound(*this, ETSCDogSound::PlayBark);
			DogSoundCooldown = 2.1f;
		}
	};
	if (Order == ETSCOrder::Follow)
	{
		ATSCUnit* L = Leader.Get();
		if (!L || !L->IsAlive())
		{
			OrderWander(Loc2D(), 300.f); // master gone: stay and roam here
			return false;
		}
		if (L->IsHidden()) { return false; } // master indoors / aboard: wait
		if (Role == ETSCRole::Husky)
		{
			PlayCooldown = FMath::Max(0.f, PlayCooldown - Dt);
			// The master is free to play: walking his round, not fighting, not sheltered or lying down.
			const bool bMasterFree = L->Order == ETSCOrder::Patrol && L->IsTargetable() && !L->bDowned && !L->Target.IsValid();
			if (PetTimer > 0.f)
			{
				// Hakon is scratching it: stand in front of him, head low.
				PetTimer -= Dt;
				if (StepToward(PetSpot, Dt, 20.f))
				{
					const FVector To = L->Loc2D() - Loc2D();
					SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
					if (OneShotTimer <= 0.f && PetTimer > 0.5f)
					{
						OneShotTimer = PetTimer;
						PlayAnim(ETSCAnim::Punch, true);
					}
					return false;
				}
				return true;
			}
			if (PlayTimer > 0.f)
			{
				if (!bMasterFree)
				{
					PlayTimer = 0.f;
					PlayCooldown = FMath::FRandRange(20.f, 40.f);
				}
				else
				{
					PlayTimer -= Dt;
					if (PlayTimer > 0.f)
					{
						// Romp: gallop circles round the master (he keeps walking his round).
						PlayAngle += Dt * 1.8f;
						const FVector Circle = L->Loc2D() + FVector(FMath::Cos(PlayAngle), FMath::Sin(PlayAngle), 0.f) * 150.f;
						const float D = FVector::Dist2D(GetActorLocation(), Circle);
						bGallop = true;
						Speed = D > 250.f ? 340.f : 260.f;
						StepToward(Circle, Dt, 15.f);
						return true;
					}
					// Played enough: it runs up and Hakon strokes it.
					PlayCooldown = FMath::FRandRange(40.f, 75.f);
					if (L->BeginPet(Loc2D(), PetDuration))
					{
						PetTimer = PetDuration;
						PetSpot = L->Loc2D() + (Loc2D() - L->Loc2D()).GetSafeNormal2D() * 90.f;
						OneShotTimer = 0.f;
					}
					return false;
				}
			}
			else if (PlayCooldown <= 0.f && bMasterFree && FVector::Dist2D(GetActorLocation(), L->GetActorLocation()) < 450.f)
			{
				StartDogPlay();
			}
		}
		// Trot a little behind and to the side of the master; gallop to catch up.
		const FVector Fwd = L->GetActorForwardVector().GetSafeNormal2D();
		const FVector Spot = L->Loc2D() - Fwd * 110.f + FVector(-Fwd.Y, Fwd.X, 0.f) * 60.f;
		const float D = FVector::Dist2D(GetActorLocation(), Spot);
		if (D < 70.f) { return false; }
		bGallop = D > 320.f;
		Speed = bGallop ? 320.f : 180.f;
		return !StepToward(Spot, Dt, 50.f);
	}

	// The village dog (a husky on a Wander order): when the jarl is in sight inside the settlement it runs to him,
	// circles him playfully for a few seconds, then trots off and leaves him alone for a good while.
	if (Role == ETSCRole::Husky && Order == ETSCOrder::Wander)
	{
		PlayCooldown = FMath::Max(0.f, PlayCooldown - Dt);
		const ATSCUnit* Jarl = GM->GetJarl();
		const bool bJarlHere = Jarl && Jarl->IsTargetable() && !Jarl->IsHidden() && !Jarl->bDowned && ZoneAllows(Jarl->GetActorLocation());
		if (PlayTimer <= 0.f && PlayCooldown <= 0.f && bJarlHere && FVector::Dist2D(GetActorLocation(), Jarl->GetActorLocation()) < JarlNoticeRadius)
		{
			StartDogPlay();
		}
		if (PlayTimer > 0.f)
		{
			PlayTimer = bJarlHere ? PlayTimer - Dt : 0.f;
			if (PlayTimer > 0.f)
			{
				PlayAngle += Dt * 1.8f;
				const FVector Spot = Jarl->Loc2D() + FVector(FMath::Cos(PlayAngle), FMath::Sin(PlayAngle), 0.f) * 140.f;
				const float D = FVector::Dist2D(GetActorLocation(), Spot);
				bGallop = true;
				Speed = D > 250.f ? 340.f : 260.f;
				StepToward(Spot, Dt, 15.f);
				return true;
			}
			// Played enough: wander off and keep away for a while.
			PlayCooldown = FMath::FRandRange(40.f, 70.f);
			WanderGoal = PickWanderGoal();
			WanderPause = 0.f;
		}
	}

	// Forest animals (all of them, wolves too) keep away from people: anyone within HumanFleeRadius sends them galloping off.
	// Never the dogs or the livestock. The wall still stops them (see StepToward), they run along it instead.
	if (Role == ETSCRole::Deer || Role == ETSCRole::Stag || Role == ETSCRole::Fox || Role == ETSCRole::Wolf)
	{
		// Failsafe: a forest animal that finds itself inside the village goes back to its range outside.
		if (Zone != ETSCZone::Free && !ZoneAllows(GetActorLocation()))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Saga] Forest animal (role %d) found inside the village at %s: sent back to %s"),
				(int32)Role, *GetActorLocation().ToString(), *HoldPos.ToString());
			Teleport(HoldPos);
			Path.Reset();
		}
		const FVector Me = Loc2D();
		float Nearest = HumanFleeRadius;
		for (const ATSCUnit* H : GM->Units)
		{
			if (!H || H->IsWildlife() || H->Team == ETSCTeam::Creature || !H->IsTargetable() || H->IsHidden()) { continue; }
			const float D = FVector::Dist2D(Me, H->GetActorLocation());
			if (D < Nearest)
			{
				Nearest = D;
				FleeFrom = H->Loc2D();
				FleeTimer = FMath::Max(FleeTimer, FleeLinger);
			}
		}
	}

	// Prey: a wolf within range sends deer, the stag and foxes galloping away; they keep running a moment after.
	if (Role == ETSCRole::Deer || Role == ETSCRole::Stag || Role == ETSCRole::Fox)
	{
		const FVector Me = Loc2D();
		float Nearest = FleeRadius;
		for (const ATSCUnit* W : GM->GetWildlife())
		{
			// predators: wolves and dogs (Hakon's husky and the village one)
			if (!W || (W->Role != ETSCRole::Wolf && W->Role != ETSCRole::Husky) || !W->IsAlive() || W->IsHidden()) { continue; }
			const float D = FVector::Dist2D(Me, W->GetActorLocation());
			if (D < Nearest)
			{
				Nearest = D;
				FleeFrom = W->Loc2D();
				FleeTimer = FleeLinger;
			}
		}
		if (FleeTimer > 0.f)
		{
			FleeTimer -= Dt;
			FVector Away = (Me - FleeFrom).GetSafeNormal2D();
			if (Away.IsNearlyZero()) { Away = GetActorForwardVector().GetSafeNormal2D(); }
			// If the wall is in the way, run along it: turn until the way ahead is open to this animal.
			for (const float Turn : { 0.f, 40.f, -40.f, 80.f, -80.f, 120.f, -120.f })
			{
				const FVector Dir = Away.RotateAngleAxis(Turn, FVector::UpVector);
				if (ZoneAllows(Me + Dir * 150.f)) { Away = Dir; break; }
			}
			bGallop = true;
			Speed = BaseSpeed * FleeSpeedMul;
			StepToward(Me + Away * 400.f, Dt, 10.f);
			if (FleeTimer <= 0.f)
			{
				// Safe: calm down and graze where it ended up before heading home again.
				Speed = BaseSpeed;
				WanderGoal = Loc2D();
				WanderPause = FMath::FRandRange(1.f, 3.f);
			}
			return true;
		}
	}

	// Wander: graze or rest a while, then walk to another spot near home.
	if (WanderPause > 0.f)
	{
		WanderPause -= Dt;
		return false;
	}
	const bool bArrived = StepToward(WanderGoal, Dt, 30.f);
	if (bZoneBlocked)
	{
		// The way leads across the palisade (e.g. through the open gate): give up on this goal.
		WanderGoal = PickWanderGoal();
		WanderPause = FMath::FRandRange(0.5f, 2.f);
		return false;
	}
	if (bArrived)
	{
		WanderPause = FMath::FRandRange(3.f, 9.f);
		if (Mesh && FMath::FRand() < 0.6f)
		{
			OneShotTimer = FMath::Min(PlayAnim(ETSCAnim::PickUp, false), WanderPause);
		}
		WanderGoal = PickWanderGoal();
		return false;
	}
	return true;
}

UStaticMeshComponent* ATSCUnit::AddWeapon(const FTSCWeaponDef& Def, bool bArmedOnly)
{
	UStaticMesh* SM = LoadFirstMesh(Def.Paths);
	if (!SM || !Mesh) { return nullptr; }
	// Fallback bone: the model's own hand bone (Def names the Viking Fist_R / Fist_L).
	const FName Hand = Def.FallbackBone == FName(TEXT("Fist_L")) ? HandL : HandR;
	const FName Socket = Mesh->DoesSocketExist(Def.Socket) ? Def.Socket : (Hand.IsNone() ? Def.FallbackBone : Hand);

	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(SM);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetGenerateOverlapEvents(false);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetupAttachment(Mesh, Socket);
	Comp->RegisterComponent();
	AddInstanceComponent(Comp);

	// Orient by the socket convention, size it against the figure, and put the grip point on the socket.
	// The skeleton bones carry a x100 scale, so sizes are set in world space, offsets divided by the socket scale.
	const float MeshLen = FMath::Max(SM->GetBoundingBox().GetSize().GetMax(), KINDA_SMALL_NUMBER);
	const float WorldScale = Def.Size * VikingHeight * (bNamed ? 1.12f : 1.f) * WeaponScale / MeshLen;
	const float SocketScale = FMath::Max(Mesh->GetSocketTransform(Socket, RTS_World).GetScale3D().GetMax(), KINDA_SMALL_NUMBER);
	const FQuat Rot = AxesToXZ(Def.LocalX, Def.LocalZ);
	Comp->SetRelativeRotation(Rot);
	Comp->SetWorldScale3D(FVector(WorldScale));
	Comp->SetRelativeLocation(-Rot.RotateVector(Def.Grip) * WorldScale / SocketScale);
	AddTintedMaterials(Comp);
	if (bArmedOnly)
	{
		// Visible for now; BuildFigure's SetArmed() hides it for unarmed units.
		ArmedItems.Add(Comp);
	}
	else
	{
		HeldItem = Comp;
	}
	return Comp;
}

void ATSCUnit::AddTintedMaterials(UMeshComponent* Comp)
{
	// Each slot gets a MID so Refresh can blend its colour to the Ashen look.
	for (int32 I = 0; I < Comp->GetNumMaterials(); ++I)
	{
		UMaterialInstanceDynamic* MID = Comp->CreateDynamicMaterialInstance(I);
		MeshMIDs.Add(MID);
		MeshCols.Add(DiffuseOrWhite(MID));
	}
}

int32 ATSCUnit::AcquireArrow()
{
	if (!Mesh) { return INDEX_NONE; }
	UStaticMesh* SM = LoadFirstMesh(WeaponArrow);
	if (!SM) { return INDEX_NONE; }

	// Reuse a finished arrow, otherwise make a new one (world-space, not attached to the moving hunter).
	int32 Idx = Arrows.IndexOfByPredicate([](const FTSCArrowFlight& A) { return A.T >= 1.f && !A.bNocked; });
	if (Idx == INDEX_NONE)
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(SM);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetupAttachment(Root);
		Comp->SetUsingAbsoluteLocation(true);
		Comp->SetUsingAbsoluteRotation(true);
		Comp->SetUsingAbsoluteScale(true);
		Comp->RegisterComponent();
		AddInstanceComponent(Comp);
		AddTintedMaterials(Comp);
		Refresh();
		ArrowComps.Add(Comp);
		Idx = Arrows.AddDefaulted();
	}
	UStaticMeshComponent* Comp = ArrowComps[Idx];
	Comp->SetWorldScale3D(FVector(ArrowSize * VikingHeight / FMath::Max(SM->GetBoundingBox().GetSize().GetMax(), KINDA_SMALL_NUMBER)));
	return Idx;
}

void ATSCUnit::LoosenArrow(int32 Idx, const FVector& To)
{
	FTSCArrowFlight& A = Arrows[Idx];
	A.From = ArrowComps[Idx]->GetComponentLocation();
	A.To = To;
	A.T = 0.f;
	A.bNocked = false;
	// Slow enough to follow with the eye: a quarter to six tenths of a second.
	A.Dur = FMath::Clamp(FVector::Dist(A.From, To) / 1800.f, 0.25f, 0.6f);
	ArrowComps[Idx]->SetVisibility(true);
	UpdateArrows(0.f);
}

bool ATSCUnit::FireArrow(const FVector& To)
{
	const int32 Idx = AcquireArrow();
	if (Idx == INDEX_NONE) { return false; }
	ArrowComps[Idx]->SetWorldLocation(HeldItem ? HeldItem->Bounds.Origin : GetActorLocation() + FVector(0.f, 0.f, 90.f));
	LoosenArrow(Idx, To);
	return true;
}

void ATSCUnit::BeginDraw(ATSCUnit* At)
{
	CancelDraw();
	if (!At) { return; }
	DrawTarget = At;
	DrawAim = At->GetActorLocation() + FVector(0.f, 0.f, At->Height * 0.6f);
	DrawTotal = DrawTimer = BowDrawTime;
	const int32 Idx = AcquireArrow();
	if (Idx == INDEX_NONE) { return; } // no arrow mesh: the shot is still loosed at the end of the draw, as a tracer line
	NockedIdx = Idx;
	Arrows[Idx].bNocked = true;
	Arrows[Idx].T = 0.f;
	ArrowComps[Idx]->SetVisibility(true);
}

void ATSCUnit::CancelDraw()
{
	if (NockedIdx != INDEX_NONE && Arrows.IsValidIndex(NockedIdx))
	{
		Arrows[NockedIdx].bNocked = false;
		Arrows[NockedIdx].T = 1.f;
		if (ArrowComps[NockedIdx]) { ArrowComps[NockedIdx]->SetVisibility(false); }
	}
	NockedIdx = INDEX_NONE;
	DrawTimer = 0.f;
	DrawTarget.Reset();
}

void ATSCUnit::ApplyBowPose()
{
	if (!Mesh || bDead || bDowned) { return; }
	const USkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
	if (!Asset) { return; }
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	// The pose just evaluated is the read buffer now; edit it in place (component space of the mesh).
	TArray<FTransform>& CS = const_cast<TArray<FTransform>&>(Mesh->GetComponentSpaceTransforms());
	if (CS.Num() != Ref.GetNum()) { return; }

	const int32 HandLIdx = Ref.FindBoneIndex(HandL);
	const int32 HandRIdx = Ref.FindBoneIndex(HandR);
	if (HandLIdx == INDEX_NONE || HandRIdx == INDEX_NONE) { return; }
	const int32 ForeL = Ref.GetParentIndex(HandLIdx), ForeR = Ref.GetParentIndex(HandRIdx);
	if (ForeL == INDEX_NONE || ForeR == INDEX_NONE) { return; }
	const int32 UpL = Ref.GetParentIndex(ForeL), UpR = Ref.GetParentIndex(ForeR);
	if (UpL == INDEX_NONE || UpR == INDEX_NONE) { return; }

	const FTransform CT = Mesh->GetComponentTransform();
	auto ToComp = [&CT](const FVector& W) { return CT.InverseTransformPosition(W); };
	auto ToCompDir = [&CT](const FVector& W) { return CT.InverseTransformVectorNoScale(W); };

	// Rotate bone Root and everything below it about Pivot.
	auto RotateSub = [&](int32 BoneRoot, const FQuat& Q, const FVector& Pivot)
	{
		for (int32 I = BoneRoot; I < CS.Num(); ++I)
		{
			bool bIn = I == BoneRoot;
			for (int32 P = Ref.GetParentIndex(I); !bIn && P != INDEX_NONE; P = Ref.GetParentIndex(P)) { bIn = P == BoneRoot; }
			if (!bIn) { continue; }
			FTransform& T = CS[I];
			T.SetLocation(Pivot + Q.RotateVector(T.GetLocation() - Pivot));
			T.SetRotation((Q * T.GetRotation()).GetNormalized());
		}
	};
	// Two-bone solve: the hand goes to Target (clamped to reach), the elbow bends towards Pole.
	auto SolveArm = [&](int32 Up, int32 Fore, int32 Hand, const FVector& Goal, const FVector& Pole)
	{
		const FVector A = CS[Up].GetLocation(), B0 = CS[Fore].GetLocation(), C0 = CS[Hand].GetLocation();
		const float La = FVector::Dist(A, B0), Lb = FVector::Dist(B0, C0);
		const FVector ToT = Goal - A;
		const float D = FMath::Clamp((float)ToT.Size(), FMath::Abs(La - Lb) + 0.1f, La + Lb - 0.1f);
		const FVector U = ToT.GetSafeNormal();
		const float X = (La * La - Lb * Lb + D * D) / (2.f * D);
		const float H = FMath::Sqrt(FMath::Max(La * La - X * X, 0.f));
		const FVector Pd = (Pole - U * FVector::DotProduct(Pole, U)).GetSafeNormal();
		const FVector B1 = A + U * X + Pd * H;
		const FVector C1 = A + U * D;
		RotateSub(Up, FQuat::FindBetweenNormals((B0 - A).GetSafeNormal(), (B1 - A).GetSafeNormal()), A);
		const FVector B = CS[Fore].GetLocation(), C = CS[Hand].GetLocation();
		RotateSub(Fore, FQuat::FindBetweenNormals((C - B).GetSafeNormal(), (C1 - B).GetSafeNormal()), B);
	};

	const FVector ShoulderL = CT.TransformPosition(CS[UpL].GetLocation());
	FVector Dir = (DrawAim - ShoulderL).GetSafeNormal();
	if (Dir.IsNearlyZero()) { Dir = GetActorForwardVector(); }
	const FVector Right = GetActorRightVector();
	const float ReachL = FVector::Dist(CS[UpL].GetLocation(), CS[ForeL].GetLocation()) + FVector::Dist(CS[ForeL].GetLocation(), CS[HandLIdx].GetLocation());
	const float ReachLW = ReachL * CT.GetScale3D().X;
	// Bow hand straight out at the target; string hand drawn back along the arrow towards the cheek.
	const FVector BowHandW = ShoulderL + Dir * ReachLW * 0.97f;
	const FVector StringHandW = BowHandW - Dir * FMath::Lerp(ReachLW * 0.25f, ArrowSize * VikingHeight * 0.8f, DrawPull) + FVector(0.f, 0.f, 2.f);
	if (BowPoseW > 0.01f)
	{
		const FVector OldBowHand = CT.TransformPosition(CS[HandLIdx].GetLocation());
		const FVector OldStringHand = CT.TransformPosition(CS[HandRIdx].GetLocation());
		SolveArm(UpL, ForeL, HandLIdx, ToComp(FMath::Lerp(OldBowHand, BowHandW, BowPoseW)), ToCompDir(-Right * 0.4f + FVector(0.f, 0.f, -0.6f)));
		SolveArm(UpR, ForeR, HandRIdx, ToComp(FMath::Lerp(OldStringHand, StringHandW, BowPoseW)), ToCompDir(Right + FVector(0.f, 0.f, 0.25f) - Dir * 0.2f));
	}

	// The bow is always held upright, whatever the clip does to the wrist (and whatever the arm pose above did): turn the bow
	// hand so the limbs (the mesh's Y) stand vertical. While drawing, the turn is a roll about the aim line, so the bow keeps facing the target.
	if (HeldItem)
	{
		Mesh->UpdateChildTransforms(); // the bow's transform must follow the pose as it is now
		const FVector Limb = HeldItem->GetComponentTransform().TransformVectorNoScale(FVector(0.f, 1.f, 0.f));
		FVector LimbP = Limb, UpP = FVector::UpVector;
		if (BowPoseW > 0.01f)
		{
			LimbP = (Limb - Dir * FVector::DotProduct(Limb, Dir)).GetSafeNormal();
			UpP = (FVector::UpVector - Dir * FVector::DotProduct(FVector::UpVector, Dir)).GetSafeNormal();
		}
		if (!LimbP.IsNearlyZero() && !UpP.IsNearlyZero())
		{
			if (FVector::DotProduct(LimbP, UpP) < 0.f) { UpP = -UpP; } // the limbs are symmetric: take the nearer way round
			const FQuat Roll = FQuat::FindBetweenNormals(LimbP, UpP);
			const FQuat Rot = CT.GetRotation();
			RotateSub(HandLIdx, Rot.Inverse() * Roll * Rot, CS[HandLIdx].GetLocation()); // world rotation, expressed in mesh space
		}
	}
	Mesh->UpdateChildTransforms();
	Mesh->MarkRenderDynamicDataDirty();
}

void ATSCUnit::UpdateArrows(float Dt)
{
	// Arm pose weight: up while drawing, quickly back to the clip afterwards.
	BowPoseW = FMath::FInterpConstantTo(BowPoseW, DrawTimer > 0.f ? 1.f : 0.f, Dt, 7.f);
	DrawPull = DrawTimer > 0.f ? FMath::SmoothStep(0.f, 1.f, 1.f - DrawTimer / FMath::Max(DrawTotal, KINDA_SMALL_NUMBER)) : 0.f;

	// The bow being drawn: the nocked arrow follows the bow, its tail sliding back along the aim line; loosed when the draw ends.
	if (DrawTimer > 0.f)
	{
		ATSCUnit* Tgt = DrawTarget.Get();
		if (bDead || bDowned || bGone || bSheltered || !GM)
		{
			CancelDraw();
		}
		else
		{
			if (Tgt && Tgt->IsTargetable()) { DrawAim = Tgt->GetActorLocation() + FVector(0.f, 0.f, Tgt->Height * 0.6f); }
			DrawTimer = FMath::Max(0.f, DrawTimer - Dt);
			const FVector BowPos = HeldItem ? HeldItem->Bounds.Origin : GetActorLocation() + FVector(0.f, 0.f, 90.f);
			const FVector AimDir = (DrawAim - BowPos).GetSafeNormal();
			if (NockedIdx != INDEX_NONE && ArrowComps[NockedIdx] && !AimDir.IsNearlyZero())
			{
				const float ArrowLen = ArrowSize * VikingHeight;
				FVector P = BowPos + AimDir * (ArrowLen * 0.2f - BowPullDist * DrawPull);
				// Once the arms are in the draw pose the arrow's tail sits in the string hand.
				if (BowPoseW > 0.05f && Mesh && !HandR.IsNone())
				{
					const FVector Tail = Mesh->GetSocketLocation(HandR);
					P = FMath::Lerp(P, Tail + AimDir * (ArrowLen * 0.5f), BowPoseW);
				}
				ArrowComps[NockedIdx]->SetWorldLocationAndRotation(P, AimDir.Rotation().Quaternion() * ArrowMeshRot);
			}
			if (DrawTimer <= 0.f)
			{
				// Loose: the hit lands now, the arrow flies the rest of the way.
				if (Tgt && Tgt->IsTargetable()) { Tgt->Hurt(Damage); }
				if (NockedIdx != INDEX_NONE)
				{
					const int32 Idx = NockedIdx;
					NockedIdx = INDEX_NONE;
					LoosenArrow(Idx, DrawAim);
				}
				else
				{
					GM->AddTracer(BowPos, DrawAim, FLinearColor(1.f, 0.9f, 0.6f));
				}
				DrawTarget.Reset();
			}
		}
	}

	for (int32 I = 0; I < Arrows.Num(); ++I)
	{
		FTSCArrowFlight& A = Arrows[I];
		UStaticMeshComponent* Comp = ArrowComps[I];
		if (A.T >= 1.f || A.bNocked || !Comp) { continue; }
		A.T = FMath::Min(1.f, A.T + Dt / A.Dur);
		if (A.T >= 1.f)
		{
			Comp->SetVisibility(false);
			continue;
		}
		// Shallow arc; the arrow points along its flight direction.
		const float Arc = FVector::Dist(A.From, A.To) * 0.06f;
		const FVector P = FMath::Lerp(A.From, A.To, A.T) + FVector(0.f, 0.f, Arc * 4.f * A.T * (1.f - A.T));
		const FVector Dir = (A.To - A.From) + FVector(0.f, 0.f, Arc * 4.f * (1.f - 2.f * A.T));
		Comp->SetWorldLocationAndRotation(P, Dir.Rotation().Quaternion() * ArrowMeshRot);
	}
}

float ATSCUnit::PlayAnim(ETSCAnim Id, bool bLoop, float Rate)
{
	UAnimSequence* A = Anims.IsValidIndex((int32)Id) ? Anims[(int32)Id].Get() : nullptr;
	if (!Mesh || !A) { return 0.f; }
	if (A != CurAnim || !bLoop)
	{
		CurAnim = A;
		Mesh->PlayAnimation(A, bLoop);
	}
	Mesh->SetPlayRate(Rate);
	return A->GetPlayLength();
}

void ATSCUnit::StartSwing(float ArmTime, float Window, ETSCAnim Clip)
{
	SwingTimer = ArmTime;
	if (!Mesh) { return; }
	const UAnimSequence* A = Anims.IsValidIndex((int32)Clip) ? Anims[(int32)Clip].Get() : nullptr;
	if (!A) { return; }
	const float Len = A->GetPlayLength();
	const float Rate = Window > 0.f ? FMath::Max(1.f, Len / Window) : 1.f;
	PlayAnim(Clip, false, Rate);
	OneShotTimer = Len / Rate;
}

void ATSCUnit::Refresh()
{
	Super::Refresh();
	const float A = FMath::SmoothStep(0.f, 1.f, SagaAlpha);
	for (int32 I = 0; I < MeshMIDs.Num(); ++I)
	{
		if (MeshMIDs[I])
		{
			MeshMIDs[I]->SetVectorParameterValue(DiffuseParam, FMath::Lerp(MeshCols[I], Ashify(MeshCols[I]), A));
		}
	}
}

void ATSCUnit::SetArmed(bool bInArmed)
{
	if (bArmed == bInArmed) { return; }
	bArmed = bInArmed;
	for (int32 I : ArmedParts) { SetShown(I, bArmed); }
	for (UStaticMeshComponent* C : ArmedItems) { if (C) { C->SetVisibility(bArmed); } }
	for (int32 I : UnarmedParts) { SetShown(I, !bArmed); }
	Refresh();
}

void ATSCUnit::SetJob(ETSCJob InJob)
{
	if (Job != InJob) { CancelTimber(); }
	Job = InJob;
	if (Role == ETSCRole::Worker && TunicPart >= 0)
	{
		const FLinearColor C = Job == ETSCJob::Wood ? FLinearColor(0.36f, 0.17f, 0.07f)
			: Job == ETSCJob::Food ? FLinearColor(0.17f, 0.30f, 0.09f) : FLinearColor(0.20f, 0.22f, 0.27f);
		Parts[TunicPart].LCol = C;
		Parts[TunicPart].ACol = C;
		Refresh();
	}
	if (Role == ETSCRole::Worker && MeshCols.IsValidIndex(TunicSlot))
	{
		MeshCols[TunicSlot] = Job == ETSCJob::Wood ? FLinearColor(0.36f, 0.17f, 0.07f)
			: Job == ETSCJob::Food ? FLinearColor(0.17f, 0.30f, 0.09f) : FLinearColor(0.20f, 0.22f, 0.27f);
		Refresh();
	}
}

void ATSCUnit::OrderWork(const FVector& A, const FVector& B)
{
	// UpdateJobs repeats the same order when an unrelated job/build changes. Keep the current tree/cargo.
	if (Order == ETSCOrder::Work && WorkA.Equals(A) && WorkB.Equals(B)) { return; }
	CancelTimber();
	Order = ETSCOrder::Work;
	WorkA = A;
	WorkB = B;
	WorkPhase = 0;
	WorkTimer = 0.f;
}

void ATSCUnit::OrderHold(const FVector& Pos)
{
	CancelTimber();
	Order = ETSCOrder::Hold;
	HoldPos = Pos;
	if (bSheltered)
	{
		bSheltered = false;
		SetActorHiddenInGame(false);
	}
}

void ATSCUnit::OrderShelter(const FVector& Pos)
{
	CancelTimber();
	Order = ETSCOrder::Shelter;
	HoldPos = Pos;
}

void ATSCUnit::OrderLeave(const FVector& Pos)
{
	CancelTimber();
	Order = ETSCOrder::Leave;
	HoldPos = Pos;
	Target = nullptr;
}

void ATSCUnit::OrderAssault()
{
	CancelTimber();
	Order = ETSCOrder::Assault;
}

void ATSCUnit::OrderPatrol(const TArray<FVector>& Route)
{
	if (Route.Num() < 2) { return; }
	CancelTimber();
	Order = ETSCOrder::Patrol;
	PatrolRoute = Route;
	// join the loop at the waypoint nearest to where the hero stands
	float Best = TNumericLimits<float>::Max();
	PatrolIdx = 0;
	for (int32 I = 0; I < Route.Num(); ++I)
	{
		const float D = FVector::DistSquared2D(GetActorLocation(), Route[I]);
		if (D < Best) { Best = D; PatrolIdx = I; }
	}
	PatrolPause = 0.f;
	PatrolLegTime = 0.f;
	Target = nullptr;
	if (bSheltered)
	{
		bSheltered = false;
		SetActorHiddenInGame(false);
	}
}

void ATSCUnit::Teleport(const FVector& Pos)
{
	CancelTimber();
	SetActorLocation(FVector(Pos.X, Pos.Y, GM ? GM->GroundReliefAt(Pos) - GM->WadeDepthAt(FVector(Pos.X, Pos.Y, 0.f)) : 0.f));
	LastWadePos = GetActorLocation();
	Path.Reset();
	bNavWasBlocked = true; StuckCount = 0; CrowdWait = 0.f; PathVersion = -1;
}

void ATSCUnit::Idle(float Dt)
{
	UpdateWading(Dt);
	Animate(Dt, false);
}

void ATSCUnit::UpdateWading(float Dt)
{
	if (!GM || bGone || bSheltered || IsHidden()) { return; }
	const FVector Loc = GetActorLocation();
	const float Depth = GM->WadeDepthAt(Loc);
	const float NewZ = FMath::FInterpTo(Loc.Z, GM->GroundReliefAt(Loc) - Depth, Dt, 8.f);
	if (!FMath::IsNearlyEqual(NewZ, Loc.Z, 0.01f))
	{
		SetActorLocation(FVector(Loc.X, Loc.Y, NewZ));
	}
	if (Depth > 6.f && !bDead && !bDowned)
	{
		const float Moved = FVector::Dist2D(Loc, LastWadePos);
		if (Moved > 20.f * Dt)
		{
			WadeStepTimer -= Dt;
			if (WadeStepTimer <= 0.f)
			{
				GM->WadeStep(Loc2D(), (Loc - LastWadePos).GetSafeNormal2D());
				WadeStepTimer = 0.3f;
			}
		}
	}
	LastWadePos = Loc;
}

bool ATSCUnit::AtHoldPos() const
{
	return FVector::Dist2D(GetActorLocation(), HoldPos) < 60.f;
}

void ATSCUnit::Hurt(float Amount)
{
	if (!IsAlive() || bInvulnerable) { return; }
	HP -= Amount;
	BarTimer = 6.f;
	if (Role == ETSCRole::Worker) { SelfDefenseHold = FMath::Max(SelfDefenseHold, 4.f); SelfDefenseThink = 0.f; } // hit: reach for the weapon
	if (HP <= 0.f)
	{
		Kill();
	}
	else if (Mesh && OneShotTimer <= 0.f && !bDowned)
	{
		OneShotTimer = PlayAnim(ETSCAnim::Hit, false);
	}
}

void ATSCUnit::Kill()
{
	if (bDead) { return; }
	CancelTimber();
	HP = 0.f;
	bDead = true;
	DeathTimer = 0.f;
	Need = ETSCNeed::None;
	if (Mesh)
	{
		OneShotTimer = 0.f;
		PlayAnim(ETSCAnim::Death, false);
	}
	if (GM) { GM->OnUnitDied(this); }
}

bool ATSCUnit::HasShot(const ATSCUnit* T) const
{
	const FVector A = Loc2D(), B = T->Loc2D();
	const float Len = FVector::Dist2D(A, B);
	// The nav mask is already widened by the clearance round every obstacle (walls, fences, props), so the ends are skipped:
	// an archer standing by a prop, or a target pressed against a wall, is not blocked by what it stands next to.
	// One thin line, not a fat corridor: only something really between the two stops the arrow.
	constexpr float TrimStart = 110.f, TrimEnd = 70.f, Step = 25.f;
	if (Len <= TrimStart + TrimEnd) { return true; }
	const FVector Dir = (B - A) / Len;
	// Hostile layer: it also holds the barred gate, which is a wall for arrows.
	for (float S = TrimStart; S <= Len - TrimEnd; S += Step)
	{
		if (GM->GetNav().IsBlockedAt(A + Dir * S, TSCNavLayer::Hostile)) { return false; }
	}
	return true;
}

ATSCUnit* ATSCUnit::FindTarget() const
{
	if (ATSCUnit* Q = Quarry.Get()) { return Q->IsTargetable() ? Q : nullptr; }
	ATSCUnit* Best = nullptr;
	float BestD = AggroRange, BestRank = MAX_flt;
	const FVector Me = GetActorLocation();
	const bool bRanged = AttackRange > 300.f;
	// The husky guards its master: it also fights whatever comes within GuardRadius of him.
	const ATSCUnit* Master = Role == ETSCRole::Husky ? Leader.Get() : nullptr;
	for (ATSCUnit* U : GM->Units)
	{
		if (!U || U == this || !U->IsTargetable() || U->Team == ETSCTeam::Animal) { continue; }
		const bool bEnemy = Role == ETSCRole::Husky ? (U->Team == ETSCTeam::Creature || U->Team == ETSCTeam::Raider)
			: (Team == ETSCTeam::Villager) != (U->Team == ETSCTeam::Villager);
		if (!bEnemy) { continue; }
		if (U->Order == ETSCOrder::Leave) { continue; } // nobody chases an enemy that is already running off
		const float D = FVector::Dist2D(Me, U->GetActorLocation());
		const bool bThreatensMaster = Master && Master->IsAlive() && FVector::Dist2D(Master->GetActorLocation(), U->GetActorLocation()) < GuardRadius;
		// On a leash: a pet with a living master only goes for enemies close to him. Once they run past GuardRadius it lets them go
		// (no target -> Follow order) and gallops back to its master.
		if (Master && Master->IsAlive() && !bThreatensMaster) { continue; }
		if ((D < BestD || bThreatensMaster) && (bRanged || GM->CanReach(this, U)))
		{
			// among candidates pick the closest to the dog; an archer first takes whoever he can actually hit (no fence in the way)
			const float Rank = D + (bRanged && D <= AttackRange && !HasShot(U) ? 5000.f : 0.f);
			if (Best && Rank >= BestRank) { continue; }
			BestRank = Rank;
			if (!bRanged) { BestD = FMath::Min(BestD, D); } // an archer ranks by Rank: a blocked near target must not shut out a clear far one
			Best = U;
		}
	}
	return Best;
}

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

bool ATSCUnit::StepToward(const FVector& Dest, float Dt, float StopDist)
{
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
	if (FVector::Dist2D(CrowdDest, Dest) > 60.f) { CrowdWait = 0.f; } // the wait belongs to one destination
	if (CrowdWait > 0.f) { CrowdWait -= Dt; return true; }
	StuckClock += Dt;
	if (StuckClock >= 0.6f)
	{
		const bool bStuck = FVector::Dist2D(Me, StuckRef) < 0.25f * Speed * StuckClock;
		StuckCount = bStuck ? StuckCount + 1 : 0;
		StuckRef = Me; StuckClock = 0.f;
		if (bStuck && (DestDist <= StopDist + 120.f || FVector::Dist2D(Me, Path.Last()) <= 120.f))
		{
			CrowdWait = 1.5f; CrowdDest = Dest; StuckCount = 0;
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
		const FVector MoveDir = bFinal ? Dir : GM->SteerAround(this, Me, Dir);
		float Moved = MoveNav(MoveDir * StepLen, bFinal ? 0.f : FTSCNavGrid::WallRadius);
		if (bZoneBlocked && !MoveDir.Equals(Dir))
		{
			bZoneBlocked = false; // only the sidestep hit the palisade: try the straight step
			Moved = MoveNav(Dir * StepLen, FTSCNavGrid::WallRadius);
		}
		else if (!bZoneBlocked && Moved < 0.2f * StepLen && !MoveDir.Equals(Dir)) { Moved = MoveNav(Dir * StepLen, FTSCNavGrid::WallRadius); } // the sidestep was walled in
		if (bZoneBlocked)
		{
			// As before the caller picks another goal; keep a straight path so the next call does not count as arrived.
			Path.Reset();
			Path.Add(FVector(Dest.X, Dest.Y, 0.f));
			PathIdx = 0;
			return false;
		}
		const FRotator Want = Dir.Rotation();
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Want.Yaw, 0.f), Dt, 10.f));
	}
	return false;
}

void ATSCUnit::Sim(float Dt)
{
	if (!IsTimberWorker()) { CancelTimber(); }
	UpdateWading(Dt);
	UpdateArrows(Dt); // arrows already in the air keep flying even if the hunter falls
	// TEMP DIAG (remove): weapon state a few seconds in.
	{
		static TSet<const ATSCUnit*> Logged;
		DiagT += Dt;
		if (Mesh && DiagT > 3.f && !Logged.Contains(this) && (bNamed || Role == ETSCRole::Raider))
		{
			Logged.Add(this);
			TArray<UStaticMeshComponent*> All;
			for (UStaticMeshComponent* C : ArmedItems) { All.Add(C); }
			if (HeldItem) { All.Add(HeldItem); }
			UE_LOG(LogTemp, Warning, TEXT("[WDIAG] %s role=%d armed=%d hidden=%d items=%d meshScale=%s"), *DisplayName, (int32)Role, bArmed, IsHidden(), All.Num(), *Mesh->GetComponentScale().ToString());
			for (UStaticMeshComponent* C : All)
			{
				const FName S = C->GetAttachSocketName();
				UE_LOG(LogTemp, Warning, TEXT("[WDIAG]   %s socket=%s vis=%d worldScale=%s extent=%s distToSocket=%.1f socketScale=%s"),
					*GetNameSafe(C->GetStaticMesh()), *S.ToString(), C->IsVisible(), *C->GetComponentScale().ToString(),
					*C->Bounds.BoxExtent.ToString(), FVector::Dist(C->Bounds.Origin, Mesh->GetSocketLocation(S)),
					*Mesh->GetSocketTransform(S, RTS_World).GetScale3D().ToString());
			}
		}
	}
	if (bGone) { return; }
	if (bDead)
	{
		DeathTimer += Dt;
		if (Role == ETSCRole::Creature)
		{
			// Primitive figure: shrinks away at once. Skinned mushroom: the Death clip (1 s) plays, it lies a moment, then shrinks.
			const float ShrinkAt = Mesh ? 1.6f : 0.f;
			SetActorScale3D(FVector(FMath::Max(0.01f, 1.f - FMath::Max(0.f, DeathTimer - ShrinkAt) * (Mesh ? 1.1f : 1.6f))));
		}
		else
		{
			if (!Mesh) // the Viking mesh falls with its Death clip
			{
				Body->SetRelativeRotation(FRotator(0.f, 0.f, FMath::Min(DeathTimer * 260.f, 90.f)));
			}
			if (DeathTimer > 5.f)
			{
				Body->SetRelativeLocation(FVector(0.f, 0.f, -(DeathTimer - 5.f) * 30.f));
			}
		}
		if (DeathTimer > (Role == ETSCRole::Creature ? (Mesh ? 2.5f : 0.7f) : 8.f))
		{
			bGone = true;
			SetActorHiddenInGame(true);
		}
		return;
	}
	if (bSheltered) { return; }

	if (Role == ETSCRole::Husky)
	{
		DogSoundCooldown = FMath::Max(0.f, DogSoundCooldown - Dt);
	}
	BarTimer = FMath::Max(0.f, BarTimer - Dt);
	AttackTimer = FMath::Max(0.f, AttackTimer - Dt);
	bool bMoving = false;

	if (bDowned)
	{
		if (!Mesh)
		{
			Body->SetRelativeRotation(FRotator(0.f, 0.f, 82.f));
		}
		else if (Anims[(int32)ETSCAnim::Death] && CurAnim != Anims[(int32)ETSCAnim::Death])
		{
			// Lying down: the Death clip holds its last (lying) pose.
			OneShotTimer = 0.f;
			PlayAnim(ETSCAnim::Death, false);
		}
		return;
	}
	Body->SetRelativeRotation(FRotator::ZeroRotator);

	// ----- self-defence: a worker attacked at his job draws sword (or axe) and shield, fights, then sheathes them and goes back to work -----
	// (at night every villager is a defender already, see StartNight)
	if (Role == ETSCRole::Worker && !bDefender && Order != ETSCOrder::Leave && Order != ETSCOrder::Shelter)
	{
		SelfDefenseHold = FMath::Max(0.f, SelfDefenseHold - Dt);
		SelfDefenseThink -= Dt;
		if (SelfDefenseThink <= 0.f)
		{
			SelfDefenseThink = 0.4f;
			if (FindTarget()) { SelfDefenseHold = 4.f; } // keeps the weapons out a moment after the last enemy
		}
		const bool bDefend = SelfDefenseHold > 0.f;
		if (bDefend != bFights)
		{
			bFights = bDefend;
			SetArmed(bDefend);
			if (!bDefend) { Target = nullptr; }
		}
	}

	if (Mesh) { UpdateHeld(); }

	// ----- fighting -----
	if (bFights && Order != ETSCOrder::Leave && Order != ETSCOrder::Shelter)
	{
		ThinkTimer -= Dt;
		ATSCUnit* T = Target.Get();
		if (ThinkTimer <= 0.f || !T || !T->IsTargetable())
		{
			ThinkTimer = 0.4f;
			T = FindTarget();
			Target = T;
		}
		if (T)
		{
			CancelTimber();
			PlayTimer = 0.f; // a fight ends any romp or petting
			PetTimer = 0.f;
			BarTimer = 6.f;
			const FVector TL = T->Loc2D();
			const float D = FVector::Dist2D(GetActorLocation(), TL);
			const bool bArcher = AttackRange > 300.f;
			const bool bHusky = Role == ETSCRole::Husky;
			if (bHusky)
			{
				DogRunTime = 0.f;
				if (!bDogGrowled && DogSoundCooldown <= 0.f && (D > AttackRange || AttackTimer > 0.f))
				{
					GM->PlayDogSound(*this, ETSCDogSound::Growl);
					bDogGrowled = true;
					DogSoundCooldown = 1.6f;
				}
			}
			if (D > AttackRange)
			{
				if (DrawTimer > 0.f && D > AttackRange * 1.3f) { CancelDraw(); } // the target ran away mid-draw
				if (bHusky) { bGallop = true; Speed = 320.f; } // charge in at a gallop
				StepToward(TL, Dt, AttackRange * 0.85f);
				bMoving = true;
			}
			else if (bArcher && !HasShot(T))
			{
				// A fence, the palisade or a building is in the way: no shot. Walk round until the line is clear.
				if (DrawTimer > 0.f) { CancelDraw(); }
				StepToward(TL, Dt, 150.f);
				bMoving = true;
			}
			else
			{
				const FRotator Want = (TL - Loc2D()).Rotation();
				SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Want.Yaw, 0.f), Dt, 12.f));
				if (AttackTimer <= 0.f)
				{
					if (bHusky && DogSoundCooldown <= 0.f)
					{
						GM->PlayDogSound(*this, ETSCDogSound::AttackBark);
						DogSoundCooldown = 3.2f;
					}
					AttackTimer = AttackInterval * FMath::FRandRange(0.9f, 1.1f);
					if (bArcher)
					{
						// No shot clip (one-handed): the arms are posed procedurally for the draw, see ApplyBowPose. The arrow is nocked,
						// pulled back, then loosed and the hit lands (UpdateArrows). A tracer line is the fallback without the arrow asset.
						SwingTimer = 0.35f;
						BeginDraw(T);
					}
					else
					{
						StartSwing(0.35f, AttackTimer, Role == ETSCRole::Healer ? ETSCAnim::Punch : ETSCAnim::Slash);
						if (bUsesCombatAxe && Damage > 0.f && !T->bInvulnerable)
						{
							GM->PlayAxeImpact(T->GetActorLocation(), false);
						}
						T->Hurt(Damage);
					}
				}
			}
			Animate(Dt, bMoving);
			return;
		}
	}

	bDogGrowled = false;

	// ----- husky vs boar: a boar near the dog makes it stop, face the boar and bark loudly at it until it is gone -----
	if (Role == ETSCRole::Husky && Order != ETSCOrder::Leave && Order != ETSCOrder::Shelter && !IsHidden())
	{
		const ATSCUnit* Boar = nullptr;
		float BestD = BoarAlertRadius;
		for (const ATSCUnit* U : GM->Units)
		{
			if (!U || U->Role != ETSCRole::Boar || !U->IsAlive() || U->IsHidden()) { continue; }
			const float D = FVector::Dist2D(GetActorLocation(), U->GetActorLocation());
			if (D < BestD) { BestD = D; Boar = U; }
		}
		if (Boar)
		{
			PlayTimer = 0.f; // no romping or petting while a boar is about
			PetTimer = 0.f;
			DogRunTime = 0.f;
			const FRotator Face = (Boar->Loc2D() - Loc2D()).Rotation();
			SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Face.Yaw, 0.f), Dt, 8.f));
			if (DogSoundCooldown <= 0.f)
			{
				GM->PlayDogSound(*this, ETSCDogSound::AlertBark);
				DogSoundCooldown = FMath::FRandRange(0.8f, 1.3f);
			}
			Animate(Dt, false);
			return;
		}
	}

	// ----- mourning: stop, turn toward the fallen, stand a moment, then back to the order -----
	if (MournTimer > 0.f && Order != ETSCOrder::Leave && Order != ETSCOrder::Shelter)
	{
		MournTimer -= Dt;
		const FRotator Face = (MournAt - Loc2D()).Rotation();
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Face.Yaw, 0.f), Dt, 4.f));
		Animate(Dt, false);
		return;
	}

	// ----- orders -----
	switch (Order)
	{
	case ETSCOrder::Work:
	{
		if (IsTimberWorker())
		{
			bMoving = SimTimberWork(Dt);
			break;
		}
		const FVector& Dest = WorkPhase == 0 ? WorkA : WorkB;
		if (WorkTimer > 0.f)
		{
			WorkTimer -= Dt;
			if (WorkPhase == 0 && !WorkFace.IsZero())
			{
				const FRotator Face = (WorkFace - Loc2D()).Rotation();
				SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Face.Yaw, 0.f), Dt, 8.f));
			}
			if (WorkPhase == 0 && SwingTimer <= 0.f && OneShotTimer <= 0.f)
			{
				// Chopping (axe) or gathering (picking up).
				const bool bChop = Role == ETSCRole::Woodcutter || (Role == ETSCRole::Worker && Job == ETSCJob::Wood);
				StartSwing(0.6f, 0.f, bChop ? ETSCAnim::Slash : ETSCAnim::PickUp);
				if (bChop) { GM->PlayAxeImpact(GetActorLocation(), true); }
			}
			if (WorkTimer <= 0.f) { WorkPhase = 1 - WorkPhase; }
		}
		else if (StepToward(Dest, Dt, 40.f))
		{
			WorkTimer = WorkPhase == 0 ? FMath::FRandRange(4.f, 7.f) : FMath::FRandRange(1.f, 2.5f);
		}
		else
		{
			bMoving = true;
		}
		break;
	}
	case ETSCOrder::Hold:
		bMoving = !StepToward(HoldPos, Dt, 30.f);
		break;
	case ETSCOrder::Patrol:
		if (PatrolRoute.Num() < 2) { break; }
		if (PatrolPause > 0.f) { PatrolPause -= Dt; break; }
		PatrolLegTime += Dt;
		if (StepToward(PatrolRoute[PatrolIdx], Dt, 80.f) || PatrolLegTime > 30.f)
		{
			// every other corner the hero stops and looks around
			if (PatrolIdx % 2 == 0 && PatrolLegTime <= 30.f) { PatrolPause = FMath::FRandRange(1.5f, 3.f); }
			PatrolIdx = (PatrolIdx + 1) % PatrolRoute.Num();
			PatrolLegTime = 0.f;
		}
		else
		{
			bMoving = true;
		}
		break;
	case ETSCOrder::Shelter:
		if (StepToward(HoldPos, Dt, 50.f))
		{
			bSheltered = true;
			SetActorHiddenInGame(true);
		}
		bMoving = true;
		break;
	case ETSCOrder::Leave:
		if (StepToward(HoldPos, Dt, 50.f))
		{
			bGone = true;
			SetActorHiddenInGame(true);
		}
		bMoving = true;
		break;
	case ETSCOrder::Wander:
	case ETSCOrder::Follow:
		bMoving = SimWildlife(Dt);
		break;
	case ETSCOrder::Assault:
	{
		FVector Goal;
		ETSCStructure Struct = ETSCStructure::None;
		GM->GetAssaultGoal(this, Goal, Struct);
		if (StepToward(Goal, Dt, 90.f))
		{
			if (Struct != ETSCStructure::None && AttackTimer <= 0.f)
			{
				AttackTimer = AttackInterval;
				StartSwing(0.35f, AttackInterval, ETSCAnim::Slash);
				GM->DamageStructure(Struct, Damage * 0.3f);
			}
		}
		else
		{
			bMoving = true;
		}
		break;
	}
	}
	if (Role == ETSCRole::Husky)
	{
		const bool bRunning = bMoving && bGallop && !IsHidden();
		DogRunTime = bRunning ? DogRunTime + Dt : 0.f;
		if (DogRunTime >= 2.5f && DogSoundCooldown <= 0.f)
		{
			GM->PlayDogSound(*this, ETSCDogSound::Pant);
			DogRunTime = 0.f;
			DogSoundCooldown = 6.f;
		}
	}
	Animate(Dt, bMoving);
}

void ATSCUnit::Animate(float Dt, bool bMoving)
{
	AnimPhase += Dt * (bMoving ? 11.f : 2.f);
	SwingTimer = FMath::Max(0.f, SwingTimer - Dt);

	if (Role == ETSCRole::Creature && Mesh)
	{
		// Skinned mushroom (Bull clips on its own legs): it walks on the ground, no drift. The ram is the swing's lunge: the
		// whole body moves forward and back over SwingTimer (the Bull's headbutt moves a head the mushrooms do not have).
		const float Lunge = SwingTimer > 0.f ? CreatureLunge * FMath::Sin(FMath::Clamp(SwingTimer / 0.35f, 0.f, 1.f) * PI) : 0.f;
		Body->SetRelativeLocation(FVector(Lunge, 0.f, 0.f));
	}
	else if (Role == ETSCRole::Creature)
	{
		// Drift: slow bob, tendrils sway.
		Body->SetRelativeLocation(FVector(0.f, 0.f, 14.f * FMath::Sin(AnimPhase * 0.25f)));
		for (int32 I = 0; I < Tendrils.Num(); ++I)
		{
			const float Ph = AnimPhase * 0.3f + I * 1.1f;
			Tendrils[I]->SetRelativeRotation(FRotator(16.f * FMath::Sin(Ph) + (bMoving ? -14.f : 0.f), 0.f, 14.f * FMath::Cos(Ph * 0.8f)));
		}
		if (SwingTimer > 0.f)
		{
			Body->SetRelativeLocation(FVector(40.f * FMath::Sin(SwingTimer / 0.35f * PI), 0.f, 0.f));
		}
		return;
	}

	if (Mesh)
	{
		// Skinned Viking: one-shot clips (attack, work, hit) play out unless the unit starts moving, then locomotion.
		if (bMoving) { OneShotTimer = 0.f; }
		OneShotTimer -= Dt;
		if (OneShotTimer > 0.f) { return; }
		if (!bMoving)
		{
			PlayAnim(ETSCAnim::Idle, true);
			return;
		}
		// The mushrooms (speed 105) walk; the gallop is for a fast scale-up only.
		const bool bRun = bGallop || (!IsWildlife() && Role != ETSCRole::Creature && (Target.IsValid() || Order == ETSCOrder::Assault || Order == ETSCOrder::Shelter));
		const bool bCarry = Order == ETSCOrder::Work && WorkPhase == 1 && Role != ETSCRole::Hunter;
		PlayAnim(bCarry ? (bRun ? ETSCAnim::RunCarry : ETSCAnim::WalkCarry) : (bRun ? ETSCAnim::Run : ETSCAnim::Walk), true);
		return;
	}

	const float Bob = bMoving ? FMath::Abs(FMath::Sin(AnimPhase)) * 7.f : 0.f;
	Body->SetRelativeLocation(FVector(0.f, 0.f, Bob));
	if (bMoving)
	{
		Body->SetRelativeRotation(FRotator(0.f, 0.f, 5.f * FMath::Sin(AnimPhase)));
	}
	if (Arm)
	{
		float Pitch = bMoving ? 18.f * FMath::Sin(AnimPhase) : 0.f;
		if (SwingTimer > 0.f)
		{
			Pitch = -95.f * FMath::Sin(FMath::Clamp(SwingTimer / 0.35f, 0.f, 1.f) * PI);
		}
		Arm->SetRelativeRotation(FRotator(Pitch, 0.f, 0.f));
	}
}

// A work cycle has two completed stages on the same reserved actor. Walking/carrying never completes a stage.
bool ATSCUnit::IsTimberWorker() const
{
	return IsValid(GM) && GM->Phase == ETSCPhase::Day && IsAlive() && !bDowned && !bSheltered && !bScriptWork
		&& Order == ETSCOrder::Work && !WorkA.Equals(WorkB)
		&& (Role == ETSCRole::Woodcutter || (Role == ETSCRole::Worker && Job == ETSCJob::Wood));
}

void ATSCUnit::CancelTimber()
{
	if (IsValid(GM)) { GM->ReleaseTimber(this); }
	if (TimberTarget.IsValid() || TimberStageDuration > 0.f)
	{
		WorkTimer = 0.f;
		WorkPhase = 0;
	}
	TimberTarget.Reset();
	TimberStageDuration = 0.f;
	TimberPathVersion = -1;
}

void ATSCUnit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelTimber();
	Super::EndPlay(EndPlayReason);
}

bool ATSCUnit::SimTimberWork(float Dt)
{
	if (WorkPhase == 1) // keep the existing return-to-store/carry animation; economy is still rate based
	{
		if (!StepToward(WorkB, Dt, 40.f)) { return true; }
		if (WorkTimer <= 0.f) { WorkTimer = FMath::FRandRange(1.f, 2.5f); }
		WorkTimer -= Dt;
		if (WorkTimer <= 0.f) { WorkPhase = 0; }
		return false;
	}
	FVector Spot;
	ATSCPrimActor* Tree = TimberTarget.Get();
	if (Tree && !GM->ValidateTimber(this, Tree, Spot))
	{
		CancelTimber();
		Tree = nullptr;
	}
	if (!Tree)
	{
		TimberRetry -= Dt;
		if (TimberRetry > 0.f) { return false; }
		Tree = GM->ReserveTimber(this, Spot);
		TimberTarget = Tree;
		if (!Tree) { TimberRetry = 1.f; return false; }
		TimberStageDuration = FMath::FRandRange(4.f, 7.f) * 0.5f;
		WorkTimer = 0.f;
		TimberPathVersion = -1;
	}
	if (TimberPathVersion != GM->GridVersion)
	{
		TArray<FVector> CheckPath;
		if (!GM->FindPath(Loc2D(), Spot, CheckPath, NavLayer()) || CheckPath.IsEmpty()
			|| FVector::Dist2D(CheckPath.Last(), Spot) > 5.f)
		{
			CancelTimber();
			TimberRetry = 0.3f;
			return false;
		}
		TimberPathVersion = GM->GridVersion;
		PathVersion = -1;
	}
	if (!StepToward(Spot, Dt, 30.f)) { return true; }
	const FRotator Face = (Tree->GetActorLocation() - Loc2D()).Rotation();
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Face.Yaw, 0.f), Dt, 12.f));
	if (WorkTimer <= 0.f) { WorkTimer = TimberStageDuration; }
	if (SwingTimer <= 0.f && OneShotTimer <= 0.f)
	{
		StartSwing(0.6f, 0.f, ETSCAnim::Slash);
		GM->PlayAxeImpact(Tree->GetActorLocation(), true);
	}
	WorkTimer -= Dt;
	if (WorkTimer <= 0.f)
	{
		const int32 Stage = GM->CompleteTimberStage(this, Tree);
		if (Stage != 1)
		{
			TimberTarget.Reset();
			TimberStageDuration = 0.f;
			WorkPhase = Stage == 2 ? 1 : 0;
		}
	}
	return false;
}
