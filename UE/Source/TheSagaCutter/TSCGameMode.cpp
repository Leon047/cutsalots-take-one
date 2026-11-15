// The Saga Cutter - game jam project.

#include "TSCGameMode.h"

#include "Sound/SoundAttenuation.h"

#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "ParticleEmitterInstances.h"
#include "ParticleHelper.h"
#include "Particles/ParticleSystemComponent.h"
#include "UObject/UObjectIterator.h"
#include "SceneInterface.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "TSCColonyCameraPawn.h"
#include "UObject/ConstructorHelpers.h"
#include "TSCHUD.h"
#include "TSCLoc.h"
#include "TSCOcean.h"
#include "TSCTerrain.h"
#include "TSCPlayerController.h"
#include "TSCPrimActor.h"
#include "TheSagaCutter.h"

using namespace TSC;

namespace
{
	const int32 GateX = 8, GateY = 11;          // gate cells (8,11) and (8,12)
	const int32 TreeX = 9, TreeY = 10;          // the spruce by the gate
	const int32 TreeFall = 5;                   // cells south of it that the fallen trunk covers (the gateway and beyond)
	const FVector2D SprucePos(8.7f, 10.3f);     // the spruce itself stands right inside the north gate post (cell units, see CellF)
	const FVector2D UlfChop(9.45f, 9.5f);       // where Ulf stands to fell it: north-east of the trunk, clear of its fall
	const FIntRect ForestRect(-6, -4, 8, 28);   // max exclusive
	const FIntRect GardenRect(15, 14, 21, 19);
	const FIntRect GroveRect(10, 19, 21, 27);
	const FIntRect DockRect(26, 9, 34, 15);

	// The palisade: a superellipse in cell units (between a circle and a square) with its west apex on the gate,
	// running from the north shore round the west to the south shore. Open to the water in the east.
	const float WallCX = 19.f, WallCY = 12.f, WallA = 11.f, WallB = 14.f, WallN = 2.8f, WallEndX = 27.55f;
	float SignedPow(float V, float E) { return FMath::Sign(V) * FMath::Pow(FMath::Abs(V), E); }
	FVector2D WallCellAt(float T)
	{
		return FVector2D(WallCX - WallA * SignedPow(FMath::Cos(T), 2.f / WallN), WallCY + WallB * SignedPow(FMath::Sin(T), 2.f / WallN));
	}
	/** < 1 inside the wall curve. */
	float WallShape(float Cx, float Cy) { return FMath::Pow(FMath::Abs(Cx - WallCX) / WallA, WallN) + FMath::Pow(FMath::Abs(Cy - WallCY) / WallB, WallN); }
	/** Curve parameter of the south shore end (the north end is its negative). */
	float WallEndT() { return FMath::Acos(-FMath::Pow((WallEndX - WallCX) / WallA, WallN * 0.5f)); }

	uint32 Hash2(int32 X, int32 Y) { return HashCombine(GetTypeHash(X * 73856093), GetTypeHash(Y * 19349663)); }
	float Rand01(int32 X, int32 Y, int32 Salt = 0) { return (Hash2(X + Salt * 31, Y - Salt * 17) % 10000) / 10000.f; }

	// Feeds the level's wind (the WindDirectionalSource that also drives the banner cloth) into MPC_Wind, which
	// M_PineTree reads for its sway. The phase is integrated here so a changing wind speed never makes the trees jump.
	void UpdatePineWind(UWorld* World, float Dt)
	{
		static UMaterialParameterCollection* Wind = []
		{
			UMaterialParameterCollection* Loaded = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Trees/MPC_Wind.MPC_Wind"));
			if (Loaded) { Loaded->AddToRoot(); }
			return Loaded;
		}();
		if (!Wind || !World || !World->Scene) { return; }

		FVector Dir = FVector::ForwardVector;
		float Speed = 0.f, MinGust = 0.f, MaxGust = 0.f;
		World->Scene->GetWindParameters_GameThread(FVector::ZeroVector, Dir, Speed, MinGust, MaxGust);

		static float Phase = 0.f;
		static float SmoothSpeed = 1.f;
		static FVector SmoothDir = FVector::ForwardVector;
		SmoothSpeed = FMath::FInterpTo(SmoothSpeed, Speed, Dt, 1.5f);
		if (!Dir.IsNearlyZero()) { SmoothDir = FMath::VInterpTo(SmoothDir, Dir.GetSafeNormal(), Dt, 1.5f); }
		Phase = FMath::Fmod(Phase + Dt * (0.9f + 0.45f * SmoothSpeed), 2000.f * UE_PI);

		UKismetMaterialLibrary::SetScalarParameterValue(World, Wind, TEXT("WindPhase"), Phase);
		UKismetMaterialLibrary::SetScalarParameterValue(World, Wind, TEXT("WindAmp"), FMath::Clamp(0.45f + 0.3f * SmoothSpeed, 0.3f, 2.f));
		UKismetMaterialLibrary::SetVectorParameterValue(World, Wind, TEXT("WindDir"), FLinearColor(SmoothDir.X, SmoothDir.Y, 0.f, 0.f));

		// Chimney smoke: the plume rises straight up and the current wind carries every live particle along, so the
		// whole column bends together when the wind shifts. Gusts come from two slow sines on top of the level's wind.
		static float GustTime = 0.f;
		GustTime += Dt;
		const float Gust = 1.f + 0.35f * FMath::Sin(GustTime * 0.37f) + 0.2f * FMath::Sin(GustTime * 1.13f + 1.7f);
		const FVector Side(-SmoothDir.Y, SmoothDir.X, 0.f);
		const FVector Drift = (SmoothDir * (75.f * SmoothSpeed * Gust) + Side * (28.f * SmoothSpeed * FMath::Sin(GustTime * 0.23f))) * Dt;
		for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
		{
			if (It->GetWorld() != World || !It->ComponentHasTag(TEXT("TSC_ChimneySmoke"))) { continue; }
			It->SetWorldRotation(FRotator::ZeroRotator);
			It->ForceAsyncWorkCompletion(UParticleSystemComponent::STALL);
			for (FParticleEmitterInstance* Inst : It->EmitterInstances)
			{
				if (!Inst || !Inst->ParticleData || !Inst->ParticleIndices) { continue; }
				for (int32 I = 0; I < Inst->ActiveParticles; ++I)
				{
					DECLARE_PARTICLE(Particle, Inst->ParticleData + Inst->ParticleStride * Inst->ParticleIndices[I]);
					Particle.Location += Drift;
					Particle.OldLocation += Drift;
				}
			}
		}
	}

	// Prologue beats (seconds): landfall, clearing, raising, the forest wakes, hold and fade.
	const float ProTLand = 8.f, ProTClear = 20.f, ProTRaise = 34.f, ProTWake = 45.f, ProTEnd = 51.f;
	const float ProShipRun = 2400.f;
	/** Seconds the sail takes to come down once the ship has stopped; the crew lands after that. */
	const float ProSailDrop = 2.f;
	/** The road fence is put up one piece after another over this many seconds, starting once the village wall is done. */
	const float ProFenceSpan = 8.f;
	const float DefaultCamArm = 16500.f;
	TAutoConsoleVariable<int32> CVarPrologue(TEXT("tsc.Prologue"), 1,
		TEXT("0: the title screen shows the finished village instead of the landfall prologue (also -NoPrologue)."));
#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarNavCheck(TEXT("tsc.NavCheck"), 1,
		TEXT("1: counts units that step into a blocked nav cell (see [Nav] STATS in the log)."));
	TAutoConsoleVariable<int32> CVarNavDebug(TEXT("tsc.NavDebug"), 0,
		TEXT("1: draws the nav grid, unit radii and paths."));
#endif
}

ATSCGameMode::ATSCGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = ATSCColonyCameraPawn::StaticClass();
	PlayerControllerClass = ATSCPlayerController::StaticClass();
	HUDClass = ATSCHUD::StaticClass();

	static ConstructorHelpers::FObjectFinder<USoundBase> MenuCue(TEXT("/Game/Vikings/Audio/Music/SC_Menu.SC_Menu"));
	static ConstructorHelpers::FObjectFinder<USoundBase> DayCue(TEXT("/Game/Vikings/Audio/Music/SC_MainTheme.SC_MainTheme"));
	static ConstructorHelpers::FObjectFinder<USoundBase> NightCue(TEXT("/Game/Vikings/Audio/Music/SC_Action.SC_Action"));
	static ConstructorHelpers::FObjectFinder<USoundBase> VictoryCue(TEXT("/Game/Vikings/Audio/Music/SC_Victory.SC_Victory"));
	MenuMusic = MenuCue.Object;
	DayMusic = DayCue.Object;
	NightMusic = NightCue.Object;
	VictoryMusic = VictoryCue.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> Bark1(TEXT("/Game/Vikings/Audio/Dog/SW_DogBark_1.SW_DogBark_1"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Bark2(TEXT("/Game/Vikings/Audio/Dog/SW_DogBark_2.SW_DogBark_2"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Pant(TEXT("/Game/Vikings/Audio/Dog/SW_DogPant.SW_DogPant"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Howl(TEXT("/Game/Vikings/Audio/Dog/SW_DogHowl_TakeC.SW_DogHowl_TakeC"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Growl(TEXT("/Game/Vikings/Audio/Dog/SW_DogGrowl.SW_DogGrowl"));
	static ConstructorHelpers::FObjectFinder<USoundBase> AttackBark(TEXT("/Game/Vikings/Audio/Dog/SW_DogAttackBark.SW_DogAttackBark"));
	if (Bark1.Object) { DogBarks.Add(Bark1.Object); }
	if (Bark2.Object) { DogBarks.Add(Bark2.Object); }
	DogPant = Pant.Object;
	DogHowl = Howl.Object;
	DogGrowl = Growl.Object;
	DogAttackBark = AttackBark.Object;
	// The colony camera sits 60 to 200 m from the ground.
	DogAttenuation = CreateDefaultSubobject<USoundAttenuation>(TEXT("DogAttenuation"));
	DogAttenuation->Attenuation.AttenuationShapeExtents = FVector(9000.f, 0.f, 0.f);
	DogAttenuation->Attenuation.FalloffDistance = 16000.f;

	static ConstructorHelpers::FObjectFinder<USoundBase> AxeWood(TEXT("/Game/Vikings/Audio/Axe/SW_AxeWood.SW_AxeWood"));
	static ConstructorHelpers::FObjectFinder<USoundBase> AxeEnemy(TEXT("/Game/Vikings/Audio/Axe/SW_AxeEnemy.SW_AxeEnemy"));
	AxeWoodImpact = AxeWood.Object;
	AxeEnemyImpact = AxeEnemy.Object;
	AxeAttenuation = CreateDefaultSubobject<USoundAttenuation>(TEXT("AxeAttenuation"));
	AxeAttenuation->Attenuation.AttenuationShapeExtents = FVector(9000.f, 0.f, 0.f);
	AxeAttenuation->Attenuation.FalloffDistance = 16000.f;
}

void ATSCGameMode::PlayAxeImpact(const FVector& Location, bool bWood)
{
	USoundBase* Sound = bWood ? AxeWoodImpact.Get() : AxeEnemyImpact.Get();
	if (!Sound) { return; }

	UGameplayStatics::PlaySoundAtLocation(this, Sound, Location,
		bWood ? 0.46f : 0.64f, FMath::FRandRange(0.92f, 1.08f), 0.f, AxeAttenuation);
}

void ATSCGameMode::PlayDogSound(const ATSCUnit& Dog, ETSCDogSound Cue)
{
	if (Dog.IsHidden()) { return; }
	USoundBase* Sound = nullptr;
	float Volume = 0.58f;
	switch (Cue)
	{
	case ETSCDogSound::PlayBark:
		if (!DogBarks.IsEmpty()) { Sound = DogBarks[FMath::RandHelper(DogBarks.Num())]; }
		break;
	case ETSCDogSound::Pant: Sound = DogPant; Volume = 0.48f; break;
	case ETSCDogSound::Growl: Sound = DogGrowl; Volume = 0.65f; break;
	case ETSCDogSound::AttackBark: Sound = DogAttackBark; Volume = 0.72f; break;
	}
	if (!Sound) { return; }

	UGameplayStatics::PlaySoundAtLocation(this, Sound, Dog.GetActorLocation(),
		Volume, FMath::FRandRange(0.96f, 1.04f), 0.f, DogAttenuation);
}

void ATSCGameMode::PlayMusic(USoundBase* Sound)
{
	if (Sound == CurrentMusic && MusicComp && MusicComp->IsPlaying()) { return; }
	GetWorldTimerManager().ClearTimer(MusicTimer);
	if (MusicComp) { MusicComp->FadeOut(MusicFadeTime, 0.f); }  // auto-destroys when the fade ends
	MusicComp = nullptr;
	CurrentMusic = Sound;
	UE_LOG(LogSaga, Display, TEXT("[Saga] Music: %s"), Sound ? *Sound->GetName() : TEXT("none"));
	if (!Sound) { return; }
	MusicComp = UGameplayStatics::SpawnSound2D(this, Sound, MusicVolume, 1.f, 0.f, nullptr, false, true);
	if (MusicComp)
	{
		MusicComp->bIsUISound = true;  // keeps playing while the game is paused
		MusicComp->FadeIn(MusicFadeTime, MusicVolume);
	}
}

// =====================================================================================================================
// Grid
// =====================================================================================================================

FVector ATSCGameMode::CellCorner(int32 Cx, int32 Cy) const { return FVector((Cx - GW / 2) * CELL, (Cy - GH / 2) * CELL, 0.f); }
FVector ATSCGameMode::CellCenter(int32 Cx, int32 Cy) const { return CellCorner(Cx, Cy) + FVector(CELL * 0.5f, CELL * 0.5f, 0.f); }
FVector ATSCGameMode::CellF(float Cx, float Cy) const { return FVector((Cx - GW / 2) * CELL, (Cy - GH / 2) * CELL, 0.f); }

FIntPoint ATSCGameMode::WorldToCell(const FVector& P) const
{
	return FIntPoint(FMath::FloorToInt(P.X / CELL) + GW / 2, FMath::FloorToInt(P.Y / CELL) + GH / 2);
}

void ATSCGameMode::SetBlocked(int32 Cx, int32 Cy, bool bBlock)
{
	if (InGrid(Cx, Cy)) { Blocked[Cy * GW + Cx] = bBlock ? 1 : 0; }
	Nav.SetCoarse(Cx, Cy, bBlock); // also the margin outside the grid (forest edge trees, wall arcs)
	++GridVersion;
}

void ATSCGameMode::SetSpruceFallen(bool bFallen)
{
	// Only the cells the trunk and crown took are freed again: the palisade and a house lie next to them.
	if (!bFallen)
	{
		for (const FIntPoint& C : SpruceCells) { SetBlocked(C.X, C.Y, false); }
		SpruceCells.Reset();
		return;
	}
	auto Take = [this](int32 X, int32 Y) { if (!IsBlocked(X, Y)) { SetBlocked(X, Y, true); SpruceCells.Add(FIntPoint(X, Y)); } };
	for (int32 Y = TreeY + 1; Y <= TreeY + TreeFall; ++Y) { Take(TreeX, Y); }
	for (int32 Y = TreeY + 3; Y < TreeY + TreeFall; ++Y) { Take(TreeX + 1, Y); } // the crown leans one column into the village
}

void ATSCGameMode::BlockRect(int32 Cx, int32 Cy, int32 W, int32 H, bool bBlock)
{
	for (int32 X = Cx; X < Cx + W; ++X) { for (int32 Y = Cy; Y < Cy + H; ++Y) { SetBlocked(X, Y, bBlock); } }
}

bool ATSCGameMode::FindPath(const FVector& From, const FVector& To, TArray<FVector>& Out, int32 Layer) const
{
	return Nav.FindPath(From, To, Out, Layer);
}

/** Units that take part in separation and steering: alive, on the map, visible. */
static bool TSCIsNavActive(const ATSCUnit* U)
{
	return U && U->IsAlive() && !U->bSheltered && !U->IsHidden();
}

void ATSCGameMode::SeparateUnits(float Dt)
{
	TArray<ATSCUnit*, TInlineAllocator<64>> Act;
	for (ATSCUnit* U : Units) { if (TSCIsNavActive(U)) { Act.Add(U); } }
	for (ATSCUnit* U : Wildlife) { if (TSCIsNavActive(U)) { Act.Add(U); } }
	const int32 N = Act.Num();
	if (N < 2) { return; }
	TArray<FVector, TInlineAllocator<64>> Push;
	Push.Init(FVector::ZeroVector, N);
	TArray<FVector, TInlineAllocator<64>> Pos;
	Pos.SetNum(N);
	for (int32 I = 0; I < N; ++I) { Pos[I] = Act[I]->Loc2D(); }
	for (int32 I = 0; I < N; ++I)
	{
		const ATSCUnit* A = Act[I];
		for (int32 J = I + 1; J < N; ++J)
		{
			const ATSCUnit* B = Act[J];
			const float Min = A->NavRadius + B->NavRadius;
			const FVector V = Pos[J] - Pos[I];
			const float D = V.Size2D();
			if (D >= Min) { continue; }
			const FVector Nrm = D < 1.f ? FVector(FMath::Cos(I * 2.4f), FMath::Sin(I * 2.4f), 0.f) : FVector(V.X / D, V.Y / D, 0.f);
			const float Pen = Min - D;
			if (A->bDowned && B->bDowned) { continue; } // neither may move
			if (A->bDowned) { Push[J] += Nrm * Pen; }
			else if (B->bDowned) { Push[I] -= Nrm * Pen; }
			else
			{
				Push[I] -= Nrm * (0.5f * Pen);
				Push[J] += Nrm * (0.5f * Pen);
			}
		}
	}
	const float Rate = FMath::Min(1.f, 12.f * Dt);
	const float Cap = 240.f * Dt;
	for (int32 I = 0; I < N; ++I)
	{
		if (Push[I].IsNearlyZero()) { continue; }
		FVector Delta = Push[I] * Rate;
		Delta.Z = 0.f;
		if (Delta.Size2D() > Cap) { Delta = Delta.GetSafeNormal2D() * Cap; }
		Act[I]->Nudge(Delta);
	}
}

FVector ATSCGameMode::SteerAround(const ATSCUnit* U, const FVector& Me, const FVector& Dir) const
{
	const FVector Perp(-Dir.Y, Dir.X, 0.f);
	const ATSCUnit* Tgt = U->Target.Get();
	const ATSCUnit* Lead = U->Leader.Get();
	float Sum = 0.f;
	auto Consider = [&](const ATSCUnit* O)
	{
		if (O == U || O == Tgt || O == Lead || !TSCIsNavActive(O)) { return; }
		const FVector V = O->Loc2D() - Me;
		const float D = V.Size2D();
		const float R = U->NavRadius + O->NavRadius;
		if (D > R + 60.f || D < 1.f) { return; }
		const float Ahead = V.X * Dir.X + V.Y * Dir.Y;
		if (Ahead <= 0.f) { return; }
		const float Lat = V.X * Perp.X + V.Y * Perp.Y;
		if (FMath::Abs(Lat) > R + 10.f) { return; }
		const float W = 1.f - FMath::Clamp((D - R) / 60.f, 0.f, 1.f);
		const float Side = Lat > 4.f ? -1.f : 1.f; // obstacle on +Perp: go -Perp; dead ahead: +Perp, so head-on walkers pass
		Sum += Side * W * 1.2f;
	};
	for (const ATSCUnit* O : Units) { Consider(O); }
	for (const ATSCUnit* O : Wildlife) { Consider(O); }
	const FVector Out = (Dir + Perp * FMath::Clamp(Sum, -1.5f, 1.5f)).GetSafeNormal2D();
	return Out.IsNearlyZero() ? Dir : Out;
}

bool ATSCGameMode::CanReach(const ATSCUnit* A, const ATSCUnit* B) const
{
	// outside = beyond the curve on the forest side (the open shore end counts as inside)
	auto Outside = [](const FVector& P)
	{
		const float Cx = P.X / CELL + GW / 2, Cy = P.Y / CELL + GH / 2;
		return Cx < WallCX && WallShape(Cx, Cy) > 1.f;
	};
	const bool bAWest = Outside(A->GetActorLocation());
	const bool bBWest = Outside(B->GetActorLocation());
	if (bAWest == bBWest) { return true; }
	return (!bGateClosed || bGateBroken) && !IsBlocked(TreeX, TreeY + 1);
}

bool ATSCGameMode::IsInsideSettlement(const FVector& P) const
{
	const float Cx = P.X / CELL + GW / 2, Cy = P.Y / CELL + GH / 2;
	return Cx < WallEndX && WallShape(Cx, Cy) < 1.f;
}

// =====================================================================================================================
// World
// =====================================================================================================================

ATSCPrimActor* ATSCGameMode::SpawnPrim(const FVector& Loc, int32 Region)
{
	ATSCPrimActor* A = GetWorld()->SpawnActor<ATSCPrimActor>(ATSCPrimActor::StaticClass(), Loc, FRotator::ZeroRotator);
	A->Region = Region;
	Prims.Add(A);
	return A;
}

ATSCUnit* ATSCGameMode::SpawnUnit(ETSCRole InRole, const FString& Name, const FVector& Loc)
{
	ATSCUnit* U = GetWorld()->SpawnActor<ATSCUnit>(ATSCUnit::StaticClass(), FVector(Loc.X, Loc.Y, 0.f), FRotator::ZeroRotator);
	U->Init(this, InRole, Name);
	Units.Add(U);
	return U;
}

// Ultra Dynamic Sky (Blueprint actor, /Game/ThirdParty/UltraDynamicSky): placed in the level or spawned; optional.
// The level owns it, so a weak pointer is enough.
static TWeakObjectPtr<AActor> GTSCSky;
static float GTSCSkyTime = 1100.f;
static TWeakObjectPtr<USkyLightComponent> GTSCSkyLight; // owned by the atmosphere actor
static FQuat GTSCDaySun = FRotator(-42.f, -22.f, 0.f).Quaternion(); // where the sun stands on its daily arc (eased)
static float GTSCSkyDrift = 0.f, GTSCShadowDrift = 0.f; // extra cloud travel earned while the game is fast-forwarded
static void TSCUpdateSky(UDirectionalLightComponent* Sun, float AshAmount, float K, bool bNight, float DayF, float Thick, float Dt, float SpeedMul);
ATSCUnit* ATSCGameMode::SpawnWildlife(ETSCRole InRole, const FVector& Loc, float Radius, int32 Region)
{
	ATSCUnit* U = GetWorld()->SpawnActor<ATSCUnit>(ATSCUnit::StaticClass(), FVector(Loc.X, Loc.Y, 0.f), FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f));
	U->Init(this, InRole, FString());
	U->Region = Region;
	// Forest animals stay outside the palisade, livestock inside it; the stag and the grove fox live where they were put.
	switch (InRole)
	{
	case ETSCRole::Deer: case ETSCRole::Wolf: case ETSCRole::Stag: case ETSCRole::Fox: U->Zone = ETSCZone::Outside; break; // all forest animals
	case ETSCRole::Cow: case ETSCRole::Bull: U->Zone = ETSCZone::Inside; break;
	case ETSCRole::Husky: if (Radius > 0.f && IsInsideSettlement(Loc)) { U->Zone = ETSCZone::Inside; } break; // the village dog

	default: break;
	}
	U->OrderWander(Loc, Radius);
	// Livestock and the village dog (a husky that roams instead of following) arrive only after the village is built.
	if (InRole == ETSCRole::Cow || InRole == ETSCRole::Bull || (InRole == ETSCRole::Husky && Radius > 0.f)) { FarmAnimals.Add(U); }
	Wildlife.Add(U);
	Prims.Add(U); // takes part in the Living / Ashen transition of its region
	return U;
}

void ATSCGameMode::SpawnAllWildlife()
{
	// The stag lives in the forest, outside the palisade (the grove is inside the wall).
	SpawnWildlife(ETSCRole::Stag, CellF(2.0f, 21.5f), 320.f, TSCRegion::Forest);
	// Deer graze in clearings of the western forest (north and south of the road).
	SpawnWildlife(ETSCRole::Deer, CellF(3.f, 6.f), 350.f, TSCRegion::Forest);
	SpawnWildlife(ETSCRole::Deer, CellF(3.6f, 6.8f), 350.f, TSCRegion::Forest);
	SpawnWildlife(ETSCRole::Deer, CellF(2.6f, 17.5f), 350.f, TSCRegion::Forest);
	// A wolf pack prowls the deep forest, far from the wall.
	SpawnWildlife(ETSCRole::Wolf, CellF(-2.f, 4.5f), 420.f, TSCRegion::Forest);
	SpawnWildlife(ETSCRole::Wolf, CellF(-2.6f, 5.2f), 420.f, TSCRegion::Forest);
	SpawnWildlife(ETSCRole::Wolf, CellF(-1.4f, 5.6f), 420.f, TSCRegion::Forest);
	// Foxes sneak along the forest edges: north of the clearing and behind the grove.
	SpawnWildlife(ETSCRole::Fox, CellF(6.4f, 2.2f), 300.f, TSCRegion::Forest);
	SpawnWildlife(ETSCRole::Fox, CellF(10.8f, 25.6f), 300.f, TSCRegion::Grove);
	// Village livestock grazes inside the cattle pen (cells 21..25 x 21..24, built in the level setup): the wander radius keeps them off the fence.
	SpawnWildlife(ETSCRole::Cow, CellF(22.4f, 22.2f), 130.f, TSCRegion::None);
	SpawnWildlife(ETSCRole::Cow, CellF(23.6f, 23.0f), 130.f, TSCRegion::None);
	SpawnWildlife(ETSCRole::Bull, CellF(23.0f, 22.6f), 130.f, TSCRegion::None);
	// The village dog runs about inside the palisade, between the hall and the pasture, and plays with the jarl.
	SpawnWildlife(ETSCRole::Husky, CellF(13.0f, 14.6f), 450.f, TSCRegion::None);
	// Hakon's husky trots after its master.
	if (Hunter)
	{
		ATSCUnit* Dog = SpawnWildlife(ETSCRole::Husky, Hunter->GetActorLocation() + FVector(-110.f, 60.f, 0.f), 0.f, TSCRegion::None);
		Dog->OrderFollow(Hunter);
		HunterDog = Dog;
	}
}

void ATSCGameMode::BuildAtmosphere()
{
	AtmoActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	USceneComponent* AtmoRoot = NewObject<USceneComponent>(AtmoActor);
	AtmoActor->SetRootComponent(AtmoRoot);
	AtmoRoot->RegisterComponent();

	auto MakeLight = [&](const FRotator& Rot, bool bShadows)
	{
		UDirectionalLightComponent* L = NewObject<UDirectionalLightComponent>(AtmoActor);
		L->SetMobility(EComponentMobility::Movable);
		L->SetupAttachment(AtmoRoot);
		L->SetCastShadows(bShadows);
		L->RegisterComponent();
		L->SetWorldRotation(Rot);
		return L;
	};
	Sun = MakeLight(FRotator(-42.f, -22.f, 0.f), true);
	Sun->SetAtmosphereSunLight(false);
	Sun->SetForwardShadingPriority(3);
	// ambient: a sky light that captures the sky dome live, so the bounce light follows the sky's colour through the day
	USkyLightComponent* Sky = NewObject<USkyLightComponent>(AtmoActor);
	Sky->SetMobility(EComponentMobility::Movable);
	Sky->SetupAttachment(AtmoRoot);
	Sky->bRealTimeCapture = true;
	Sky->bLowerHemisphereIsBlack = false;
	Sky->SetCastShadows(false);
	Sky->RegisterComponent();
	GTSCSkyLight = Sky;

	Fog = NewObject<UExponentialHeightFogComponent>(AtmoActor);
	Fog->SetupAttachment(AtmoRoot);
	Fog->RegisterComponent();
	Fog->SetFogHeightFalloff(0.55f);
	Fog->SetStartDistance(2000.f);

	// Sky: use the Ultra Dynamic Sky actor placed in the level, or spawn one. The game keeps its own lights and fog;
	// the sky only follows the game's clock (see UpdateSky). Everything is optional: no asset, no sky.
	AActor* SkyActor = nullptr;
	GTSCSkyTime = 1100.f;
	GTSCSkyDrift = GTSCShadowDrift = 0.f;
	if (UClass* SkyClass = LoadClass<AActor>(nullptr, TEXT("/Game/ThirdParty/UltraDynamicSky/Blueprints/Ultra_Dynamic_Sky_BP.Ultra_Dynamic_Sky_BP_C")))
	{
		for (TActorIterator<AActor> It(GetWorld(), SkyClass); It; ++It) { SkyActor = *It; break; }
		if (!SkyActor) { SkyActor = GetWorld()->SpawnActor<AActor>(SkyClass, FVector::ZeroVector, FRotator::ZeroRotator); }
	}
	GTSCSky = SkyActor;
	if (SkyActor)
	{
		if (FBoolProperty* P = FindFProperty<FBoolProperty>(SkyActor->GetClass(), TEXT("Animate Day/Night Cycle"))) { P->SetPropertyValue_InContainer(SkyActor, false); }
		if (FBoolProperty* P = FindFProperty<FBoolProperty>(SkyActor->GetClass(), TEXT("Simulate Cloud Density Changes"))) { P->SetPropertyValue_InContainer(SkyActor, false); }
		Fog->SetFogCutoffDistance(60000.f); // keep the height fog off the sky dome
		TInlineComponentArray<UPrimitiveComponent*> SkyPrims(SkyActor);
		for (UPrimitiveComponent* Prim : SkyPrims) { Prim->SetCastShadow(false); }
	}

	PostFx = NewObject<UPostProcessComponent>(AtmoActor);
	PostFx->SetupAttachment(AtmoRoot);
	PostFx->bUnbound = true;
	PostFx->RegisterComponent();
	FPostProcessSettings& S = PostFx->Settings;
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = AEM_Manual;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = 0.f;
	S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	S.AutoExposureApplyPhysicalCameraExposure = false;
	S.bOverride_ColorSaturation = true;
	S.bOverride_ColorGain = true;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 1.1f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.55f;
	UpdateAtmosphere(0.f);
}

void ATSCGameMode::BuildWorld()
{
	Blocked.Init(0, GW * GH);
	Nav.Init(GW, GH, CELL);
	// Margin outside the grid: open only west of the settlement's middle and outside the wall curve (forest animals live there).
	for (int32 Y = -FTSCNavGrid::PadN; Y < GH + FTSCNavGrid::PadS; ++Y)
	{
		for (int32 X = -FTSCNavGrid::PadW; X < GW + FTSCNavGrid::PadE; ++X)
		{
			if (!InGrid(X, Y) && (X >= 19 || WallShape(X + 0.5f, Y + 0.5f) < 1.f)) { Nav.SetCoarse(X, Y, true); }
		}
	}

	// ----- continuous ground and trail network -----
	{
		// The shore (a beach along a fractal waterline, X 1800 .. ShoreXAt(Y)) is built by the ocean.
		Ocean =GetWorld()->SpawnActor<ATSCOcean>(ATSCOcean::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		Ocean->Build(CellF(28.f, 12.f).X, 0.f);
		Terrain = GetWorld()->SpawnActor<ATSCTerrain>(ATSCTerrain::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		// Keep these routes in the same clearances as the original paths and through the gate opening.
		const TArray<TArray<FVector2D>> TrailCells = {
			{{-6, 12}, {1, 12.13}, {5, 11.89}, {8, 12}, {11.5, 12}, {12.9, 13.3}, {16.4, 13.5},
			 {19.7, 13.35}, {20.9, 12.3}, {23.5, 12}, {26.3, 12}},
			{{12.05, 12.5}, {13.5, 10.4}, {13.6, 6.8}, {13.7, 4.35}, {14.5, 3.6}, {16.1, 3.5}},
			{{14.6, 13.4}, {14.4, 15.2}, {14.56, 17.6}, {14.5, 19.6}},
			{{21.6, 14.65}, {21.45, 13.35}, {20.6, 11.8}, {20.5, 9.45}},
		};
		TArray<TArray<FVector2D>> TrailRoutes;
		for (const TArray<FVector2D>& Cells : TrailCells)
		{
			TArray<FVector2D>& Route = TrailRoutes.AddDefaulted_GetRef();
			for (const FVector2D& Cell : Cells) { Route.Add(FVector2D(CellF(Cell.X, Cell.Y))); }
		}
		Terrain->Build(Ocean, TrailRoutes);
	}
	// ----- palisade line (dense samples, north shore end to south shore end) -----
	TArray<FVector> WallLine;
	{
		const float TE = WallEndT();
		for (int32 I = 0; I <= 2400; ++I)
		{
			const FVector2D C = WallCellAt(FMath::Lerp(-TE, TE, I / 2400.f));
			WallLine.Add(CellF(C.X, C.Y));
		}
	}
	auto WallDist = [&WallLine](const FVector& P)
	{
		float D = BIG_NUMBER;
		for (int32 I = 0; I < WallLine.Num(); I += 8) { D = FMath::Min(D, (float)FVector::Dist2D(P, WallLine[I])); }
		return D;
	};

	// ----- forest (west) and the framing trees -----
	for (int32 X = -6; X < GW + 2; ++X)
	{
		for (int32 Y = -4; Y < GH + 4; ++Y)
		{
			const bool bForest = X <= 6;
			// framing trees outside the palisade (not on the shore side nor in front of the gate), grove trees inside it
			const float Shape = WallShape(X + 0.5f, Y + 0.5f);
			const bool bFrame = !bForest && X >= 8 && X <= 27 && Shape > 1.f && (X + 0.5f < WallCX || !InGrid(X, Y))
				&& !(X <= 9 && Y >= 9 && Y <= 14);
			const bool bGroveTree = X >= 10 && X <= 20 && Y >= 21 && Y <= GH && !(X >= 13 && X <= 17 && Y <= 22) && Shape < 1.f;
			if (!bForest && !bFrame && !bGroveTree) { continue; }
			if (X > 27) { UE_LOG(LogSaga, Warning, TEXT("[Saga] tree loop: X=%d Y=%d forest=%d frame=%d grove=%d"), X, Y, bForest, bFrame, bGroveTree); continue; }
			if (bForest && (Y == 11 || Y == 12)) { continue; } // forest road
			if (!bForest && WallDist(CellCenter(X, Y)) < 170.f) { continue; } // keep the palisade clear
			if (!bForest && (Y == -1 || Y == GH)) { continue; } // north/south log walls (placed in Map_Saga)
			const float Density = bForest ? (X >= 5 ? 0.42f : 0.62f) : bGroveTree ? 0.4f : 0.5f;
			if (Rand01(X, Y, 7) > Density)
			{
				if ((bForest || bGroveTree) && Rand01(X, Y, 9) < 0.22f && InGrid(X, Y))
				{
					ATSCPrimActor* M = SpawnPrim(CellCenter(X, Y), bForest ? TSCRegion::Forest : TSCRegion::Grove);
					BuildMushrooms(M, X * 31 + Y);
					M->Refresh();
				}
				continue;
			}
			const FVector Jitter((Rand01(X, Y, 4) - 0.5f) * 70.f, (Rand01(X, Y, 5) - 0.5f) * 70.f, 0.f);
			ATSCPrimActor* T = SpawnPrim(CellCenter(X, Y) + Jitter, bForest ? TSCRegion::Forest : bGroveTree ? TSCRegion::Grove : TSCRegion::None);
			BuildTree(T, X * 57 + Y * 13, true);
			T->Refresh();
			SetBlocked(X, Y, true);
			if (InGrid(X, Y) || X < 19) { Nav.SetTree(X, Y, T->GetActorLocation()); } // the east margin stays fully blocked
			if ((bForest || bGroveTree) && InGrid(X, Y))
			{
				FTSCTimberTree& Tree = TimberTrees.AddDefaulted_GetRef();
				Tree.Tree = T;
				Tree.Cell = FIntPoint(X, Y);
				Tree.Base = T->GetActorLocation();
				Tree.Rotation = T->GetActorQuat();
			}
		}
	}
	// glowing mushrooms in the stag grove (foraged for food)
	const FVector2D GroveShrooms[7] = { {12.5f, 20.4f}, {13.6f, 21.3f}, {14.2f, 19.6f}, {16.5f, 20.6f}, {17.5f, 21.4f}, {18.6f, 20.2f}, {11.4f, 19.5f} };
	for (int32 I = 0; I < 7; ++I)
	{
		ATSCPrimActor* M = SpawnPrim(CellF(GroveShrooms[I].X, GroveShrooms[I].Y), TSCRegion::Grove);
		BuildMushrooms(M, 100 + I);
		M->Refresh();
	}

	// ----- palisade and gate (west) -----
	// One rounded log wall along WallLine, broken by the gate: straight segments of about 3 m, each an SM_WoodenFence
	// section with the Ashen stone footing. The cells under the wall block the paths.
	{
		const float GateY0 = CellCorner(GateX, GateY).Y, GateY1 = CellCorner(GateX, GateY + 2).Y;
		TArray<FVector> Runs[2];
		for (const FVector& P : WallLine)
		{
			if (P.Y > GateY0 && P.Y < GateY1) { continue; }
			Runs[P.Y <= GateY0 ? 0 : 1].Add(P);
			for (const FVector& O : { FVector::ZeroVector, FVector(30.f, 0.f, 0.f), FVector(-30.f, 0.f, 0.f), FVector(0.f, 30.f, 0.f), FVector(0.f, -30.f, 0.f) })
			{
				const FIntPoint C = WorldToCell(P + O);
				SetBlocked(C.X, C.Y, true);
			}
		}
		Runs[0].Add(FVector(WallX(), GateY0, 0.f));
		Runs[1].Insert(FVector(WallX(), GateY1, 0.f), 0);
		int32 Seg = 0;
		for (const TArray<FVector>& Run : Runs)
		{
			float Len = 0.f;
			for (int32 I = 1; I < Run.Num(); ++I) { Len += FVector::Dist2D(Run[I - 1], Run[I]); }
			const int32 N = FMath::Max(1, FMath::RoundToInt(Len / 295.f));
			const float Step = Len / N;
			// cut the run into N straight segments of equal length
			TArray<FVector> Knots = { Run[0] };
			float Acc = 0.f, Next = Step;
			for (int32 I = 1; I < Run.Num(); ++I)
			{
				const float D = FVector::Dist2D(Run[I - 1], Run[I]);
				while (Knots.Num() < N && D > 0.f && Acc + D >= Next)
				{
					Knots.Add(FMath::Lerp(Run[I - 1], Run[I], (Next - Acc) / D));
					Next += Step;
				}
				Acc += D;
			}
			Knots.Add(Run.Last());
			for (int32 I = 0; I + 1 < Knots.Num(); ++I)
			{
				const FVector Dir = Knots[I + 1] - Knots[I];
				ATSCPrimActor* P = SpawnPrim(Knots[I]);
				P->SetActorRotation(FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)) - 90.f, 0.f));
				BuildPalisadeSegment(P, Dir.Size2D(), Seg++);
				P->Refresh();
				WallSegs.Add(P);
			}
		}
		// the gate and the road in front of it stay open
		SetGateCells(bGateClosed);
		SetBlocked(GateX - 1, GateY, false);
		SetBlocked(GateX - 1, GateY + 1, false);
	}
	Gate = SpawnPrim(CellCorner(GateX, GateY));
	BuildGate(Gate, GateLeaves);
	Gate->Refresh();
	ProDressing.Add(Gate);

	// the spruce by the gate: a separate stump stands in for its Ashen version
	EventTree = SpawnPrim(CellF(SprucePos.X, SprucePos.Y), TSCRegion::Forest);
	BuildTree(EventTree, 4242, false);
	EventTree->SetActorRotation(FRotator::ZeroRotator);
	EventTree->SetActorScale3D(FVector::OneVector);
	EventTree->Refresh();
	{
		// The old giant of the forest, big enough to bar the gate: about 9.5 m tall (a pine variant can be as short as ~3 m at
		// scale 1), with a crown to match. Sized from the mesh so a different variant does not shrink it again.
		const float H = FMath::Max(EventTree->GetComponentsBoundingBox(true).GetSize().Z, 100.f);
		const float Z = 950.f / H;
		EventTree->SetActorScale3D(FVector(Z * 0.8f, Z * 0.8f, Z));
		SpruceFullScale = EventTree->GetActorScale3D();
	}
	{
		ATSCPrimActor* Stump = SpawnPrim(CellF(SprucePos.X, SprucePos.Y), TSCRegion::Forest);
		BuildStump(Stump);
		Stump->SetActorScale3D(FVector(1.6f)); // the burnt stump of that giant
		Stump->Refresh();
	}
	SetBlocked(TreeX, TreeY, true);
	Nav.SetTree(TreeX, TreeY, CellCenter(TreeX, TreeY)); // keeps the whole cell blocked; the trunk itself stands in the wall cell beside it

	// ----- constructions -----
	TArray<FBox2D> GroundExclusions;
	auto Place = [&](int32 X, int32 Y, int32 W, int32 H, int32 Region = 0)
	{
		ATSCPrimActor* A = SpawnPrim(CellCorner(X, Y), Region);
		BlockRect(X, Y, W, H);
		GroundExclusions.Emplace(FVector2D(CellCorner(X, Y)), FVector2D(CellCorner(X + W, Y + H)));
		A->bStepParts = true;  // lets the prologue raise it tile by tile; at full progress nothing changes
		ProBuildings.Add(A);
		return A;
	};
	{
		ATSCPrimActor* A = Place(14, 10, 6, 3); BuildLonghouse(A); A->Refresh();
		A = Place(10, 4, 3, 2); BuildSawmill(A); A->Refresh();
		A = Place(22, 9, 2, 2); BuildHealer(A); A->Refresh();
		const FIntPoint Houses[4] = { {10, 15}, {12, 17}, {21, 5}, {23, 15} };
		for (int32 I = 0; I < 4; ++I)
		{
			A = Place(Houses[I].X, Houses[I].Y, 2, 2);
			BuildHouse(A, I + 1);
			A->Refresh();
		}
		for (const FVector2D& L : { FVector2D(13.5f, 4.5f), FVector2D(10.2f, 6.4f) })
		{
			A = SpawnPrim(CellF(L.X, L.Y));
			BuildLogPile(A);
			A->Refresh();
			ProDressing.Add(A);
			const FIntPoint C = WorldToCell(CellF(L.X, L.Y));
			SetBlocked(C.X, C.Y, true);
		}
	}
	Garden = SpawnPrim(CellCorner(16, 15), TSCRegion::Garden);
	BuildGarden(Garden, 4, 3, CropParts);
	Garden->Refresh();
	ProDressing.Add(Garden);

	DockActor = SpawnPrim(CellCorner(26, 11), TSCRegion::Dock);
	BuildDock(DockActor, 6, 2, CrateParts);
	DockActor->Refresh();
	ProDressing.Add(DockActor);
	{
		ShipMooring = CellF(30.6f, 14.4f) + FVector(0.f, 0.f, -6.f);
		Longship = SpawnPrim(ShipMooring, TSCRegion::Dock);
		LongshipSail = BuildLongship(Longship);
		Longship->Refresh();
		SetSailHoisted(0.f); // moored: the sail is down (the prologue hoists it for the voyage)
	}
	// water is blocked except the dock and the shallows
	for (int32 X = 30; X < GW; ++X)
	{
		for (int32 Y = 0; Y < GH; ++Y) { if (!(Y == 11 || Y == 12) || X >= 32) { SetBlocked(X, Y, true); } }
	}
	RaiderBoat = SpawnPrim(CellF(40.f, 12.f) + FVector(0.f, 0.f, -4.f));
	BuildLongship(RaiderBoat, 620.f, 180.f); // the same drakkar as ours, bow to the west: it sails in from the east
	RaiderBoat->Refresh();
	RaiderBoat->SetActorHiddenInGame(true);

	// the sea owns the Z, pitch and roll of everything that floats
	Ocean->AddFloater(Longship, 300.f, 55.f, -6.f, 420.f);
	Ocean->AddFloater(RaiderBoat, 190.f, 45.f, -4.f, 300.f);
	{
		const FVector2D BarrelCells[3] = { FVector2D(29.3f, 9.6f), FVector2D(30.4f, 15.9f), FVector2D(32.2f, 10.4f) };
		const FVector2D WoodCells[4] = { FVector2D(29.8f, 10.3f), FVector2D(31.0f, 13.4f), FVector2D(32.6f, 12.6f), FVector2D(30.2f, 8.4f) };
		for (int32 I = 0; I < 3; ++I)
		{
			ATSCPrimActor* A = SpawnPrim(CellF(BarrelCells[I].X, BarrelCells[I].Y), TSCRegion::Dock);
			BuildBarrel(A);
			A->Refresh();
			A->SetActorRotation(FRotator(0.f, Rand01(I, 7) * 360.f, 0.f));
			Flotsam.Add(A);
			ProDressing.Add(A);
			Ocean->AddFloater(A, 40.f, 22.f, 0.f, 0.f, true);
		}
		for (int32 I = 0; I < 4; ++I)
		{
			ATSCPrimActor* A = SpawnPrim(CellF(WoodCells[I].X, WoodCells[I].Y), TSCRegion::Dock);
			BuildDriftwood(A, I);
			A->Refresh();
			A->SetActorRotation(FRotator(0.f, Rand01(I, 7) * 360.f, 0.f));
			Flotsam.Add(A);
			Ocean->AddFloater(A, 80.f, 14.f, 0.f, 0.f, true);
		}
	}

	// The cattle pen: a rectangle of village fence on the free ground south of the plots, clear of the grove and the wall.
	// The livestock graze inside it (see SpawnAllWildlife). Cells just outside the fence are blocked, so nobody walks through it.
	{
		const int32 PenX = 21, PenY = 21, PenW = 4, PenH = 3;
		ATSCPrimActor* Pen = SpawnPrim(CellCorner(PenX, PenY));
		BuildCattlePen(Pen, PenW * CELL, PenH * CELL);
		Pen->Refresh();
		ProDressing.Add(Pen);
		for (int32 I = -1; I <= PenW; ++I) { SetBlocked(PenX + I, PenY - 1, true); SetBlocked(PenX + I, PenY + PenH, true); }
		for (int32 J = 0; J < PenH; ++J) { SetBlocked(PenX - 1, PenY + J, true); SetBlocked(PenX + PenW, PenY + J, true); }
	}

	const FIntPoint Rocks[8] = { {26, 8}, {27, 16}, {26, 19}, {27, 4}, {26, 2}, {27, 21}, {25, 22}, {24, 1} };
	for (int32 I = 0; I < 8; ++I)
	{
		ATSCPrimActor* R = SpawnPrim(CellCenter(Rocks[I].X, Rocks[I].Y) + FVector(0.f, 0.f, 4.f));
		BuildRock(R, I + 3);
		R->SetActorScale3D(FVector(1.3f + 0.4f * (I % 3)));
		R->Refresh();
		SetBlocked(Rocks[I].X, Rocks[I].Y, true);
	}
	// Torch Blueprints are placed in Map_Saga and collected by CollectLevelProps.

	// ----- construction plots -----
	Plots.SetNum(2);
	Plots[0].Cell = FIntPoint(10, 8);
	Plots[1].Cell = FIntPoint(21, 18);
	for (FTSCPlot& P : Plots)
	{
		GroundExclusions.Emplace(FVector2D(CellCorner(P.Cell.X, P.Cell.Y)),
			FVector2D(CellCorner(P.Cell.X + 3, P.Cell.Y + 2)));
		P.Marker = SpawnPrim(CellCorner(P.Cell.X, P.Cell.Y));
		BuildPlotMarker(P.Marker, 3, 2);
		P.Marker->Refresh();
		P.Marker->SetActorHiddenInGame(true);
	}
	Plots[0].bAvailable = true;
	Plots[0].Marker->SetActorHiddenInGame(false);
	ProDressing.Add(Plots[0].Marker);

	GroundExclusions.Emplace(FVector2D(CellCorner(GardenRect.Min.X, GardenRect.Min.Y)),
		FVector2D(CellCorner(GardenRect.Max.X, GardenRect.Max.Y)));
	// The north and south log walls are placed in the level. Their clearances are
	// also used by the framing trees above.
	Terrain->BuildForestFloor(Ocean,
		[&](const FVector2D& P, float Radius)
		{
			if (WallDist(FVector(P, 0.f)) < Radius + 95.f) { return false; }
			const FIntPoint Min = WorldToCell(FVector(P - FVector2D(Radius), 0.f));
			const FIntPoint Max = WorldToCell(FVector(P + FVector2D(Radius), 0.f));
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				for (int32 X = Min.X; X <= Max.X; ++X)
				{
					if (InGrid(X, Y) && IsBlocked(X, Y)) { return false; }
					if (X >= GateX && X < WallEndX && (Y == -1 || Y == GH)) { return false; }
				}
			}
			for (const FBox2D& Box : GroundExclusions)
			{
				if (Box.ComputeSquaredDistanceToPoint(P) < FMath::Square(Radius)) { return false; }
			}
			return true;
		},
		[](const FVector2D& P)
		{
			const float X = P.X / CELL + GW / 2;
			return X < WallEndX && WallShape(X, P.Y / CELL + GH / 2) < 1.f;
		});

	BakeNavProps();
	// ----- people -----
	Hunter = SpawnUnit(ETSCRole::Hunter, TEXT("Hakon the Hunter"), CellF(9.6f, 12.f));
	Woodcutter = SpawnUnit(ETSCRole::Woodcutter, TEXT("Ulf the Woodcutter"), CellF(11.5f, 6.6f));
	Healer = SpawnUnit(ETSCRole::Healer, TEXT("Sigrun the Healer"), CellF(22.6f, 11.6f));
	Shield = SpawnUnit(ETSCRole::Shield, TEXT("Torvald Shield-Bearer"), CellF(20.5f, 12.f));
	for (int32 I = 0; I < 10; ++I)
	{
		ATSCUnit* W = SpawnUnit(ETSCRole::Worker, FString(), CellF(14.5f + (I % 5) * 1.1f, 13.5f + (I / 5) * 0.6f));
		Workers.Add(W);
	}
	NumWood = 4;
	NumFood = 4;
	NumHomeUnits = Units.Num();
	for (ATSCUnit* U : Units) { UnitHome.Add(U->GetActorLocation()); }
	SpawnAllWildlife();
	ResetVillagers();
}

void ATSCGameMode::ResetVillagers()
{
	for (int32 I = 0; I < NumHomeUnits && I < Units.Num(); ++I)
	{
		if (ATSCUnit* U = Units[I])
		{
			U->Teleport(UnitHome[I]);
			U->SetActorRotation(FRotator::ZeroRotator);
			U->SetActorHiddenInGame(false);
		}
	}
	OrderPatrolRoute(Hunter);
	Woodcutter->OrderWork(CellF(7.5f, 9.5f), CellF(11.5f, 6.5f));
	OrderPatrolRoute(Healer);
	OrderPatrolRoute(Shield);
	UpdateJobs();
}

void ATSCGameMode::OrderPatrolRoute(ATSCUnit* U)
{
	if (!U || !U->IsAlive()) { return; }
	// Cell coordinates inside the palisade. Each loop keeps at least ~1.5 cells from the other two, so the heroes never meet:
	//  Hakon  - the outer ring along the palisade (gate road, north shore, dock, south), the longest walk
	//  Torvald - the inner ring round the longhouse
	//  Sigrun - the herb round through the garden, south of the longhouse
	static const FVector2D HakonCells[] = {
		{ 10.8f, 8.4f }, { 14.6f, 7.6f }, { 14.6f, 2.9f }, { 20.0f, 2.9f }, { 25.0f, 3.9f }, { 26.0f, 8.0f }, { 26.0f, 14.0f },
		{ 25.8f, 19.0f }, { 22.0f, 21.8f }, { 15.0f, 21.8f }, { 10.6f, 20.4f }, { 9.3f, 15.5f }, { 10.8f, 12.0f } };
	static const FVector2D TorvaldCells[] = { { 12.9f, 9.0f }, { 20.9f, 9.0f }, { 20.9f, 13.9f }, { 12.9f, 13.9f } };
	static const FVector2D SigrunCells[] = { { 14.8f, 15.6f }, { 20.8f, 15.6f }, { 21.0f, 19.6f }, { 14.8f, 19.6f } };

	const FVector2D* Cells = nullptr;
	int32 Num = 0;
	switch (U->Role)
	{
	case ETSCRole::Hunter: Cells = HakonCells; Num = UE_ARRAY_COUNT(HakonCells); break;
	case ETSCRole::Shield: Cells = TorvaldCells; Num = UE_ARRAY_COUNT(TorvaldCells); break;
	case ETSCRole::Healer: Cells = SigrunCells; Num = UE_ARRAY_COUNT(SigrunCells); break;
	default: return;
	}
	TArray<FVector> Route;
	for (int32 I = 0; I < Num; ++I) { Route.Add(CellF(Cells[I].X, Cells[I].Y)); }
	U->OrderPatrol(Route);
}

void ATSCGameMode::BeginPlay()
{
	Super::BeginPlay();
	EditsLeft = MaxEdits;
	DefineScenes();
	CollectLevelProps();  // before anything is spawned at runtime
	BuildAtmosphere();
	BuildWorld();
	BuildPrologueExtras();
	float W, F;
	ComputeRates(W, F);
	WoodRate = FMath::RoundToInt(W);
	FoodRate = FMath::RoundToInt(F);
	PlayMusic(MenuMusic);
	if (CVarPrologue.GetValueOnGameThread() != 0 && !FParse::Param(FCommandLine::Get(), TEXT("NoPrologue")))
	{
		StartPrologue();
	}
}

// =====================================================================================================================
// Prologue: the settlers land, clear the forest and build; the forest notices. Plays in a loop behind the title.
// =====================================================================================================================

/** A level actor that is a piece of wooden fence (static mesh or prop blueprint with "Fence" in its mesh / class path). */
static bool IsFenceProp(const AActor* A)
{
	if (A->GetClass()->GetPathName().Contains(TEXT("Fence"))) { return true; }
	if (const AStaticMeshActor* SM = Cast<AStaticMeshActor>(A))
	{
		const UStaticMesh* Mesh = SM->GetStaticMeshComponent() ? SM->GetStaticMeshComponent()->GetStaticMesh() : nullptr;
		return Mesh && Mesh->GetPathName().Contains(TEXT("Fence"));
	}
	return false;
}

void ATSCGameMode::CollectLevelProps()
{
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* A = *It;
		if (!A || A->IsHidden()) { continue; }
		const AStaticMeshActor* SM = Cast<AStaticMeshActor>(A);
		const bool bPropBlueprint = A->GetClass()->GetPathName().StartsWith(TEXT("/Game/Props/"));
		if (!SM && !bPropBlueprint) { continue; }
		if (SM)
		{
			const UStaticMesh* Mesh = SM->GetStaticMeshComponent() ? SM->GetStaticMeshComponent()->GetStaticMesh() : nullptr;
			if (!Mesh || Mesh->GetPathName().Contains(TEXT("Mushroom"))) { continue; }  // mushrooms belong to the forest
		}
		NavPropActors.Add(A);
		const FVector P = A->GetActorLocation();
		if (SM && SM->GetStaticMeshComponent() && SM->GetStaticMeshComponent()->GetStaticMesh()
			&& SM->GetStaticMeshComponent()->GetStaticMesh()->GetName().Contains(TEXT("SM_Altar")))
		{
			AltarPos = P; // the one stone altar: prayers and blessings belong to it
			bAltarFound = true;
		}
		const float Cx = P.X / CELL + GW / 2, Cy = P.Y / CELL + GH / 2;
		if (Cx < 27.5f && WallShape(Cx, Cy) < 1.08f) { ProLevelProps.Add(A); }
		else if (Cx < WallCX && IsFenceProp(A)) { ProRoadFence.Add(A); } // the double fence running out of the gate along the road
	}
	ProLevelProps.Sort([](const AActor& A, const AActor& B) { return A.GetActorLocation().X > B.GetActorLocation().X; });
	// The road fence is built last, outwards from the gate; remember each piece's own scale (the level may have resized it).
	const FVector GatePos = CellCorner(GateX, GateY + 1);
	ProRoadFence.Sort([GatePos](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), GatePos) < FVector::DistSquared2D(B.GetActorLocation(), GatePos);
	});
	for (const AActor* F : ProRoadFence) { ProRoadFenceScale.Add(F->GetActorScale3D()); }
	if (!bAltarFound)
	{
		AltarPos = CellCenter(16, 2);
		UE_LOG(LogSaga, Warning, TEXT("[Saga] No stone altar (SM_Altar) found in the level, praying at a default spot"));
	}
	UE_LOG(LogSaga, Display, TEXT("[Saga] Prologue: %d level props inside the wall, %d road fence pieces"), ProLevelProps.Num(), ProRoadFence.Num());
}

void ATSCGameMode::BakeNavProps()
{
	// Points the scene scripts and orders walk to: a prop must never cut them off.
	const FVector KeyPoints[] = {
		HallDoor(), CellF(UlfChop.X, UlfChop.Y), CellF(14.4f, 20.3f), CellF(18.f, 14.6f), CellF(17.f, 14.4f), CellF(9.9f, 12.f),
		CellF(7.5f, 12.f), CellF(3.5f, 12.f), CellF(0.5f, 12.f), CellF(26.4f, 12.f), CellF(31.5f, 12.f)
	};
	constexpr int32 NumKeys = UE_ARRAY_COUNT(KeyPoints);
	auto KeyFine = [this, &KeyPoints](const TArray<float>& Cost, int32 K)
	{
		return Nav.CostAt(Cost, KeyPoints[K]) < MAX_flt && !Nav.IsBlockedAt(KeyPoints[K], TSCNavLayer::People);
	};
	TArray<float> Cost;
	Nav.Flood(HallDoor(), TSCNavLayer::People, Cost);
	bool bBaseline[NumKeys];
	for (int32 K = 0; K < NumKeys; ++K) { bBaseline[K] = KeyFine(Cost, K); }

	int32 NumActors = 0, NumIgnored = 0;
	for (AActor* A : NavPropActors)
	{
		if (!A) { continue; }
		TArray<UStaticMeshComponent*> Comps;
		A->GetComponents<UStaticMeshComponent>(Comps);
		// garden crops are walked through
		bool bCrop = false;
		for (const UStaticMeshComponent* C : Comps)
		{
			const UStaticMesh* Mesh = C ? C->GetStaticMesh() : nullptr;
			if (Mesh && (Mesh->GetPathName().Contains(TEXT("VegetablePlant")) || Mesh->GetPathName().Contains(TEXT("VegetableSprout")))) { bCrop = true; break; }
		}
		if (bCrop) { continue; }

		TArray<int32> Added;
		if (!Cast<AStaticMeshActor>(A) && !IsFenceProp(A))
		{
			// torches, banners, braziers: poles with wide overhead bounds; only the foot blocks
			const float Radius = A->GetClass()->GetPathName().Contains(TEXT("Brazier")) ? 40.f : 30.f;
			Added.Add(Nav.AddPropDisc(FVector2D(A->GetActorLocation()), Radius));
		}
		else
		{
			for (const UStaticMeshComponent* C : Comps)
			{
				const UStaticMesh* Mesh = C ? C->GetStaticMesh() : nullptr;
				if (!Mesh || !C->IsVisible()) { continue; }
				if (C->Bounds.Origin.Z - C->Bounds.BoxExtent.Z > GroundReliefAt(C->Bounds.Origin) + 140.f) { continue; } // hangs overhead
				const FBox LB = Mesh->GetBoundingBox();
				const FTransform T = C->GetComponentTransform();
				const FVector Center = T.TransformPosition(LB.GetCenter());
				FVector2D AxisX(1.f, 0.f);
				FVector2D Half;
				if (FMath::Abs(T.GetUnitAxis(EAxis::Z).Z) < 0.8f)
				{
					// tilted or lying prop: its world bounds
					Half = FVector2D(C->Bounds.BoxExtent.X, C->Bounds.BoxExtent.Y);
				}
				else
				{
					const FVector WX = T.GetUnitAxis(EAxis::X);
					AxisX = FVector2D(WX.X, WX.Y).GetSafeNormal();
					if (AxisX.IsNearlyZero()) { AxisX = FVector2D(1.f, 0.f); }
					const FVector Scale = T.GetScale3D();
					Half = FVector2D(LB.GetExtent().X * FMath::Abs(Scale.X), LB.GetExtent().Y * FMath::Abs(Scale.Y));
				}
				Half.X = FMath::Max(Half.X + 10.f, 15.f);
				Half.Y = FMath::Max(Half.Y + 10.f, 15.f);
				Added.Add(Nav.AddPropBox(FVector2D(Center), AxisX, Half));
			}
		}
		if (Added.IsEmpty()) { continue; }
		++NumActors;

		// guard: a prop that cuts off a key point is left out
		Nav.Flood(HallDoor(), TSCNavLayer::People, Cost);
		bool bBreaks = false;
		for (int32 K = 0; K < NumKeys && !bBreaks; ++K) { bBreaks = bBaseline[K] && !KeyFine(Cost, K); }
		if (bBreaks)
		{
			for (const int32 Idx : Added) { Nav.SetPropEnabled(Idx, false); }
			++NumIgnored;
			UE_LOG(LogSaga, Warning, TEXT("[Nav] prop %s ignored: it would block a key point"), *A->GetName());
		}
	}
	int32 NumObstacles = 0;
	for (int32 I = 0; I < Nav.NumProps(); ++I) { if (Nav.IsPropEnabled(I)) { ++NumObstacles; } }
	++GridVersion;
	UE_LOG(LogSaga, Display, TEXT("[Nav] props: %d obstacles from %d level actors, %d actors ignored"), NumObstacles, NumActors, NumIgnored);
}

void ATSCGameMode::BuildPrologueExtras()
{
	// the forest that stood where the village is: never blocks a cell, only shown during the prologue
	for (int32 X = 9; X <= 26; ++X)
	{
		for (int32 Y = 0; Y < 20; ++Y)
		{
			if (WallShape(X + 0.5f, Y + 0.5f) > 0.9f || Rand01(X, Y, 23) > 0.36f) { continue; }
			const FVector Jitter((Rand01(X, Y, 24) - 0.5f) * 90.f, (Rand01(X, Y, 25) - 0.5f) * 90.f, 0.f);
			ATSCPrimActor* T = SpawnPrim(CellCenter(X, Y) + Jitter);
			BuildTree(T, X * 41 + Y * 7, false);
			T->Refresh();
			T->SetActorHiddenInGame(true);
			ProTrees.Add(T);
			// felled in a wave from the shore to the west
			ProTreeFell.Add(ProTLand + 1.2f + (26 - X) / 17.f * 9.f + Rand01(X, Y, 29) * 1.6f);
			ProTreeYaw.Add(Rand01(X, Y, 30) * 2.f * PI);
		}
	}
	// the watchers at the tree line: not in Units, so nobody targets them and they fight nobody
	for (int32 I = 0; I < 6; ++I)
	{
		ATSCUnit* W = SpawnUnit(ETSCRole::Creature, FString(), CellF(3.f, 10.f + I));
		Units.Remove(W);
		W->SetActorHiddenInGame(true);
		ProWatchers.Add(W);
	}
	const FVector GatePos = CellCorner(GateX, GateY + 1);
	WallSegs.Sort([GatePos](const ATSCPrimActor& A, const ATSCPrimActor& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), GatePos) < FVector::DistSquared2D(B.GetActorLocation(), GatePos);
	});
}

void ATSCGameMode::SetSailHoisted(float Alpha)
{
	if (LongshipSail) { LongshipSail->SetRelativeScale3D(FVector(1.f, 1.f, FMath::Clamp(Alpha, 0.03f, 1.f))); }
}

void ATSCGameMode::SetCameraView(const FVector& Loc, float Arm)
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (ATSCColonyCameraPawn* Cam = Cast<ATSCColonyCameraPawn>(PC->GetPawn())) { Cam->SetView(Loc, Arm); }
	}
}

void ATSCGameMode::StartPrologue()
{
	bPrologue = true;
	if (Terrain)
	{
		Terrain->SetTrailsVisible(false);
		Terrain->SetClearingStumpsVisible(false);
	}
	ProTime = 0.f;
	ProLanded = 0;
	ProStage = 0;
	bProLandingPlayed = false;
	for (ATSCUnit* A : FarmAnimals) { if (A) { A->SetActorHiddenInGame(true); } } // they come once the village is built
	if (HunterDog) { HunterDog->SetActorHiddenInGame(true); }
	ProDusk = 0.f;
	ProFade = 1.f;
	ProCaption.Empty();
	ProCaptionAlpha = 0.f;

	Longship->SetActorLocation(ShipMooring + FVector(ProShipRun, 0.f, 0.f));
	SetSailHoisted(1.f); // under sail on the way in
	for (ATSCPrimActor* S : WallSegs) { S->SetActorHiddenInGame(true); }
	for (ATSCPrimActor* D : ProDressing) { D->SetActorHiddenInGame(true); }
	for (AActor* A : ProLevelProps) { if (A) { A->SetActorHiddenInGame(true); } }
	for (int32 I = 0; I < ProRoadFence.Num(); ++I)
	{
		if (ProRoadFence[I]) { ProRoadFence[I]->SetActorScale3D(ProRoadFenceScale[I]); ProRoadFence[I]->SetActorHiddenInGame(true); }
	}
	for (ATSCPrimActor* B : ProBuildings) { B->SetBuildProgress(0.f); }
	for (ATSCPrimActor* T : ProTrees)
	{
		T->SetActorRotation(FRotator::ZeroRotator);
		T->SetActorScale3D(FVector::OneVector);
		T->SetActorHiddenInGame(false);
	}
	for (ATSCUnit* W : ProWatchers) { W->SetActorHiddenInGame(true); }
	for (int32 I = 0; I < NumHomeUnits && I < Units.Num(); ++I)
	{
		Units[I]->Teleport(ShipMooring);
		Units[I]->SetActorHiddenInGame(true);
	}
	Nav.SetPropsActive(false); // the level props are hidden until the village is built
	++GridVersion;
	ProCamLoc = CellF(25.5f, 13.f);
	ProCamArm = 10500.f;
	SetCameraView(ProCamLoc, ProCamArm);
	PlayMusic(MenuMusic);
}

void ATSCGameMode::EndPrologue()
{
	if (!bPrologue) { return; }
	bPrologue = false;
	if (Terrain)
	{
		Terrain->SetTrailsVisible(true);
		Terrain->SetClearingStumpsVisible(true);
	}
	ProFade = 0.f;
	ProDusk = 0.f;
	ProCaptionAlpha = 0.f;

	Longship->SetActorLocation(ShipMooring);
	SetSailHoisted(0.f);
	// the farm animals stay for good, Hakon's dog beside him
	for (ATSCUnit* A : FarmAnimals) { if (A) { A->SetActorScale3D(FVector::OneVector); A->SetActorHiddenInGame(false); } }
	if (HunterDog) { HunterDog->SetActorScale3D(FVector::OneVector); HunterDog->SetActorHiddenInGame(false); }
	for (ATSCPrimActor* S : WallSegs)
	{
		S->SetActorScale3D(FVector::OneVector);
		S->SetActorHiddenInGame(false);
	}
	for (ATSCPrimActor* D : ProDressing) { D->SetActorHiddenInGame(false); }
	for (AActor* A : ProLevelProps) { if (A) { A->SetActorHiddenInGame(false); } }
	for (int32 I = 0; I < ProRoadFence.Num(); ++I)
	{
		if (ProRoadFence[I]) { ProRoadFence[I]->SetActorScale3D(ProRoadFenceScale[I]); ProRoadFence[I]->SetActorHiddenInGame(false); }
	}
	for (ATSCPrimActor* B : ProBuildings) { B->SetBuildProgress(1.f); }
	for (ATSCPrimActor* T : ProTrees) { T->SetActorHiddenInGame(true); }
	for (ATSCUnit* W : ProWatchers) { W->SetActorHiddenInGame(true); }
	ResetVillagers();
	Nav.SetPropsActive(true);
	++GridVersion;  // everyone plans a fresh path from home
	SetCameraView(CellF(17.f, 12.f), DefaultCamArm);
	PlayMusic(MenuMusic);
}

void ATSCGameMode::SimPrologue(float Dt)
{
	ProTime += Dt;
	const float T = ProTime;
	if (Terrain && T >= ProTClear + 1.5f)
	{
		Terrain->SetTrailsVisible(true);
		Terrain->SetClearingStumpsVisible(true);
	}
	if (T >= ProTEnd)
	{
		StartPrologue();
		return;
	}
	ProFade = FMath::Clamp(FMath::Max(1.f - T / 1.2f, (T - (ProTEnd - 1.4f)) / 1.2f), 0.f, 1.f);

	// ----- captions: one line per beat -----
	{
		static const TCHAR* Lines[4] = {
			TEXT("The Cutsalots came on the grey tide, one ship and forty souls."),
			TEXT("The forest was old. They did not ask its leave."),
			TEXT("Timber became wall, and wall became home."),
			TEXT("But every stump was counted. The forest remembers.") };
		const float Edges[5] = { 0.f, ProTLand, ProTClear, ProTRaise, ProTWake };
		ProCaptionAlpha = 0.f;
		for (int32 I = 0; I < 4; ++I)
		{
			if (T >= Edges[I] && T < Edges[I + 1])
			{
				ProCaption = Lines[I];
				ProCaptionAlpha = FMath::Clamp(FMath::Min(T - Edges[I] - 0.7f, Edges[I + 1] - 0.3f - T) / 0.8f, 0.f, 1.f);
			}
		}
	}

	// ----- the ship comes in -----
	{
		const float A = FMath::Clamp(T / (ProTLand - 0.5f), 0.f, 1.f);
		const float Left = FMath::Pow(1.f - A, 3.f);
		Longship->SetActorLocation(ShipMooring + FVector(ProShipRun * Left, 0.f, 0.f));
		// The ship stops at ProTLand - 0.5; then the sail comes down, and only after that the crew goes ashore.
		const float Lowered = FMath::Clamp((T - (ProTLand - 0.5f)) / ProSailDrop, 0.f, 1.f);
		SetSailHoisted(1.f - FMath::SmoothStep(0.f, 1.f, Lowered));
		if (T >= ProTLand - 0.6f && !bProLandingPlayed)
		{
			bProLandingPlayed = true;
			if (Ocean) { Ocean->PlayLanding(FVector2D(Longship->GetActorLocation()) - FVector2D(300.f, 0.f)); }
		}
	}

	// ----- people: ashore one by one, then to the trees, then to the building sites, then home -----
	while (ProLanded < NumHomeUnits && T > ProTLand - 0.5f + ProSailDrop + 0.3f + ProLanded * 0.45f)
	{
		const int32 I = ProLanded++;
		ATSCUnit* U = Units[I];
		U->Teleport(CellF(29.4f, 11.3f + (I % 2) * 0.7f));
		U->SetActorHiddenInGame(false);
		U->OrderWork(CellF(23.5f - (I % 7) * 2.f, 4.5f + ((I * 5) % 15)), CellF(25.2f, 9.5f + (I % 5)));
	}
	if (ProStage == 0 && T >= ProTClear)
	{
		ProStage = 1;
		static const FVector2D Sites[14] = { {13.5f, 13.4f}, {20.4f, 13.4f}, {11.6f, 6.4f}, {21.5f, 11.3f}, {11.f, 17.3f}, {13.f, 19.3f}, {22.f, 7.3f},
			{24.f, 17.3f}, {9.6f, 10.4f}, {9.6f, 13.5f}, {12.5f, 8.5f}, {16.5f, 9.5f}, {18.5f, 13.5f}, {10.5f, 13.f} };
		for (int32 I = 0; I < ProLanded; ++I)
		{
			Units[I]->OrderWork(CellF(Sites[I % 14].X, Sites[I % 14].Y), CellF(13.2f + (I % 3) * 0.6f, 5.6f));
		}
	}
	if (ProStage == 1 && T >= ProTRaise)
	{
		ProStage = 2;
		for (int32 I = 0; I < ProLanded; ++I) { Units[I]->OrderHold(UnitHome[I]); }
		PlayMusic(NightMusic);
	}
	for (int32 I = 0; I < ProLanded; ++I) { Units[I]->Sim(Dt); }
	SeparateUnits(Dt);

	// ----- the trees come down -----
	for (int32 I = 0; I < ProTrees.Num(); ++I)
	{
		ATSCPrimActor* Tree = ProTrees[I];
		const float D = T - ProTreeFell[I];
		if (D <= 0.f || Tree->IsHidden()) { continue; }
		if (D < 1.3f)
		{
			const float A = D / 1.3f;
			const float Ang = A * A * HALF_PI * 0.985f;
			const float S = FMath::Sin(Ang);
			Tree->SetActorRotation(FQuat::FindBetweenNormals(FVector::UpVector, FVector(FMath::Cos(ProTreeYaw[I]) * S, FMath::Sin(ProTreeYaw[I]) * S, FMath::Cos(Ang))));
		}
		else if (D < 2.5f)
		{
			Tree->SetActorScale3D(FVector(FMath::Max(0.02f, 1.f - (D - 1.3f) / 1.2f)));  // limbed and hauled off
		}
		else
		{
			Tree->SetActorHiddenInGame(true);
		}
	}

	// ----- the wall goes up from the gate outwards, then the houses and everything in them -----
	for (int32 I = 0; I < WallSegs.Num(); ++I)
	{
		const float D = (T - (ProTClear + 0.3f + 9.5f * I / WallSegs.Num())) / 0.3f;
		if (D <= 0.f) { continue; }
		WallSegs[I]->SetActorScale3D(FVector(1.f, 1.f, FMath::Clamp(D, 0.05f, 1.f)));
		if (WallSegs[I]->IsHidden()) { WallSegs[I]->SetActorHiddenInGame(false); }
	}
	for (int32 I = 0; I < ProDressing.Num(); ++I)
	{
		if (ProDressing[I]->IsHidden() && T > ProTClear + 1.5f + 10.f * I / ProDressing.Num()) { ProDressing[I]->SetActorHiddenInGame(false); }
	}
	for (int32 I = 0; I < ProBuildings.Num(); ++I)
	{
		const float P = FMath::Clamp((T - (ProTClear + 1.f + I * 1.2f)) / 5.5f, 0.f, 1.f);
		if (!FMath::IsNearlyEqual(ProBuildings[I]->BuildProgress, P)) { ProBuildings[I]->SetBuildProgress(P); }
	}
	for (int32 I = 0; I < ProLevelProps.Num(); ++I)
	{
		AActor* A = ProLevelProps[I];
		if (A && A->IsHidden() && T > ProTClear + 5.f + 8.f * I / ProLevelProps.Num()) { A->SetActorHiddenInGame(false); }
	}

	// ----- last of all the road fence grows out of the gate, piece by piece, as the village is finished -----
	{
		const float FenceStart = ProTClear + 10.5f;
		for (int32 I = 0; I < ProRoadFence.Num(); ++I)
		{
			AActor* F = ProRoadFence[I];
			if (!F) { continue; }
			const float D = (T - (FenceStart + ProFenceSpan * I / ProRoadFence.Num())) / 0.4f;
			if (D <= 0.f) { continue; }
			F->SetActorScale3D(ProRoadFenceScale[I] * FVector(1.f, 1.f, FMath::Clamp(D, 0.05f, 1.f)));
			if (F->IsHidden()) { F->SetActorHiddenInGame(false); }
		}
	}

	// ----- animals: Hakon's dog comes ashore with him; livestock and the village dog arrive once the village is finished -----
	{
		if (HunterDog && HunterDog->IsHidden() && Hunter && !Hunter->IsHidden())
		{
			HunterDog->Teleport(Hunter->GetActorLocation() + FVector(-110.f, 60.f, 0.f));
			HunterDog->SetActorHiddenInGame(false);
		}
		// finished = the last building and the road fence are both done
		const float BuiltAt = FMath::Max(ProTClear + 1.f + FMath::Max(ProBuildings.Num() - 1, 0) * 1.2f + 5.5f, ProTClear + 10.5f + ProFenceSpan + 0.4f);
		const float FarmT = FMath::Min(BuiltAt + 0.5f, ProTWake - 1.5f);
		if (T >= FarmT)
		{
			const float Grow = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((T - FarmT) / 1.2f, 0.f, 1.f));
			for (ATSCUnit* A : FarmAnimals)
			{
				if (!A) { continue; }
				A->SetActorScale3D(FVector(FMath::Max(Grow, 0.05f)));
				if (A->IsHidden()) { A->SetActorHiddenInGame(false); }
			}
		}
	}

	// ----- dusk, fog, and the things that grow in it -----
	ProDusk = FMath::SmoothStep(ProTRaise - 2.5f, ProTRaise + 2.5f, T);
	for (int32 I = 0; I < ProWatchers.Num(); ++I)
	{
		ATSCUnit* W = ProWatchers[I];
		const float In = FMath::Clamp((T - (ProTRaise + 1.2f + I * 0.35f)) / 1.6f, 0.f, 1.f);
		const float Out = FMath::Clamp((T - (ProTWake - 3.6f + I * 0.2f)) / 2.2f, 0.f, 1.f);
		const float A = FMath::SmoothStep(0.f, 1.f, In) * (1.f - FMath::SmoothStep(0.f, 1.f, Out));
		const bool bShow = A > 0.01f;
		if (W->IsHidden() == bShow) { W->SetActorHiddenInGame(!bShow); }
		if (!bShow) { continue; }
		const FVector Front = CellF(6.7f - (I % 2) * 1.1f, 9.3f + I * 0.95f);
		W->SetActorLocation(FMath::Lerp(Front + FVector(-700.f, 0.f, 0.f), Front, A));
		W->SetActorRotation(FRotator::ZeroRotator);  // facing the gate
		W->SetActorScale3D(FVector(FMath::Max(0.05f, A)));
		W->Idle(Dt);
	}

	// ----- camera: one slow move per beat -----
	{
		const FVector Target = T < ProTLand ? CellF(25.5f, 13.f) : T < ProTClear ? CellF(20.f, 12.f) : T < ProTRaise ? CellF(17.f, 12.f)
			: T < ProTWake ? CellF(10.5f, 11.5f) : CellF(17.f, 12.f);
		const float Arm = T < ProTLand ? 10500.f : T < ProTClear ? 14500.f : T < ProTRaise ? DefaultCamArm : T < ProTWake ? 9500.f : DefaultCamArm;
		ProCamLoc = FMath::VInterpTo(ProCamLoc, Target, Dt, 0.8f);
		ProCamArm = FMath::FInterpTo(ProCamArm, Arm, Dt, 0.8f);
		SetCameraView(ProCamLoc, ProCamArm);
	}
}

// =====================================================================================================================
// Tick
// =====================================================================================================================

void ATSCGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float RealDt = FMath::Min(DeltaSeconds, 0.1f);
	WorldTime += RealDt;
	UpdatePineWind(GetWorld(), RealDt);

	if (!bCameraSet)
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				Pawn->SetActorLocationAndRotation(CellF(17.f, 12.f), FRotator(0.f, -45.f, 0.f));
				bCameraSet = true;
			}
		}
	}

	// visuals keep running while the simulation is frozen
	for (ATSCPrimActor* P : Prims) { if (P) { P->SimVisual(RealDt, WorldTime); } }
	SimEmbers(RealDt);
	for (int32 I = Tracers.Num() - 1; I >= 0; --I) { Tracers[I].Time -= RealDt; if (Tracers[I].Time <= 0.f) { Tracers.RemoveAtSwap(I); } }
	for (int32 I = Toasts.Num() - 1; I >= 0; --I) { Toasts[I].Time -= RealDt; if (Toasts[I].Time <= 0.f) { Toasts.RemoveAt(I); } }
	BannerTimer = FMath::Max(0.f, BannerTimer - RealDt);
	FlashTimer = FMath::Max(0.f, FlashTimer - RealDt);
	if (TransitionTimer > 0.f && !bCutOpen)
	{
		TransitionTimer -= RealDt;
		if (FMath::FRand() < RealDt * 12.f) { for (const FTSCScene& S : Scenes) { if (S.bAshen && S.Timer < 0.f) { SpawnEmbers(S.Region, 2); } } }
	}

	// the scout boat rows away once its scene is over
	if (RaiderBoat && Scenes[3].bResolved && !RaiderBoat->IsHidden())
	{
		FRotator Heading = RaiderBoat->GetActorRotation();
		Heading.Yaw = 180.f; // it came in bow-west; it rows away bow-east
		RaiderBoat->SetActorRotation(Heading);
		RaiderBoat->AddActorWorldOffset(FVector(320.f * RealDt, 0.f, 0.f));
		if (RaiderBoat->GetActorLocation().X > CellF(44.f, 0.f).X) { RaiderBoat->SetActorHiddenInGame(true); }
	}

	// gate leaves
	{
		// A barred gate swings open for a villager who walks up to it and shuts again behind him; never while an enemy is at the gate.
		bool bVillagerAtGate = false, bEnemyAtGate = false;
		if (bGateClosed && !bGateBroken)
		{
			const FVector GatePoint = CellCorner(GateX, GateY + 1);
			for (const ATSCUnit* U : Units)
			{
				if (!U || !U->IsTargetable() || U->IsHidden() || U->IsWildlife()) { continue; }
				const float D = FVector::Dist2D(U->GetActorLocation(), GatePoint);
				if (U->Team == ETSCTeam::Villager) { bVillagerAtGate |= D < 320.f; }
				else { bEnemyAtGate |= D < 500.f; }
			}
		}
		GateVillagerLinger = (bVillagerAtGate && !bEnemyAtGate) ? 1.f : FMath::Max(0.f, GateVillagerLinger - RealDt);
		const float Want = (bGateClosed && !bGateBroken && GateVillagerLinger <= 0.f) ? 0.f : 1.f;
		GateOpenAlpha = FMath::FInterpConstantTo(GateOpenAlpha, Want, RealDt, 1.2f);
		if (GateLeaves.Num() == 2)
		{
			const float Fall = bGateBroken ? 75.f : 0.f;
			GateLeaves[0]->SetRelativeRotation(FRotator(0.f, 100.f * GateOpenAlpha, Fall * GateOpenAlpha));
			GateLeaves[1]->SetRelativeRotation(FRotator(0.f, -100.f * GateOpenAlpha, -Fall * GateOpenAlpha));
		}
	}

	if (bPrologue && Phase == ETSCPhase::Title) { SimPrologue(RealDt); }

	if (!IsFrozen())
	{
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(SpeedMul));
		const float Dt = RealDt * SpeedMul / Steps;
		for (int32 S = 0; S < Steps && !IsFrozen(); ++S)
		{
			if (Phase == ETSCPhase::Day) { SimDay(Dt); }
			else if (Phase == ETSCPhase::Night) { SimNight(Dt); }
			SimTimber(Dt);
			for (int32 I = 0; I < Units.Num(); ++I) { if (Units[I]) { Units[I]->Sim(Dt); } }
			for (ATSCUnit* A : Wildlife) { if (A && !A->IsHidden()) { A->Sim(Dt); } } // hidden = not arrived yet (prologue)
			SeparateUnits(Dt);
#if !UE_BUILD_SHIPPING
			if (CVarNavCheck.GetValueOnGameThread() != 0) { CheckNav(); }
#endif
		}
	}
#if !UE_BUILD_SHIPPING
	if (CVarNavDebug.GetValueOnGameThread() != 0) { DrawNavDebug(); }
#endif
	{
		FTSCOceanInput In;
		In.Time = WorldTime;
		In.Ash = DockActor ? DockActor->SagaAlpha : 0.f;
		In.bDaylight = Phase == ETSCPhase::Day || Phase == ETSCPhase::Title;
		In.Focus = CellF(17.f, 12.f);
		In.Zoom01 = 0.5f;
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn()) { In.Focus = Pawn->GetActorLocation(); }
			if (PC->PlayerCameraManager)
			{
				In.Zoom01 = FMath::Clamp((FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), In.Focus) - 6000.f) / 14000.f, 0.f, 1.f);
			}
		}
		if (Sun)
		{
			In.SunColor = Sun->GetLightColor();
			In.SunIntensity = Sun->Intensity;
		}
		In.MeadowColor = Pal::ForestFloor;
		if (Ocean) { Ocean->Sim(RealDt, In); }
		if (Terrain) { Terrain->Sim(RealDt, In, AshAmount); }
	}
	UpdateAtmosphere(RealDt);
	if (bAuto) { AutoPlay(RealDt); }
}

float ATSCGameMode::GroundReliefAt(const FVector& P) const
{
	return Terrain ? Terrain->GroundRelief(FVector2D(P)) : 0.f;
}

float ATSCGameMode::WadeDepthAt(const FVector& P) const
{
	const float ShoreX = Ocean ? Ocean->ShoreXAt(P.Y) : CellF(28.f, 0.f).X;
	if (P.X <= ShoreX) { return 0.f; }
	if (DockActor && !DockActor->IsHidden() && DockActor->SagaAlpha < 0.5f)
	{
		const FVector C = CellCorner(26, 11);
		if (P.X >= C.X && P.X <= C.X + 6.f * CELL && P.Y >= C.Y && P.Y <= C.Y + 2.f * CELL) { return 0.f; }
	}
	return FMath::Clamp((P.X - ShoreX) / 400.f, 0.f, 1.f) * 55.f;
}

void ATSCGameMode::WadeStep(const FVector& P, const FVector& Dir)
{
	if (Ocean) { Ocean->WadeStep(FVector2D(P), FVector2D(Dir)); }
}

void ATSCGameMode::UpdateAtmosphere(float Dt)
{
	if (!Sun) { return; }
	int32 NumAshen = 0;
	for (const FTSCScene& S : Scenes) { NumAshen += S.bAshen ? 1 : 0; }
	AshAmount = FMath::FInterpTo(AshAmount, NumAshen / 4.f, Dt, 0.6f);

	const bool bNight = Phase == ETSCPhase::Night || (Phase == ETSCPhase::Epilogue);
	const float F = Phase == ETSCPhase::Day ? FMath::Clamp(DayTime / DayLength, 0.f, 1.f) : (Phase == ETSCPhase::Title ? (bPrologue ? FMath::Lerp(0.1f, 1.f, ProDusk) : 0.55f) : 1.f);
	// cold blue-grey day sliding into an orange dusk; the night is a deep blue lit by fires
	FLinearColor SunCol = FMath::Lerp(FLinearColor(0.62f, 0.74f, 1.f), FLinearColor(1.f, 0.56f, 0.34f), F * F);
	// ambient comes from the sky light only, so the sun's moving shadows read; the sun is a little stronger to hold the overall brightness
	float SunInt = FMath::Lerp(6.5f, 4.f, F);
	float FillInt = FMath::Lerp(0.17f, 0.14f, F);
	if (bNight)
	{
		SunCol = FLinearColor(0.35f, 0.48f, 1.f);
		SunInt = 1.0f;
		FillInt = 4.f; // the night sky is nearly black: lift what little it gives
	}
	static float CurSun = 2.f, CurFill = 0.7f;
	static FLinearColor CurCol(0.6f, 0.7f, 1.f);
	const float K = Dt <= 0.f ? 1.f : FMath::Clamp(Dt * 0.8f, 0.f, 1.f);
	CurSun = FMath::Lerp(CurSun, SunInt, K);
	CurFill = FMath::Lerp(CurFill, FillInt, K);
	CurCol = FMath::Lerp(CurCol, SunCol, K);

	// the sun crosses the sky over each day: low in the morning, high at noon, low again at dusk, swinging 130 degrees
	// around the clearing, so the shadows turn and stretch. It follows DayTime, hence also the fast-forward speed.
	// At night the same light is the moon and settles at a fixed angle.
	static FQuat CurSunRot = GTSCDaySun;
	const FRotator DayArc(-(FMath::Lerp(28.f, 22.f, F) + 32.f * FMath::Sin(F * PI)), -22.f + FMath::Lerp(-65.f, 65.f, F), 0.f);
	GTSCDaySun = FQuat::Slerp(GTSCDaySun, DayArc.Quaternion(), K);
	CurSunRot = FQuat::Slerp(CurSunRot, bNight ? FRotator(-42.f, -22.f, 0.f).Quaternion() : GTSCDaySun, K);
	Sun->SetWorldRotation(CurSunRot);
	// a low sun lights the ground less; make up most of it so mornings and dusks keep the brightness the game was tuned for (42 degrees)
	const float SunHeight = FMath::Max(-CurSunRot.GetForwardVector().Z, 0.3f);
	Sun->SetIntensity(CurSun * FMath::Pow(0.669f / SunHeight, 0.6f));
	Sun->SetLightColor(CurCol);
	if (USkyLightComponent* Sky = GTSCSkyLight.Get())
	{
		Sky->SetIntensity(CurFill);
		Sky->SetLightColor(FMath::Lerp(FLinearColor(0.8f, 0.9f, 1.f), FLinearColor(1.f, 0.9f, 0.8f), AshAmount));
	}

	const float Thick = (Phase == ETSCPhase::Day && Day >= 3) || bNight || (bPrologue && ProDusk > 0.5f) ? 1.7f : 1.f;
	static float CurThick = 1.f;
	CurThick = FMath::Lerp(CurThick, Thick, K);
	Fog->SetFogDensity(0.012f * CurThick);
	Fog->SetFogInscatteringColor(FMath::Lerp(FLinearColor(0.16f, 0.23f, 0.30f), FLinearColor(0.24f, 0.21f, 0.19f), AshAmount) * (bNight ? 0.45f : 1.f));
	TSCUpdateSky(Sun, AshAmount, K, bNight, F, Thick, Dt, SpeedMul);

	// colour drain: the more of the world is Ashen, the greyer it gets; a hard dip while a cut is transitioning
	float Sat = FMath::Lerp(1.08f, 0.72f, AshAmount);
	float Gain = 1.f;
	if (TransitionTimer > 0.f)
	{
		const float T = FMath::Sin(FMath::Clamp(TransitionTimer / 3.4f, 0.f, 1.f) * PI);
		Sat *= 1.f - 0.85f * T;
		Gain += 0.25f * T;
	}
	if (bCutOpen) { Sat *= 0.35f; Gain = 0.6f; }
	PostFx->Settings.ColorSaturation = FVector4(Sat, Sat, Sat, 1.f);
	PostFx->Settings.ColorGain = FVector4(Gain, Gain * (1.f - 0.04f * AshAmount), Gain * (1.f - 0.1f * AshAmount), 1.f);
}

// Drives the Ultra Dynamic Sky Blueprint from the game's clock. The Blueprint's variables are set by name, so the C++
// module does not depend on the asset. Its own Tick pushes them into the sky material every frame.
static void TSCUpdateSky(UDirectionalLightComponent* Sun, float AshAmount, float K, bool bNight, float DayF, float Thick, float Dt, float SpeedMul)
{
	AActor* SkyActor = GTSCSky.Get();
	if (!SkyActor || !Sun) { return; }
	UClass* C = SkyActor->GetClass();
	auto SetNum = [&](const TCHAR* Name, double V)
	{
		if (FNumericProperty* P = FindFProperty<FNumericProperty>(C, Name)) { P->SetFloatingPointPropertyValue(P->ContainerPtrToValuePtr<void>(SkyActor), V); }
	};

	// 0..2400 clock: morning sliding to sunset over the day, then deep night (the epilogue stays dark too)
	const float TargetTime = bNight ? 2330.f : FMath::Lerp(800.f, 1850.f, DayF);
	GTSCSkyTime = FMath::Lerp(GTSCSkyTime, TargetTime, K);
	SetNum(TEXT("Time of Day"), GTSCSkyTime);
	// the sky's own night is pitch black; lift it so the cloud deck still reads over the fires
	SetNum(TEXT("Night Brightness"), 0.5f * FMath::GetMappedRangeValueClamped(FVector2f(1850.f, 2330.f), FVector2f(0.f, 1.f), GTSCSkyTime));

	// heavier cloud when the fog thickens, overcast as the world turns Ashen; the Blueprint eases towards the target
	const float Clouds = FMath::Lerp(Thick > 1.f ? 2.1f : 1.25f, 3.2f, AshAmount);
	SetNum(TEXT("Cloud Density_target"), Clouds);
	if (K >= 1.f) { SetNum(TEXT("Cloud Density"), Clouds); }
	SetNum(TEXT("Saturation"), FMath::Lerp(1.f, 0.35f, AshAmount));

	// cloud shadows: the sky's light-function material goes on the game's own sun and follows the same cloud cover
	// (the sky's defaults are sized for a far bigger world, so the pattern is scaled down to this clearing)
	SetNum(TEXT("Cloud Shadows Scale"), 40000.f);
	SetNum(TEXT("Cloud Shadows Intensity"), bNight ? 0.2f : 0.4f);
	if (FBoolProperty* P = FindFProperty<FBoolProperty>(C, TEXT("Use Cloud Shadows"))) { P->SetPropertyValue_InContainer(SkyActor, true); }
	FObjectProperty* ShadowProp = FindFProperty<FObjectProperty>(C, TEXT("Cloud Shadows MID"));
	if (UMaterialInstanceDynamic* Shadows = ShadowProp ? Cast<UMaterialInstanceDynamic>(ShadowProp->GetObjectPropertyValue_InContainer(SkyActor)) : nullptr)
	{
		FNumericProperty* Density = FindFProperty<FNumericProperty>(C, TEXT("Cloud Density"));
		if (Density) { Shadows->SetScalarParameterValue(TEXT("Cloud Density"), Density->GetFloatingPointPropertyValue(Density->ContainerPtrToValuePtr<void>(SkyActor))); }
		// fast-forward: the materials run on real time, so the extra travel is added as a time offset (no jump on toggle)
		float ShadowSpeed = 0.3f;
		Shadows->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Cloud Speed")), ShadowSpeed);
		GTSCShadowDrift += Dt * (SpeedMul - 1.f) * ShadowSpeed;
		Shadows->SetScalarParameterValue(TEXT("Cloud Time Offset"), GTSCShadowDrift);
		Sun->SetLightFunctionMaterial(Shadows);
		Sun->SetLightFunctionFadeDistance(1000000.f);
	}

	FObjectProperty* SkyMatProp = FindFProperty<FObjectProperty>(C, TEXT("UDM_mat"));
	FNumericProperty* SkySpeed = FindFProperty<FNumericProperty>(C, TEXT("Cloud Speed"));
	if (UMaterialInstanceDynamic* SkyMat = SkyMatProp ? Cast<UMaterialInstanceDynamic>(SkyMatProp->GetObjectPropertyValue_InContainer(SkyActor)) : nullptr)
	{
		const float Speed = SkySpeed ? (float)SkySpeed->GetFloatingPointPropertyValue(SkySpeed->ContainerPtrToValuePtr<void>(SkyActor)) : 0.05f;
		GTSCSkyDrift += Dt * (SpeedMul - 1.f) * Speed;
		SkyMat->SetScalarParameterValue(TEXT("Cloud Time Offset"), GTSCSkyDrift);
	}

	// sun disc: exactly where the game's sun light stands on its daily arc; after dusk it sinks below the horizon
	FStructProperty* RotProp = FindFProperty<FStructProperty>(C, TEXT("Sun Rotation"));
	if (RotProp && RotProp->Struct == TBaseStructure<FRotator>::Get())
	{
		FRotator R = GTSCDaySun.Rotator();
		R.Pitch += 40.f * FMath::GetMappedRangeValueClamped(FVector2f(1850.f, 2100.f), FVector2f(0.f, 1.f), GTSCSkyTime);
		*RotProp->ContainerPtrToValuePtr<FRotator>(SkyActor) = R;
	}
}

// =====================================================================================================================
// Embers
// =====================================================================================================================

void ATSCGameMode::SpawnEmbers(int32 Region, int32 Count)
{
	const FIntRect& R = Region == TSCRegion::Forest ? ForestRect : Region == TSCRegion::Garden ? GardenRect : Region == TSCRegion::Grove ? GroveRect : DockRect;
	for (int32 I = 0; I < Count && Embers.Num() < 140; ++I)
	{
		const FVector P = CellF(FMath::FRandRange((float)FMath::Max(R.Min.X, -2), (float)FMath::Min(R.Max.X, GW)), FMath::FRandRange((float)FMath::Max(R.Min.Y, 0), (float)FMath::Min(R.Max.Y, GH)));
		ATSCPrimActor* A = GetWorld()->SpawnActor<ATSCPrimActor>(ATSCPrimActor::StaticClass(), P + FVector(0.f, 0.f, FMath::FRandRange(10.f, 200.f)), FRotator(FMath::FRandRange(0.f, 90.f), FMath::FRandRange(0.f, 90.f), 0.f));
		const bool bAsh = FMath::FRand() < 0.35f;
		const int32 Idx = A->AddPart(ETSCShape::Cube, FVector::ZeroVector, FRotator::ZeroRotator, FVector(bAsh ? 0.26f : 0.18f, bAsh ? 0.26f : 0.18f, 0.04f), bAsh ? Pal::Ash * 2.f : Pal::Fire);
		if (!bAsh) { A->SetGlow(Idx, FLinearColor(18.f, 5.f, 0.5f), FLinearColor(18.f, 5.f, 0.5f)); }
		A->Parts[Idx].ACol = A->Parts[Idx].LCol;
		A->Refresh();
		FTSCEmber E;
		E.Actor = A;
		E.Vel = FVector(FMath::FRandRange(-40.f, 60.f), FMath::FRandRange(-40.f, 40.f), bAsh ? FMath::FRandRange(-30.f, 40.f) : FMath::FRandRange(70.f, 190.f));
		E.Life = FMath::FRandRange(1.8f, 3.6f);
		Embers.Add(E);
	}
}

void ATSCGameMode::SimEmbers(float Dt)
{
	for (int32 I = Embers.Num() - 1; I >= 0; --I)
	{
		FTSCEmber& E = Embers[I];
		E.Life -= Dt;
		if (E.Life <= 0.f || !E.Actor)
		{
			if (E.Actor) { E.Actor->Destroy(); }
			Embers.RemoveAtSwap(I);
			continue;
		}
		E.Actor->AddActorWorldOffset(E.Vel * Dt);
		E.Actor->AddActorLocalRotation(FRotator(120.f * Dt, 90.f * Dt, 0.f));
		E.Actor->SetActorScale3D(FVector(FMath::Clamp(E.Life, 0.05f, 1.f)));
	}
}

// =====================================================================================================================
// Flow
// =====================================================================================================================

void ATSCGameMode::StartGame()
{
	if (Phase != ETSCPhase::Title) { return; }
	EndPrologue();
	StartDay(1);
}

void ATSCGameMode::RestartGame()
{
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
}

void ATSCGameMode::ToggleSpeed()
{
	SpeedMul = SpeedMul < 1.5f ? 2.f : (SpeedMul < 3.f ? 4.f : 1.f);
}

FString ATSCGameMode::PhaseLabel() const
{
	switch (Phase)
	{
	case ETSCPhase::Day: return Day == 1 ? TEXT("DAY 1 - SETTLING IN") : Day == 2 ? TEXT("DAY 2 - GROWING THE VILLAGE") : TEXT("DAY 3 - PREPARING");
	case ETSCPhase::Night: return TEXT("FINAL NIGHT - THE DIRECTOR'S CUT");
	case ETSCPhase::Epilogue: return TEXT("EPILOGUE");
	default: return TEXT("");
	}
}

float ATSCGameMode::PhaseFrac() const
{
	if (Phase == ETSCPhase::Day) { return FMath::Clamp(DayTime / DayLength, 0.f, 1.f); }
	if (Phase == ETSCPhase::Night) { return FMath::Clamp(NightTime / NightLength, 0.f, 1.f); }
	return 0.f;
}

void ATSCGameMode::ShowBanner(const FString& Text, float Time)
{
	Banner = Text;
	BannerTimer = Time;
}

void ATSCGameMode::Toast(const FString& Text)
{
	UE_LOG(LogSaga, Display, TEXT("[Saga] %s"), *Text);
	FTSCToast T;
	T.Text = Text;
	T.Time = 9.f;
	Toasts.Add(T);
	if (Toasts.Num() > 5) { Toasts.RemoveAt(0); }
}

void ATSCGameMode::AddTracer(const FVector& From, const FVector& To, const FLinearColor& Col)
{
	FTSCTracer T;
	T.From = From;
	T.To = To;
	T.Color = Col;
	T.Time = 0.22f;
	Tracers.Add(T);
}

void ATSCGameMode::StartDay(int32 InDay)
{
	Phase = ETSCPhase::Day;
	PlayMusic(DayMusic);  // keeps the current take running from one day to the next
	Day = InDay;
	DayTime = 0.f;
	DayFlags = 0;
	DayFoodMul = 1.f;
	if (Day == 1)
	{
		bDayOneOnboarding = !bAuto && UGameplayStatics::GetCurrentLevelName(this, true) == TEXT("Map_Saga");
		ShowBanner(TEXT("Three days. One night. Two sagas."), 3.f);
	}
	else if (Day == 2)
	{
		ShowBanner(TEXT("Day 2 - Growing the village.  Choose what to raise on the marked plot with the build bar below."), 12.f);
	}
	else
	{
		ShowBanner(TEXT("Day 3 - The fog is thickening, and sails have been sighted.  They come tonight. Finish your preparations."), 14.f);
		Plots[1].bAvailable = true;
		if (!Plots[1].bUsed) { Plots[1].Marker->SetActorHiddenInGame(false); }
		Toast(TEXT("A second plot has been cleared by the shore path."));
		if (bClearingSpruce) { FinishSpruceClearing(); }
	}
	UpdateJobs();
}

void ATSCGameMode::SimDay(float Dt)
{
	// scripted beats; the clock waits while a scene is playing
	if (ActiveScene >= 0)
	{
		SimScene(ActiveScene, Dt);
	}
	else
	{
		DayTime += Dt;
	}
	auto Once = [this](int32 Bit, float T) { if (DayTime >= T && !(DayFlags & (1 << Bit))) { DayFlags |= 1 << Bit; return true; } return false; };
	if (ActiveScene < 0)
	{
		if (Day == 1)
		{
			if (Once(0, 24.f)) { StartScene(0); }
			if (Once(1, 62.f) && Woodcutter->IsAlive()) { Woodcutter->OrderHold(CellF(UlfChop.X, UlfChop.Y)); }
			if (Once(2, 74.f)) { StartScene(1); }
		}
		else if (Day == 2)
		{
			if (Once(1, 40.f) && Healer->IsAlive()) { Healer->OrderHold(CellF(14.4f, 20.3f)); Toast(TEXT("Sigrun walks to the stag grove to gather glowing mushrooms.")); }
			if (Once(2, 58.f)) { StartScene(2); }
		}
		else if (Day == 3)
		{
			if (Once(2, 46.f)) { StartScene(3); }
		}
	}

	SimConstruction(Dt);
	if (bClearingSpruce) { SimSpruceClearing(Dt); }
	if (ActiveScene < 0) { EconomyTimer += Dt; }
	if (EconomyTimer >= EconomyInterval)
	{
		EconomyTimer -= EconomyInterval;
		EconomyTick();
	}

	if (DayTime >= DayLength && ActiveScene < 0)
	{
		if (Day < 3) { StartDay(Day + 1); }
		else { StartNight(); }
	}
}

// ----- economy ------------------------------------------------------------------------------------------------------

void ATSCGameMode::ComputeRates(float& OutWood, float& OutFood) const
{
	int32 Living = 0;
	for (const ATSCUnit* U : Units) { if (U && U->Team == ETSCTeam::Villager && U->IsAlive()) { ++Living; } }
	OutWood = NumWood * 1.f * ForestMul + 1.f + ExtraSawmills * 2.f + (Woodcutter && Woodcutter->IsAlive() ? 1.f : 0.f);
	OutFood = (NumFood * 1.5f * GardenMul + GroveBonus + DockBonus + (Hunter && Hunter->IsAlive() ? 2.f : 0.f) + (bStagBlessing ? 2.f : 0.f)) * DayFoodMul - Living * 0.6f;
}

void ATSCGameMode::EconomyTick()
{
	float W, F;
	ComputeRates(W, F);
	WoodRate = FMath::RoundToInt(W);
	FoodRate = FMath::RoundToInt(F);
	Wood = FMath::Max(0.f, Wood + W);
	const bool bWasFed = Food > 0.f;
	Food = FMath::Max(0.f, Food + F);
	if (bWasFed && Food <= 0.f) { Toast(TEXT("The food stores are empty. Hungry defenders will fight poorly.")); }
	int32 Living = 0;
	for (const ATSCUnit* U : Units) { if (U && U->Team == ETSCTeam::Villager && U->IsAlive()) { ++Living; } }
	Pop = Living;
}

void ATSCGameMode::AdjustJob(ETSCJob Job, int32 Delta)
{
	if (IsFrozen()) { return; }
	int32& N = Job == ETSCJob::Wood ? NumWood : NumFood;
	const int32 Idle = Workers.Num() - NumWood - NumFood;
	if (Delta > 0 && Idle > 0)
	{
		++N;
		if (bDayOneOnboarding && Day == 1)
		{
			(Job == ETSCJob::Wood ? bAssignedWoodOnDayOne : bAssignedFoodOnDayOne) = true;
		}
	}
	else if (Delta < 0 && N > 0) { --N; }
	UpdateJobs();
	float W, F;
	ComputeRates(W, F);
	WoodRate = FMath::RoundToInt(W);
	FoodRate = FMath::RoundToInt(F);
}

void ATSCGameMode::UpdateJobs()
{
	Workers.RemoveAll([](const TObjectPtr<ATSCUnit>& U) { return !U || !U->IsAlive(); });
	NumWood = FMath::Clamp(NumWood, 0, Workers.Num());
	NumFood = FMath::Clamp(NumFood, 0, Workers.Num() - NumWood);
	NumIdle = Workers.Num() - NumWood - NumFood;
	int32 Living = 0;
	for (const ATSCUnit* U : Units) { if (U && U->Team == ETSCTeam::Villager && U->IsAlive()) { ++Living; } }
	Pop = Living;
	if (Phase == ETSCPhase::Night || Phase == ETSCPhase::Epilogue) { return; }

	const bool bForestOpen = ForestMul >= 1.f && !IsBlocked(TreeX, TreeY + 1) && !(ActiveScene == 1);
	int32 BuildPlot = -1;
	for (int32 I = 0; I < Plots.Num(); ++I) { if (Plots[I].bUsed && Plots[I].Progress < 1.f) { BuildPlot = I; } }

	for (int32 I = 0; I < Workers.Num(); ++I)
	{
		ATSCUnit* U = Workers[I];
		const ETSCJob Job = I < NumWood ? ETSCJob::Wood : (I < NumWood + NumFood ? ETSCJob::Food : ETSCJob::Idle);
		U->SetJob(Job);
		U->bScriptWork = false;
		U->WorkFace = FVector::ZeroVector;
		if (Job == ETSCJob::Wood && bClearingSpruce)
		{
			FVector Stand, Face;
			SpruceCutSpot(I, Stand, Face);
			U->bScriptWork = true;
			U->WorkFace = Face;
			U->OrderWork(Stand, CellF(10.6f + (I % 4) * 0.5f, 6.5f));
			U->Need = ETSCNeed::Wood;
		}
		else if (Job == ETSCJob::Wood)
		{
			static const float ChopY[6] = { 8.5f, 13.6f, 7.4f, 14.6f, 6.5f, 15.5f };
			const FVector A = bForestOpen ? CellF(7.5f, ChopY[I % 6]) : CellF(12.6f + (I % 3) * 0.5f, 6.5f + (I % 2) * 0.5f);
			U->OrderWork(A, CellF(10.6f + (I % 4) * 0.5f, 6.5f));
			U->Need = ETSCNeed::Wood;
		}
		else if (Job == ETSCJob::Food)
		{
			const int32 K = I - NumWood;
			const bool bForage = (K % 3 == 2) && GroveBonus > 0.f;
			const FVector A = bForage ? CellF(12.4f + K * 0.9f, 19.7f) : CellF(16.5f + (K % 4), 15.6f + (K / 4 % 3) * 0.9f);
			U->OrderWork(A, CellF(15.4f + (K % 5) * 0.7f, 13.7f));
			U->Need = ETSCNeed::Leaf;
		}
		else
		{
			const int32 K = I - NumWood - NumFood;
			U->Need = ETSCNeed::None;
			if (BuildPlot >= 0)
			{
				const FIntPoint C = Plots[BuildPlot].Cell;
				U->OrderWork(CellF(C.X + 0.4f + (K % 5) * 0.6f, C.Y + 2.35f), CellF(C.X + 0.4f + (K % 5) * 0.6f, C.Y + 2.9f));
			}
			else
			{
				// pray around the stone altar
				const float Ang = K * 2.4f;
				const float Ring = 230.f + (K % 2) * 60.f;
				const FVector Spot = AltarPos + FVector(FMath::Cos(Ang) * Ring, FMath::Sin(Ang) * Ring, 0.f);
				U->OrderWork(Spot, CellF(18.6f + (K % 4) * 0.5f, 14.f));
			}
		}
	}
	if (bClearingSpruce && Woodcutter && Woodcutter->IsAlive())
	{
		// Ulf takes the butt end, where he felled it.
		Woodcutter->bScriptWork = true;
		Woodcutter->WorkFace = CellF(SprucePos.X + 0.1f, SprucePos.Y + 0.6f);
		Woodcutter->OrderWork(CellF(UlfChop.X, UlfChop.Y), CellF(11.5f, 6.5f));
	}
}

void ATSCGameMode::SpruceCutSpot(int32 I, FVector& OutStand, FVector& OutFace) const
{
	// On the village side of the trunk, from the gate along to the crown (the trunk runs from SprucePos, 0.16 cell east per cell south).
	static const FVector2D Stands[4] = { { 10.45f, 11.4f }, { 10.45f, 12.6f }, { 11.35f, 13.5f }, { 11.35f, 14.5f } };
	const FVector2D P = Stands[I % 4] + FVector2D(0.45f * (I / 4), 0.f);
	OutStand = CellF(P.X, P.Y);
	OutFace = CellF(SprucePos.X + 0.16f * (P.Y - SprucePos.Y), P.Y);
}

void ATSCGameMode::SimSpruceClearing(float Dt)
{
	int32 Cutters = 0;
	for (const ATSCUnit* U : Units) { if (U && U->IsAlive() && U->bScriptWork && U->Order == ETSCOrder::Work) { ++Cutters; } }
	// Five axes (Ulf and four woodcutters) take about 30 s; fewer take longer, and someone always gets it done.
	const float Before = SpruceClear;
	SpruceClear = FMath::Min(1.f, SpruceClear + Dt * FMath::Clamp(Cutters, 1, 5) / (5.f * 30.f));
	// Sawn from the crown down: the trunk shortens toward the stump, and the cut lengths pile up beside it.
	EventTree->SetActorScale3D(FVector(SpruceFullScale.X, SpruceFullScale.Y, SpruceFullScale.Z * FMath::Lerp(1.f, 0.1f, SpruceClear)));
	for (const float Mark : { 0.2f, 0.45f, 0.7f })
	{
		if (Before < Mark && SpruceClear >= Mark)
		{
			const float Y = SprucePos.Y + (1.f - Mark) * 4.4f;
			ATSCPrimActor* Pile = SpawnPrim(CellF(SprucePos.X + 0.16f * (Y - SprucePos.Y) + 0.75f, Y));
			BuildLogPile(Pile);
			Pile->SetActorRotation(FRotator(0.f, 80.f, 0.f));
			Pile->Refresh();
			SprucePieces.Add(Pile);
		}
	}
	if (SpruceClear >= 1.f) { FinishSpruceClearing(); }
}

void ATSCGameMode::FinishSpruceClearing()
{
	bClearingSpruce = false;
	EventTree->SetActorHiddenInGame(true);
	for (ATSCPrimActor* P : SprucePieces) { if (P) { P->SetActorHiddenInGame(true); } } // hauled off to the woodpile
	SprucePieces.Reset();
	SetBlocked(TreeX, TreeY, false);
	SetSpruceFallen(false);
	Wood += 40.f;
	for (ATSCUnit* U : Units) { if (U) { U->bScriptWork = false; U->WorkFace = FVector::ZeroVector; } }
	if (Hunter->IsAlive()) { OrderPatrolRoute(Hunter); }
	if (Woodcutter->IsAlive()) { Woodcutter->OrderWork(CellF(7.5f, 9.5f), CellF(11.5f, 6.5f)); }
	Toast(TEXT("The fallen spruce is sawn up and hauled to the woodpile. The west gate is clear: +40 wood."));
	UpdateJobs();
}

// ----- construction -------------------------------------------------------------------------------------------------

int32 ATSCGameMode::BuildCost(EBuilding Type) const
{
	switch (Type)
	{
	case EBuilding::House: return 90;
	case EBuilding::Sawmill: return 110;
	case EBuilding::Healer: return 100;
	default: return 130;
	}
}

int32 ATSCGameMode::FreePlot() const
{
	for (int32 I = 0; I < Plots.Num(); ++I) { if (Plots[I].bAvailable && !Plots[I].bUsed) { return I; } }
	return -1;
}

bool ATSCGameMode::CanBuild(EBuilding Type) const
{
	if (Phase != ETSCPhase::Day || IsFrozen() || Wood < BuildCost(Type)) { return false; }
	if (Type == EBuilding::HammerBlessing) { return !bHammerBlessing; } // blessings are asked of the altar: no plot needed
	if (Type == EBuilding::StagBlessing) { return !bStagBlessing; }
	return FreePlot() >= 0;
}

void ATSCGameMode::StartBuild(EBuilding Type)
{
	if (!CanBuild(Type)) { return; }
	if (Type == EBuilding::HammerBlessing || Type == EBuilding::StagBlessing)
	{
		const bool bHammer = Type == EBuilding::HammerBlessing;
		Wood -= BuildCost(Type);
		(bHammer ? bHammerBlessing : bStagBlessing) = true;
		if (!AltarGlow)
		{
			AltarGlow = SpawnPrim(AltarPos);
		}
		AltarGlow->AddLight(FVector(0.f, 0.f, 160.f), bHammer ? Pal::Fire : Pal::Shroom, 4.f, 2.f, 600.f);
		AltarGlow->Refresh();
		BuiltNames.Add(bHammer ? TEXT("the Hammer's blessing") : TEXT("the Stag's blessing"));
		Toast(bHammer ? TEXT("The altar glows red-gold: your defenders will strike harder.")
			: TEXT("The altar glows green: the forest gives more food and sends fewer horrors."));
		UpdateJobs();
		return;
	}
	const int32 Idx = FreePlot();
	FTSCPlot& P = Plots[Idx];
	Wood -= BuildCost(Type);
	P.bUsed = true;
	P.Type = Type;
	P.Progress = 0.f;
	P.Marker->SetActorHiddenInGame(true);
	const int32 W = Type == EBuilding::Sawmill ? 3 : 2;
	P.Building = SpawnPrim(CellCorner(P.Cell.X, P.Cell.Y));
	BuildPlotBuilding(P.Building, Type, Idx + 2);
	P.Building->SetBuildProgress(0.f);
	BlockRect(P.Cell.X, P.Cell.Y, W, 2);
	static const TCHAR* Names[3] = { TEXT("a house"), TEXT("a second sawmill"), TEXT("a healer's hut") };
	Toast(TSCLoc::Fmt(TEXT("Your people begin raising {0}, tile by tile."), { TSCLoc::Tr(Names[(int32)Type]) }));
	UpdateJobs();
}

void ATSCGameMode::SimConstruction(float Dt)
{
	for (FTSCPlot& P : Plots)
	{
		if (!P.bUsed || P.Progress >= 1.f) { continue; }
		P.Progress = FMath::Min(1.f, P.Progress + Dt * (0.018f + 0.011f * NumIdle));
		P.Building->SetBuildProgress(P.Progress);
		if (P.Progress >= 1.f)
		{
			switch (P.Type)
			{
			case EBuilding::House:
				PopCap += 3;
				++HousesBuilt;
				for (int32 I = 0; I < 2; ++I)
				{
					ATSCUnit* W = SpawnUnit(ETSCRole::Worker, FString(), CellF(25.f, 11.6f + I * 0.6f));
					Workers.Add(W);
				}
				BuiltNames.Add(TEXT("a new house"));
				Toast(TEXT("The house stands. Two settlers arrive from the shore: more hands, and more spears for the night."));
				break;
			case EBuilding::Sawmill:
				++ExtraSawmills;
				BuiltNames.Add(TEXT("a second sawmill"));
				Toast(TEXT("The second sawmill is running: +2 wood income."));
				break;
			case EBuilding::Healer:
				++ExtraHealers;
				BuiltNames.Add(TEXT("a healer's hut"));
				Toast(TEXT("The healer's hut is ready: the wounded will be tended on the final night."));
				break;
			default:
				break;
			}
			UpdateJobs();
		}
	}
}

// =====================================================================================================================
// Scenes and the CUT! mechanic
// =====================================================================================================================

void ATSCGameMode::DefineScenes()
{
	Scenes.SetNum(4);
	{
		FTSCScene& S = Scenes[0];
		S.Title = TEXT("The Boar in the Garden");
		S.Region = TSCRegion::Garden;
		S.Alert = TEXT("A boar has broken out of the forest and is charging the vegetable garden!");
		S.LivingName = TEXT("LIVING SAGA - Wooden fence");
		S.LivingText = TEXT("Lose 30 food and half of today's harvest. The soil stays rich, but the longhouse gets no wall.");
		S.AshenName = TEXT("ASHEN SAGA - Stone wall");
		S.AshenText = TEXT("The wall saves the crops and shields the longhouse on the final night (+120 hall strength). Garden yields drop 40% for good.");
	}
	{
		FTSCScene& S = Scenes[1];
		S.Title = TEXT("The Spruce at the Gate");
		S.Region = TSCRegion::Forest;
		S.Alert = TEXT("The spruce has fallen across the west gate! Hakon the hunter is trapped outside, and the fog is moving.");
		S.LivingName = TEXT("THE LIVING SAGA - The fallen spruce");
		S.LivingText = TEXT("Hakon dies alone outside the gate, and the fog-born that kill him learn the way: three more come in every wave on the final night. The woodcutters saw up the spruce (+40 wood), and the forest keeps giving timber.");
		S.AshenName = TEXT("THE ASHEN SAGA - The burnt stump");
		S.AshenText = TEXT("Here the forest burned years ago. No spruce bars the gate, Hakon walks home, and his bow guards the west gate on the final night. But scorched stumps give no timber: no +40 wood, and forest wood is halved for good.");
	}
	{
		FTSCScene& S = Scenes[2];
		S.Title = TEXT("Spores in the Stag Grove");
		S.Region = TSCRegion::Grove;
		S.Alert = TEXT("Sigrun the healer has breathed the grove's spores and fallen! No one else can tend the wounded.");
		S.LivingName = TEXT("THE LIVING SAGA - The glowing grove");
		S.LivingText = TEXT("The spores take Sigrun before nightfall. On the final night no one heals your defenders unless you build a healer's hut. The grove keeps feeding the village (+3 food income).");
		S.AshenName = TEXT("THE ASHEN SAGA - The dead roots");
		S.AshenText = TEXT("Here the grove burned in the old war and nothing grows that could harm her. Sigrun lives and heals your defenders through the final night. But the glowing mushrooms are gone (-3 food income) and the Stag's blessing is weakened.");
	}
	{
		FTSCScene& S = Scenes[3];
		S.Title = TEXT("Scouts at the Dock");
		S.Region = TSCRegion::Dock;
		S.Alert = TEXT("A scout boat has made fast to the dock! Torvald the shield-bearer runs to meet the raiders alone.");
		S.LivingName = TEXT("THE LIVING SAGA - The timber dock");
		S.LivingText = TEXT("Torvald drives the scouts off but is badly wounded, and they carry away the stores (-30 food). Tonight six raiders land at the dock. The dock stands and the fishing goes on (+3 food income).");
		S.AshenName = TEXT("THE ASHEN SAGA - The charred posts");
		S.AshenText = TEXT("Here the dock burned with the fleet. The scouts row away, Torvald is unhurt, and tonight only three raiders wade ashore, slow in the surf. But the longship is a wreck and the fishing is gone (-3 food income).");
	}
}

void ATSCGameMode::StartScene(int32 Idx)
{
	FTSCScene& S = Scenes[Idx];
	S.bStarted = true;
	S.Step = 0;
	S.Timer = 0.f;
	ActiveScene = Idx;
	UE_LOG(LogSaga, Display, TEXT("[Saga] Scene %d starts: %s"), Idx, *S.Title);

	if (Idx == 0)
	{
		Boar = SpawnUnit(ETSCRole::Boar, FString(), CellF(0.5f, 12.f));
		Boar->Speed = 185.f;
		Boar->OrderHold(CellF(18.f, 14.6f));
		S.bCutOpenable = true;
		ShowBanner(S.Alert, 60.f);
	}
	else if (Idx == 1)
	{
		if (Woodcutter->IsAlive()) { Woodcutter->OrderHold(CellF(UlfChop.X, UlfChop.Y)); }
		if (Hunter->IsAlive()) { Hunter->OrderHold(CellF(3.5f, 12.f)); }
		UpdateJobs(); // call the woodcutters in from the forest
		ShowBanner(TEXT("Ulf the woodcutter sets his axe to the old spruce beside the west gate..."), 20.f);
	}
	else if (Idx == 2)
	{
		if (!Healer->IsAlive()) { FinishScene(Idx); return; }
		Healer->OrderHold(CellF(14.4f, 20.3f));
	}
	else if (Idx == 3)
	{
		RaiderBoat->SetActorHiddenInGame(false);
		RaiderBoat->SetActorRotation(FRotator::ZeroRotator);
		RaiderBoat->SetActorLocation(CellF(40.f, 12.f) + FVector(0.f, 0.f, -4.f));
		ShowBanner(TEXT("A dark sail slips out of the fog toward the dock..."), 20.f);
	}
}

void ATSCGameMode::SimScene(int32 Idx, float Dt)
{
	FTSCScene& S = Scenes[Idx];
	S.Timer += Dt;

	if (Idx == 0)
	{
		if (Boar && (Boar->AtHoldPos() || S.Timer > 40.f)) { ResolveLiving(Idx); }
	}
	else if (Idx == 1)
	{
		if (S.Step == 0)
		{
			// Ulf walks up and chops
			const bool bThere = !Woodcutter->IsAlive() || Woodcutter->AtHoldPos();
			if (bThere || S.Timer > 14.f)
			{
				if (Woodcutter->IsAlive()) { Woodcutter->Teleport(CellF(UlfChop.X, UlfChop.Y)); Woodcutter->OrderWork(CellF(UlfChop.X, UlfChop.Y), CellF(UlfChop.X, UlfChop.Y)); Woodcutter->SetActorRotation(FRotator(0.f, (CellF(SprucePos.X, SprucePos.Y) - CellF(UlfChop.X, UlfChop.Y)).Rotation().Yaw, 0.f)); }
				S.Step = 1;
				S.Timer = 0.f;
			}
		}
		else if (S.Step == 1 && S.Timer > 4.5f)
		{
			S.Step = 2;
			S.Timer = 0.f;
		}
		else if (S.Step == 2)
		{
			// the spruce falls south across the gateway, angled a little into the village so the palisade does not hide it
			const float A = FMath::Clamp(S.Timer / 1.3f, 0.f, 1.f);
			const float Ang = A * A * HALF_PI * 0.985f;
			const FVector FallDir = FVector(0.16f, 1.f, 0.f).GetSafeNormal();
			EventTree->SetActorRotation(FQuat::FindBetweenNormals(FVector::UpVector, FallDir * FMath::Sin(Ang) + FVector::UpVector * FMath::Cos(Ang)));
			if (A >= 1.f)
			{
				SetSpruceFallen(true);
				if (Woodcutter->IsAlive()) { Woodcutter->OrderHold(CellF(10.6f, 10.5f)); }
				if (Hunter->IsAlive())
				{
					Hunter->OrderHold(CellF(7.5f, 12.f));
					Hunter->bFights = true;
				}
				for (int32 I = 0; I < 6; ++I)
				{
					ATSCUnit* C = SpawnUnit(ETSCRole::Creature, FString(), CellF(-3.f - (I / 3) * 1.5f, 10.4f + (I % 3) * 1.1f));
					C->MaxHP = C->HP = 75.f;
					C->AggroRange = 450.f;
					C->bFights = false;   // they walk past the forest workers and only fight once at Hakon (see Step 3)
					// Hakon cannot win this fight: the fog-born go for him alone, and no blade (his, the woodcutters', the husky's)
					// can bring them down by day, so he falls in the fight instead of dropping dead when the scene times out.
					C->Quarry = Hunter;
					C->bInvulnerable = true;
					C->OrderHold(CellF(6.6f, 11.4f + (I % 3) * 0.5f));
					EventUnits.Add(C);
				}
				S.bCutOpenable = true;
				S.Step = 3;
				S.Timer = 0.f;
				ShowBanner(S.Alert, 90.f);
				Toast(TEXT("The spruce crashes down across the gateway. Hakon is on the wrong side of it."));
			}
		}
		else if (S.Step == 3)
		{
			for (ATSCUnit* C : EventUnits) { if (C && C->IsAlive() && !C->bFights && C->AtHoldPos()) { C->bFights = true; } }
			if (!Hunter->IsAlive() || S.Timer > 45.f) { ResolveLiving(Idx); }
		}
	}
	else if (Idx == 2)
	{
		if (S.Step == 0 && (Healer->AtHoldPos() || S.Timer > 20.f))
		{
			Healer->Teleport(CellF(14.4f, 20.3f));
			Healer->bDowned = true;
			Healer->Need = ETSCNeed::Heal;
			S.Step = 1;
			S.Timer = 0.f;
			S.bCutOpenable = true;
			ShowBanner(S.Alert, 60.f);
		}
		else if (S.Step == 1 && S.Timer > 24.f)
		{
			ResolveLiving(Idx);
		}
	}
	else if (Idx == 3)
	{
		if (S.Step == 0)
		{
			const float A = FMath::Clamp(S.Timer / 6.f, 0.f, 1.f);
			RaiderBoat->SetActorLocation(FMath::Lerp(CellF(40.f, 12.f), CellF(33.6f, 12.f), FMath::SmoothStep(0.f, 1.f, A)) + FVector(0.f, 0.f, -4.f));
			if (A >= 1.f)
			{
				for (int32 I = 0; I < 4; ++I)
				{
					ATSCUnit* R = SpawnUnit(ETSCRole::Raider, FString(), CellF(31.6f, 11.3f + I * 0.45f));
					R->OrderHold(CellF(27.6f + (I % 2) * 0.6f, 11.4f + (I / 2) * 0.8f));
					R->Damage = 5.f;
					EventUnits.Add(R);
				}
				if (Shield->IsAlive())
				{
					Shield->OrderHold(CellF(26.4f, 12.f));
					Shield->bInvulnerable = true;
					Shield->BarTimer = 30.f;
				}
				S.Step = 1;
				S.Timer = 0.f;
				S.bCutOpenable = true;
				ShowBanner(S.Alert, 60.f);
			}
		}
		else if (S.Step == 1 && S.Timer > 22.f)
		{
			ResolveLiving(Idx);
		}
	}
}

void ATSCGameMode::ResolveLiving(int32 Idx)
{
	FTSCScene& S = Scenes[Idx];
	if (Idx == 0)
	{
		Food = FMath::Max(0.f, Food - 30.f);
		DayFoodMul = 0.5f;
		for (int32 I = 0; I < CropParts.Num(); ++I) { if (I % 5 != 0) { Garden->SetShown(CropParts[I], false); } }
		Garden->Refresh();
		if (Boar) { Boar->OrderLeave(CellF(-4.f, 12.f)); }
		Toast(TEXT("The boar tramples the garden: -30 food, and the harvest is halved for today."));
	}
	else if (Idx == 1)
	{
		for (ATSCUnit* C : EventUnits) { if (C && C->IsAlive()) { C->bFights = false; C->Quarry = nullptr; C->bInvulnerable = false; C->OrderLeave(CellF(-5.f, 12.f)); } }
		EventUnits.Reset();
		if (Hunter->IsAlive()) { Hunter->Kill(); } // backstop only: the fog-born kill him well before the 45 s timeout
		// The villagers near the gate do not just pick their axes up again: they stop, turn to where he fell and stand a while.
		const FVector Fallen = Hunter->Loc2D();
		for (ATSCUnit* U : Units)
		{
			if (U && U != Hunter && U->Team == ETSCTeam::Villager && U->IsAlive() && FVector::Dist2D(U->GetActorLocation(), Fallen) < 1600.f)
			{
				U->MournAt = Fallen;
				U->MournTimer = FMath::FRandRange(5.f, 8.f);
				UE_LOG(LogSaga, Display, TEXT("[Saga] MOURN %s %.1fs"), *U->GetName(), U->MournTimer);
			}
		}
		Toast(TEXT("Hakon the hunter falls outside the gate he could not reach. The fog-born have learned the way."));
		// The fight is over: the woodcutters saw up the spruce and haul it off (SimSpruceClearing; UpdateJobs in FinishScene
		// sends them). The gate opens and the wood comes when they are done; the mourners join once they have stood a while.
		bClearingSpruce = true;
		SpruceClear = 0.f;
		Toast(TEXT("The woodcutters set their axes to the fallen spruce."));
	}
	else if (Idx == 2)
	{
		Healer->bDowned = false;
		Healer->Kill();
		Toast(TEXT("The spores take Sigrun the healer. The grove glows on, indifferent."));
	}
	else if (Idx == 3)
	{
		Food = FMath::Max(0.f, Food - 30.f);
		for (int32 C : CrateParts) { DockActor->SetShown(C, false); }
		DockActor->Refresh();
		for (ATSCUnit* R : EventUnits) { if (R && R->IsAlive()) { R->bFights = false; R->OrderLeave(CellF(31.6f, 12.f)); } }
		EventUnits.Reset();
		if (Shield->IsAlive())
		{
			Shield->bInvulnerable = false;
			Shield->HP = Shield->MaxHP * 0.45f;
			Shield->Need = ETSCNeed::Heal;
			Shield->BarTimer = 20.f;
			OrderPatrolRoute(Shield);
			bShieldWounded = true;
		}
		S.Step = 5; // boat leaves in the day sim below
		Toast(TEXT("The scouts are driven off, but Torvald is badly wounded and the stores are gone: -30 food."));
	}
	FinishScene(Idx);
}

void ATSCGameMode::TransitionRegion(int32 Region, const FVector& Origin)
{
	for (ATSCPrimActor* P : Prims)
	{
		if (P && P->Region == Region)
		{
			P->StartSagaTransition(1.f, FMath::Min(FVector::Dist2D(P->GetActorLocation(), Origin) / 2400.f, 2.4f));
		}
	}
	if (Terrain && Region >= TSCRegion::Forest && Region <= TSCRegion::Grove)
	{
		const FIntRect& Cells = Region == TSCRegion::Forest ? ForestRect
			: Region == TSCRegion::Garden ? GardenRect : GroveRect;
		Terrain->StartGroundTransition(Region,
			FBox2D(FVector2D(CellCorner(Cells.Min.X, Cells.Min.Y)), FVector2D(CellCorner(Cells.Max.X, Cells.Max.Y))),
			FVector2D(Origin));
	}
	SpawnEmbers(Region, 60);
	TransitionTimer = 3.4f;
	FlashTimer = 0.5f;
}

void ATSCGameMode::ApplyAshen(int32 Idx)
{
	FTSCScene& S = Scenes[Idx];
	S.bAshen = true;
	TransitionTitle = S.AshenName;
	if (Idx == 0)
	{
		TransitionRegion(TSCRegion::Garden, CellF(18.f, 16.5f));
		GardenMul = 0.6f;
		if (Boar) { Boar->OrderLeave(CellF(-4.f, 12.f)); }
		Toast(TEXT("EDIT: the boar turns away from an old stone wall. The crops are safe and the wall will shield the longhouse, but the ash-poor soil will always yield less."));
	}
	else if (Idx == 1)
	{
		TransitionRegion(TSCRegion::Forest, CellCenter(TreeX, TreeY));
		ForestMul = 0.5f;
		SetSpruceFallen(false);
		for (ATSCUnit* C : EventUnits) { if (C && C->IsAlive()) { C->bFights = false; C->Quarry = nullptr; C->bInvulnerable = false; C->OrderLeave(CellF(-5.f, 12.f)); } }
		EventUnits.Reset();
		if (Hunter->IsAlive())
		{
			Hunter->bInvulnerable = true;
			Hunter->bFights = false;
			Hunter->HP = Hunter->MaxHP;
			OrderPatrolRoute(Hunter);
		}
		if (Woodcutter->IsAlive()) { Woodcutter->OrderWork(CellF(13.f, 6.5f), CellF(11.5f, 6.5f)); }
		Toast(TEXT("EDIT: the forest burned years ago. The gate stands clear and Hakon walks home, but that timber is ash."));
	}
	else if (Idx == 2)
	{
		TransitionRegion(TSCRegion::Grove, CellCenter(15, 21));
		GroveBonus = 0.f;
		Healer->bDowned = false;
		Healer->Need = ETSCNeed::None;
		OrderPatrolRoute(Healer);
		Toast(TEXT("EDIT: the grove is dead roots and ash. Sigrun rises unharmed, but the glowing mushrooms will never feed you again."));
	}
	else if (Idx == 3)
	{
		TransitionRegion(TSCRegion::Dock, CellF(28.f, 12.f));
		DockBonus = 0.f;
		for (ATSCUnit* R : EventUnits) { if (R && R->IsAlive()) { R->bGone = true; R->SetActorHiddenInGame(true); } }
		EventUnits.Reset();
		if (Shield->IsAlive())
		{
			Shield->bInvulnerable = false;
			Shield->HP = Shield->MaxHP;
			OrderPatrolRoute(Shield);
		}
		S.Step = 5;
		Toast(TEXT("EDIT: the dock burned with the fleet. The scouts row away and Torvald is unhurt, but the longship and the fishing are lost."));
	}
	S.Timer = -1.f; // marks the scene whose region is burning (ember spawning)
	UpdateJobs();
}

void ATSCGameMode::FinishScene(int32 Idx)
{
	Scenes[Idx].bResolved = true;
	Scenes[Idx].bCutOpenable = false;
	ActiveScene = -1;
	BannerTimer = 0.f;
	UE_LOG(LogSaga, Display, TEXT("[Saga] Scene %d resolved: %s"), Idx, Scenes[Idx].bAshen ? TEXT("ASHEN") : TEXT("LIVING"));
	UpdateJobs();
	float W, F;
	ComputeRates(W, F);
	WoodRate = FMath::RoundToInt(W);
	FoodRate = FMath::RoundToInt(F);
}

bool ATSCGameMode::CanCut() const
{
	return Phase == ETSCPhase::Day && ActiveScene >= 0 && Scenes[ActiveScene].bCutOpenable && !Scenes[ActiveScene].bResolved && EditsLeft > 0
		&& !bCutOpen && TransitionTimer <= 0.f;
}

ETSCDayOnePrompt ATSCGameMode::GetDayOnePrompt() const
{
	if (!bDayOneOnboarding || Phase != ETSCPhase::Day || Day != 1 || IsFrozen())
	{
		return ETSCDayOnePrompt::None;
	}
	if (ActiveScene == 0 && !bTriedFirstCut && CanCut()) { return ETSCDayOnePrompt::Cut; }
	if (ActiveScene >= 0) { return ETSCDayOnePrompt::None; }
	if (!bAssignedWoodOnDayOne) { return ETSCDayOnePrompt::Wood; }
	if (!bAssignedFoodOnDayOne) { return ETSCDayOnePrompt::Food; }
	if (FreePlot() >= 0) { return ETSCDayOnePrompt::Build; }
	return ETSCDayOnePrompt::None;
}

bool ATSCGameMode::IsFirstCutFramed() const
{
	return bFramingFirstCut && (bCutOpen || TransitionTimer > 0.f);
}

void ATSCGameMode::PressCut()
{
	if (!CanCut()) { return; }
	bCutOpen = true;
	SelectedTake = 0;
	bFramingFirstCut = bDayOneOnboarding && ActiveScene == 0;
	if (bFramingFirstCut)
	{
		bTriedFirstCut = true;
		SetCameraView(CellF(18.f, 16.5f), 8000.f);
	}
}

void ATSCGameMode::ConfirmCut()
{
	if (!bCutOpen) { return; }
	bCutOpen = false;
	if (SelectedTake == 1 && ActiveScene >= 0)
	{
		const int32 Idx = ActiveScene;
		--EditsLeft;
		for (FTSCScene& S : Scenes) { if (S.Timer < 0.f) { S.Timer = 0.f; } }
		ApplyAshen(Idx);
		FinishScene(Idx);
		if (bFramingFirstCut)
		{
			Toast(FString::Printf(TEXT("%d cuts left"), EditsLeft));
		}
	}
}

// =====================================================================================================================
// Final night
// =====================================================================================================================

void ATSCGameMode::SetGate(bool bClosed)
{
	bGateClosed = bClosed;
	SetGateCells(bClosed);
}

void ATSCGameMode::SetGateCells(bool bBar)
{
	// The two gate cells never block the People layer: villagers open the gate and lock it behind them. The bar stops only
	// raiders and the fog-born (TSCNavLayer::Hostile); Blocked[] keeps it for the game's own checks.
	for (int32 Y = GateY; Y <= GateY + 1; ++Y)
	{
		if (InGrid(GateX, Y)) { Blocked[Y * GW + GateX] = bBar ? 1 : 0; }
		Nav.SetCoarse(GateX, Y, false);
		Nav.SetGateBar(GateX, Y, bBar);
	}
	++GridVersion;
}

void ATSCGameMode::StartNight()
{
	if (bClearingSpruce) { FinishSpruceClearing(); }
	Phase = ETSCPhase::Night;
	PlayMusic(NightMusic);
	NightTime = 0.f;
	DayFlags = 0;
	SetSpruceFallen(false);
	for (FTSCPlot& P : Plots) { P.Marker->SetActorHiddenInGame(true); }
	for (ATSCUnit* U : Units) { if (U && U->Team != ETSCTeam::Villager && U->IsAlive()) { U->bGone = true; U->SetActorHiddenInGame(true); } }
	RaiderBoat->SetActorHiddenInGame(true);

	SetGate(true);
	StartNightWood = Wood;
	const float Spent = FMath::Min(Wood, 300.f);
	Wood -= Spent;
	GateMaxHP = GateHP = 150.f + Spent;
	HallMaxHP = HallHP = 300.f + (Scenes[0].bAshen ? 120.f : 0.f);
	bHungryNight = Food < 100.f;
	bFedNight = Food >= 200.f;
	const float HpMul = bHungryNight ? 0.72f : (bFedNight ? 1.15f : 1.f);
	const float DmgMul = bHammerBlessing ? 1.3f : 1.f;

	auto Arm = [&](ATSCUnit* U, const FVector& Pos)
	{
		U->bDefender = true;
		U->bFights = true;
		U->bInvulnerable = false;
		U->Need = ETSCNeed::None;
		U->SetArmed(true);
		U->MaxHP *= HpMul;
		U->HP = FMath::Min(U->HP * HpMul, U->MaxHP);
		U->Damage *= DmgMul;
		U->OrderHold(Pos);
		U->BarTimer = 999.f;
	};
	if (Hunter->IsAlive()) { Hunter->HP = Hunter->MaxHP; Arm(Hunter, CellF(9.9f, 12.f)); }
	if (Woodcutter->IsAlive()) { Arm(Woodcutter, CellF(10.4f, 11.2f)); }
	const bool bBound = Shield->IsAlive() && bShieldWounded && Healer->IsAlive();
	if (Shield->IsAlive())
	{
		Shield->HP = Shield->MaxHP * (!bShieldWounded ? 1.f : (bBound ? 0.8f : 0.45f));
		Arm(Shield, CellF(25.8f, 12.f));
	}
	Workers.RemoveAll([](const TObjectPtr<ATSCUnit>& U) { return !U || !U->IsAlive(); });
	const int32 NumDef = FMath::Min(Workers.Num(), 6 + HousesBuilt);
	for (int32 I = 0; I < Workers.Num(); ++I)
	{
		ATSCUnit* U = Workers[I];
		if (I < NumDef)
		{
			const int32 K = I / 2;
			if (I % 2 == 0) { Arm(U, CellF(10.6f + (K % 2) * 0.6f, 11.f + K * 0.7f)); }
			else { Arm(U, CellF(25.2f - (K % 2) * 0.6f, 11.f + K * 0.7f)); }
		}
		else
		{
			U->Need = ETSCNeed::None;
			U->OrderShelter(HallDoor() + FVector((I % 3 - 1) * 60.f, 0.f, 0.f));
		}
	}
	if (Healer->IsAlive()) { Healer->OrderHold(CellF(17.f, 14.4f)); }

	NightVillagersAtStart = 0;
	for (const ATSCUnit* U : Units) { if (U && U->Team == ETSCTeam::Villager && U->IsAlive()) { ++NightVillagersAtStart; } }
	NightDefendersLost = 0;
	EnemiesSlain = 0;
	const int32 Pack = Scenes[1].bAshen ? 0 : 3;                          // the fog-born that killed Hakon lead others in
	const int32 Calm = bStagBlessing ? (Scenes[2].bAshen ? 0 : 1) : 0;
	CreatureWaves[0] = CreatureWaves[1] = 3 + Pack - Calm;
	CreatureWaves[2] = 4 + Pack - (bStagBlessing ? 1 : 0);
	RaiderWaves[0] = Scenes[3].bAshen ? 2 : 3;
	RaiderWaves[1] = Scenes[3].bAshen ? 1 : 3;
	ShowBanner(TEXT("The Final Night.  The gate is barred with the last of the timber. Creatures gather in the fog; raiders pull for the shore."), 12.f);
	Toast(TSCLoc::Tr(FString::Printf(TEXT("%d wood braces the gate (%d strength)."), FMath::RoundToInt(Spent), FMath::RoundToInt(GateMaxHP)))
		+ TSCLoc::Tr(bHungryNight ? TEXT(" Your defenders are weak with hunger.") : (bFedNight ? TEXT(" Your defenders are well fed.") : TEXT("")))
		+ TSCLoc::Tr(bHammerBlessing ? TEXT(" The Hammer blesses their axes.") : TEXT(""))
		+ TSCLoc::Tr(Scenes[0].bAshen ? TEXT(" The old stone wall shields the longhouse door.") : TEXT("")));
	Toast(Scenes[1].bAshen ? TEXT("Hakon's bow covers the west gate.")
		: TEXT("No bow answers at the west gate. The fog-born that killed Hakon lead the others in."));
	Toast(Healer->IsAlive() ? (bBound ? TEXT("Sigrun waits by the hearth with clean bandages. She has bound Torvald's wounds.") : TEXT("Sigrun waits by the hearth with clean bandages."))
		: (ExtraHealers > 0 ? TEXT("Sigrun is gone, but the healer's hut tends the wounded in her place.") : TEXT("No healer waits by the hearth. Every wound tonight stays open.")));
	Toast(Scenes[3].bAshen ? TEXT("Torvald stands whole on the empty shore. Only three raiders can wade in.")
		: TEXT("Six raiders pull for the dock. The scouts told them how few spears wait there."));
	UE_LOG(LogSaga, Display, TEXT("[Saga] NIGHT gate=%d hall=%d food=%d hpmul=%.2f creatures=%d/%d/%d raiders=%d/%d villagers=%d"),
		FMath::RoundToInt(GateMaxHP), FMath::RoundToInt(HallMaxHP), FMath::FloorToInt(Food), HpMul,
		CreatureWaves[0], CreatureWaves[1], CreatureWaves[2], RaiderWaves[0], RaiderWaves[1], NightVillagersAtStart);
}

void ATSCGameMode::GetAssaultGoal(const ATSCUnit* U, FVector& OutPos, ETSCStructure& OutStruct) const
{
	const float J = ((int32)(U->GetUniqueID() % 5) - 2) * 34.f;
	if (U->Team == ETSCTeam::Creature && bGateClosed && !bGateBroken)
	{
		OutPos = CellF(7.55f, 12.f) + FVector(((U->GetUniqueID() / 5) % 3) * -60.f, J * 1.6f, 0.f);
		OutStruct = ETSCStructure::Gate;
		return;
	}
	OutPos = HallDoor() + FVector(J * 3.f, ((U->GetUniqueID() / 5) % 2) * 50.f, 0.f);
	OutStruct = ETSCStructure::Longhouse;
}

void ATSCGameMode::DamageStructure(ETSCStructure S, float Amount)
{
	if (Phase != ETSCPhase::Night) { return; }
	if (S == ETSCStructure::Gate)
	{
		if (bGateBroken) { return; }
		GateHP -= Amount * 2.4f;
		if (GateHP <= 0.f)
		{
			GateHP = 0.f;
			bGateBroken = true;
			bWestBreached = true;
			SetGateCells(false);
			Toast(TEXT("The west gate splinters! The fog-born pour through."));
		}
	}
	else if (S == ETSCStructure::Longhouse)
	{
		HallHP -= Amount;
		if (!bEastBreached && !bWestBreached) { bEastBreached = true; }
		if (HallHP <= 0.f)
		{
			HallHP = 0.f;
			EndNight(true);
		}
	}
}

void ATSCGameMode::OnUnitDied(ATSCUnit* U)
{
	if (U == Hunter && IsValid(HunterDog.Get()) && HunterDog->IsAlive() && !HunterDog->IsHidden() && DogHowl)
	{
		// A story beat: let the player hear the companion's howl even while looking elsewhere.
		UGameplayStatics::PlaySound2D(this, DogHowl, 0.85f);
	}
	if (U->Team == ETSCTeam::Villager)
	{
		if (U->bNamed && Phase == ETSCPhase::Night) { Toast(TSCLoc::Fmt(TEXT("{0} has fallen."), { TSCLoc::Tr(U->DisplayName) })); }
		if (Phase == ETSCPhase::Night) { ++NightDefendersLost; }
		else { UpdateJobs(); UE_LOG(LogSaga, Display, TEXT("[Saga] DAYDEATH %s scene=%d at %s"), *U->GetName(), ActiveScene, *U->GetActorLocation().ToString()); }
	}
	else if (Phase == ETSCPhase::Night)
	{
		++EnemiesSlain;
	}
	else if (U->Team != ETSCTeam::Animal)
	{
		UE_LOG(LogSaga, Display, TEXT("[Saga] DAYKILL %s scene=%d at %s"), *U->GetName(), ActiveScene, *U->GetActorLocation().ToString());
	}
}

void ATSCGameMode::SimNight(float Dt)
{
	NightTime += Dt;
	auto Once = [this](int32 Bit, float T) { if (NightTime >= T && !(DayFlags & (1 << Bit))) { DayFlags |= 1 << Bit; return true; } return false; };

	// creatures: three waves out of the fog. The Stag's blessing thins them.
	auto CreatureWave = [&](int32 N)
	{
		for (int32 I = 0; I < N; ++I)
		{
			ATSCUnit* C = SpawnUnit(ETSCRole::Creature, FString(), CellF(-3.f - (I / 3) * 1.4f, 10.2f + (I % 4) * 0.9f));
			C->MaxHP = C->HP = 50.f;
			C->Damage = 8.f;
			C->OrderAssault();
		}
	};
	if (Once(0, 5.f)) { CreatureWave(CreatureWaves[0]); Toast(TEXT("Pale shapes drift out of the fog toward the palisade.")); }
	if (Once(1, 24.f)) { CreatureWave(CreatureWaves[1]); }
	if (Once(2, 44.f)) { CreatureWave(CreatureWaves[2]); }

	// raiders: they land at the dock, or wade ashore if the dock is ash
	const bool bDockAshen = Scenes[3].bAshen;
	auto RaiderWave = [&](int32 N)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const FVector P = bDockAshen ? CellF(29.6f, 9.f + I * 1.4f) : CellF(31.5f, 11.3f + (I % 3) * 0.5f);
			ATSCUnit* R = SpawnUnit(ETSCRole::Raider, FString(), P);
			if (bDockAshen) { R->Speed *= 0.55f; R->HP = R->MaxHP = R->MaxHP * 0.85f; }
			R->OrderAssault();
		}
	};
	if (Once(3, 9.f)) { if (Ocean) { Ocean->PlayLanding(FVector2D(CellF(bDockAshen ? 29.6f : 31.5f, 11.5f)), 0.8f); } RaiderWave(RaiderWaves[0]); Toast(bDockAshen ? TEXT("With no dock to land at, a few raiders wade ashore through the surf.") : TEXT("Raiders leap onto the dock!")); }
	if (Once(4, 34.f)) { RaiderWave(RaiderWaves[1]); }

	// healing
	const float Heal = (Healer->IsAlive() ? 2.6f : 0.f) + ExtraHealers * 1.6f;
	int32 EnemiesAlive = 0;
	for (ATSCUnit* U : Units)
	{
		if (!U || !U->IsAlive()) { continue; }
		if (U->Team == ETSCTeam::Villager)
		{
			if (U->bDefender && Heal > 0.f && U->HP < U->MaxHP) { U->HP = FMath::Min(U->MaxHP, U->HP + Heal * Dt); }
		}
		else
		{
			++EnemiesAlive;
		}
	}
	// once enemies are at the longhouse door, every defender falls back to it
	if (HallHP < HallMaxHP && Once(5, 0.f))
	{
		int32 K = 0;
		for (ATSCUnit* U : Units)
		{
			if (U && U->IsAlive() && U->bDefender) { U->OrderHold(HallDoor() + FVector((K % 5 - 2) * 70.f, 60.f + (K / 5) * 60.f, 0.f)); ++K; }
		}
		Toast(TEXT("They are at the longhouse door! Every defender falls back to the hearth."));
	}

	const bool bAllSpawned = NightTime > 46.f;
	if (bAllSpawned && EnemiesAlive == 0 && Once(7, 0.f))
	{
		DayFlags |= 1 << 8; NightTime = FMath::Max(NightTime, NightLength - 3.f); Toast(TEXT("The last of them falls. The night goes quiet."));
#if !UE_BUILD_SHIPPING
		TSCNavStats();
#endif
	}
	if (NightTime >= NightLength)
	{
		EndNight(false);
	}
}

void ATSCGameMode::EndNight(bool bHallFell)
{
	if (Phase != ETSCPhase::Night) { return; }
	for (ATSCUnit* U : Units)
	{
		if (U && U->IsAlive() && U->Team != ETSCTeam::Villager) { U->bFights = false; U->OrderLeave(U->Team == ETSCTeam::Creature ? CellF(-5.f, 12.f) : CellF(31.5f, 12.f)); }
	}
	BuildEpilogue(bHallFell);
	Phase = ETSCPhase::Epilogue;
	if (!bHallFell && VictoryMusic)
	{
		// Victory sting, then the calm theme under the epilogue.
		PlayMusic(VictoryMusic);
		GetWorldTimerManager().SetTimer(MusicTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { PlayMusic(MenuMusic); }),
			FMath::Max(1.f, VictoryMusic->GetDuration()), false);
	}
	else
	{
		PlayMusic(MenuMusic);
	}
#if !UE_BUILD_SHIPPING
	TSCNavStats(); // the 30 s stats timer runs on real time and misses the night
#endif
	for (const FString& L : Epilogue) { UE_LOG(LogSaga, Display, TEXT("[Epilogue] %s"), *L); }
}

void ATSCGameMode::BuildEpilogue(bool bHallFell)
{
	Epilogue.Reset();
	int32 Survivors = 0;
	for (const ATSCUnit* U : Units) { if (U && U->Team == ETSCTeam::Villager && U->IsAlive()) { ++Survivors; } }
	const int32 NumAshen = (Scenes[0].bAshen ? 1 : 0) + (Scenes[1].bAshen ? 1 : 0) + (Scenes[2].bAshen ? 1 : 0) + (Scenes[3].bAshen ? 1 : 0);
	const bool bHakon = Hunter->IsAlive(), bUlf = Woodcutter->IsAlive(), bSigrun = Healer->IsAlive(), bTorvald = Shield->IsAlive();
	const int32 NamedAlive = (bHakon ? 1 : 0) + (bUlf ? 1 : 0) + (bSigrun ? 1 : 0) + (bTorvald ? 1 : 0);
	const int32 CutsUsed = MaxEdits - EditsLeft;
	const int32 Lost = NightDefendersLost;

	enum class EEnd { Cold, Bitter, Ashen, Long, Green, Two };
	const EEnd End = bHallFell ? EEnd::Cold
		: (NamedAlive <= 2 || Lost * 2 >= NightVillagersAtStart) ? EEnd::Bitter
		: NamedAlive == 4 ? EEnd::Long
		: NumAshen >= 3 ? EEnd::Ashen
		: NumAshen <= 1 ? EEnd::Green : EEnd::Two;
	switch (End)
	{
	case EEnd::Cold:   EpilogueTitle = TEXT("THE SAGA OF THE COLD HEARTH"); break;
	case EEnd::Bitter: EpilogueTitle = TEXT("THE SAGA OF THE BITTER DAWN"); break;
	case EEnd::Ashen:  EpilogueTitle = TEXT("THE SAGA OF THE ASHEN HOLD"); break;
	case EEnd::Long:   EpilogueTitle = TEXT("THE SAGA OF THE LONG HEARTH"); break;
	case EEnd::Green:  EpilogueTitle = TEXT("THE SAGA OF THE GREEN CLEARING"); break;
	default:           EpilogueTitle = TEXT("THE SAGA OF TWO TELLINGS"); break;
	}

	Epilogue.Add(CutsUsed == 0
		? TEXT("Hear me, the skald. You held the knife and never cut, so I sing the saga as it was written.")
		: TEXT("Hear me, the skald, for I sing the saga you cut."));

	// the garden
	Epilogue.Add(Scenes[0].bAshen
		? TEXT("When the boar came, you gave the garden a wall of old burnt stone. Not a cabbage was lost, the thin grey soil gave a little less ever after, and on the last night that wall stood before the longhouse door.")
		: TEXT("When the boar came, you let the saga run. It trampled the green rows, the gardeners cursed, and the good soil forgave them by next morning."));

	// the gate
	if (Scenes[1].bAshen)
	{
		Epilogue.Add(bHakon
			? TEXT("You burned the western forest out of the telling so that no spruce could bar the gate. Hakon the hunter walked home through ash, and on the last night it was his bow that held that gate.")
			: TEXT("You burned the western forest out of the telling to bring Hakon home through a clear gate. He held it on the last night, and he fell holding it."));
	}
	else
	{
		Epilogue.Add(TEXT("The spruce fell across the gate and you let it lie. Hakon the hunter died on the wrong side of it, and on the last night the pale things that killed him came back and brought others."));
	}

	// the grove
	if (Scenes[2].bAshen)
	{
		Epilogue.Add(bSigrun
			? TEXT("You traded the glowing grove for Sigrun the healer. The grove stands among dead roots now, but her hands closed many wounds on the final night.")
			: TEXT("You traded the glowing grove for Sigrun the healer, and still the night took her at the longhouse door."));
	}
	else if (!Scenes[2].bStarted || bSigrun)
	{
		Epilogue.Add(TEXT("Sigrun the healer still gathers in the grove, and the grove still glows."));
	}
	else
	{
		Epilogue.Add(TEXT("You kept the grove and its glowing harvest. Sigrun the healer breathed its spores and did not rise, and on the last night the wounded lay untended."));
	}

	// the dock
	if (Scenes[3].bAshen)
	{
		Epilogue.Add(bTorvald
			? TEXT("You let the dock burn in an older war. The raiders found only black posts and came ashore few and slow, where Torvald Shield-Bearer, whole and unhurt, was waiting. No ship will ever sail from that shore again.")
			: TEXT("You let the dock burn in an older war, so the raiders came few and slow. Torvald Shield-Bearer met them in the surf and did not come back from it."));
	}
	else
	{
		Epilogue.Add(bTorvald
			? TEXT("You kept the dock and the longship. Torvald Shield-Bearer bled for it at dusk and stood again at night, and the fishing boats will go out tomorrow.")
			: TEXT("You kept the dock and the longship. Torvald Shield-Bearer, already bleeding from the scouts' axes, died on its planks when the raiders landed in force."));
	}

	// what was built
	if (BuiltNames.Num() > 0)
	{
		FString B = TSCLoc::Tr(BuiltNames[0]);
		if (BuiltNames.Num() > 1) { B += TSCLoc::Tr(TEXT(" and ")) + TSCLoc::Tr(BuiltNames[1]); }
		FString Why;
		if (bHammerBlessing) { Why += TSCLoc::Tr(TEXT(" The Hammer put weight in every axe.")); }
		if (bStagBlessing) { Why += TSCLoc::Tr(TEXT(" The Stag called some of the fog back into the trees.")); }
		if (ExtraHealers > 0) { Why += TSCLoc::Tr(TEXT(" The new hut's fires boiled bandages all night.")); }
		if (HousesBuilt > 0) { Why += TSCLoc::Tr(TEXT(" The newcomers took up shields beside the old families.")); }
		if (ExtraSawmills > 0) { Why += TSCLoc::Tr(TEXT(" Its timber went into the gate.")); }
		Epilogue.Add(TSCLoc::Fmt(TEXT("On the empty ground you raised {0}.{1}"), { B, Why }));
	}
	else
	{
		Epilogue.Add(TEXT("The marked plot stayed empty; nothing new was raised before the night came."));
	}

	// the night
	if (bGateBroken)
	{
		Epilogue.Add(FString::Printf(TEXT("The west gate, braced with %d wood, broke, and pale things drifted between the houses."), FMath::RoundToInt(FMath::Min(StartNightWood, 300.f))));
	}
	else
	{
		Epilogue.Add(FString::Printf(TEXT("The west gate, braced with %d wood, held until dawn."), FMath::RoundToInt(FMath::Min(StartNightWood, 300.f))));
	}
	if (bHungryNight) { Epilogue.Add(TEXT("They fought on empty bellies, for the stores had run thin.")); }

	if (bHallFell)
	{
		Epilogue.Add(FString::Printf(TEXT("%d fell that night, and at the end the longhouse hearth was kicked cold. %d walked away along the shore, carrying the story and little else."), Lost, Survivors));
	}
	else if (Lost == 0)
	{
		Epilogue.Add(FString::Printf(TEXT("Not one of your people fell. %d of them watched the sun come up over the water."), Survivors));
	}
	else
	{
		Epilogue.Add(FString::Printf(TEXT("%d fell that night; %d lived to see the sun come up over the water, and the hearth still burned."), Lost, Survivors));
	}

	// what the settlement became
	FString Became;
	switch (End)
	{
	case EEnd::Cold:
		Became = CutsUsed == 0
			? TEXT("You never once called CUT! The saga ran as it was written, and it was written as a lament: the clearing is a place of cold stones now.")
			: CutsUsed >= MaxEdits
			? TEXT("Even three cuts could not hold back the night. So the clearing is a place of cold stones now, and this cut of the saga ends here.")
			: TEXT("Your cuts were too few, or fell on the wrong scenes. So the clearing is a place of cold stones now, and this cut of the saga ends here.");
		break;
	case EEnd::Bitter: Became = TEXT("So the hearth still burns, but too few are left to sit around it, and no one sings a dawn like that as a victory."); break;
	case EEnd::Ashen:  Became = TEXT("So it became a hard grey hold of stone and stumps: safe, hungry, and proud of its scars."); break;
	case EEnd::Long:   Became = TEXT("So you cut only what had to be cut, and the land stayed half green and half ash. This is the telling the skalds will keep."); break;
	case EEnd::Green:  Became = TEXT("So it stayed a green and dangerous place, rich in timber and mushrooms, that buries its dead under living trees."); break;
	default:           Became = TEXT("So it became a village of two sagas, green on one side and ash on the other, and its children learned both."); break;
	}
	const TArray<FString> Names = { bHakon ? TEXT("Hakon") : TEXT(""), bUlf ? TEXT("Ulf") : TEXT(""), bSigrun ? TEXT("Sigrun") : TEXT(""), bTorvald ? TEXT("Torvald") : TEXT("") };
	FString Living;
	int32 NLiving = 0;
	for (const FString& N : Names) { if (!N.IsEmpty()) { Living += (NLiving++ ? TEXT(", ") : TEXT("")) + TSCLoc::Tr(N); } }
	Epilogue.Add(TSCLoc::Tr(Became) + (NLiving > 0 ? TSCLoc::Fmt(TEXT(" Of the four you knew by name, {0} still sit by the fire."), { Living }) : TSCLoc::Tr(TEXT(" Of the four you knew by name, none are left to hear it."))));
	Epilogue.Add(FString::Printf(TEXT("Edits used: %d of %d."), MaxEdits - EditsLeft, MaxEdits));

	static const TCHAR* EndNames[] = { TEXT("COLD_HEARTH"), TEXT("BITTER_DAWN"), TEXT("ASHEN_HOLD"), TEXT("LONG_HEARTH"), TEXT("GREEN_CLEARING"), TEXT("TWO_TELLINGS") };
	UE_LOG(LogSaga, Display, TEXT("[Saga] RESULT takes=%c%c%c%c cuts=%d ending=%s hallhp=%d gate=%s lost=%d survivors=%d named=%d hungry=%d"),
		Scenes[0].bAshen ? 'A' : 'L', Scenes[1].bAshen ? 'A' : 'L', Scenes[2].bAshen ? 'A' : 'L', Scenes[3].bAshen ? 'A' : 'L',
		CutsUsed, EndNames[(int32)End], FMath::RoundToInt(HallHP), bGateBroken ? TEXT("broken") : TEXT("held"), Lost, Survivors, NamedAlive, bHungryNight ? 1 : 0);
}

// =====================================================================================================================
// Test helpers
// =====================================================================================================================

void ATSCGameMode::TSCAuto(const FString& Takes, const FString& Build, float Speed)
{
#if !UE_BUILD_SHIPPING
	if (Takes.Equals(TEXT("TIMBERTEST"), ESearchCase::IgnoreCase)) { TestTimber(); return; }
	if (Takes.Equals(TEXT("NAVTEST"), ESearchCase::IgnoreCase)) { TestNav(); return; }
#endif
	bAuto = true;
	AutoTakes = Takes.ToUpper();
	AutoBuild = Build.ToUpper();
	SpeedMul = FMath::Clamp(Speed, 1.f, 16.f);
	UE_LOG(LogSaga, Display, TEXT("[Saga] Autoplay takes=%s build=%s speed=%.0f"), *AutoTakes, *AutoBuild, SpeedMul);
}

void ATSCGameMode::TSCLang(const FString& Lang)
{
	TSCLoc::SetRussian(Lang.Equals(TEXT("ru"), ESearchCase::IgnoreCase));
}

void ATSCGameMode::TSCLocMisses()
{
	TSCLoc::DumpMisses();
}

void ATSCGameMode::TSCCut(int32 Take, int32 bConfirm)
{
	PressCut();
	SelectTake(Take);
	if (bConfirm) { ConfirmCut(); }
}

void ATSCGameMode::TSCSkip()
{
	if (Phase == ETSCPhase::Day && ActiveScene < 0) { DayTime += 20.f; }
}

void ATSCGameMode::AutoPlay(float Dt)
{
	if (Phase == ETSCPhase::Title) { StartGame(); return; }
	if (Phase == ETSCPhase::Epilogue) { bAuto = false; SpeedMul = 1.f; return; }
	if (CanCut() && Scenes[ActiveScene].Timer > 2.f && AutoTakes.IsValidIndex(ActiveScene) && AutoTakes[ActiveScene] == TEXT('A'))
	{
		PressCut();
		SelectTake(1);
		ConfirmCut();
	}
	if (AutoBuild.Len() > 0 && Phase == ETSCPhase::Day)
	{
		const TCHAR C = AutoBuild[0];
		if (C != TEXT('H') && C != TEXT('M') && C != TEXT('E') && C != TEXT('S') && C != TEXT('T')) { AutoBuild.RightChopInline(1); return; }
		const EBuilding T = C == TEXT('H') ? EBuilding::House : C == TEXT('M') ? EBuilding::Sawmill : C == TEXT('E') ? EBuilding::Healer : C == TEXT('S') ? EBuilding::HammerBlessing : EBuilding::StagBlessing;
		if (CanBuild(T)) { StartBuild(T); AutoBuild.RightChopInline(1); }
	}
}

// Timber claims are acquired and completed on the game thread, before another unit can select the actor.
ATSCPrimActor* ATSCGameMode::ReserveTimber(ATSCUnit* Worker, FVector& OutSpot)
{
	check(IsInGameThread());
	if (!IsValid(Worker) || !Worker->IsTimberWorker()) { return nullptr; }
	FTSCTimberTree* Best = nullptr;
	float BestCost = MAX_flt;
	TArray<float> Reach;
	Nav.Flood(Worker->Loc2D(), TSCNavLayer::People, Reach);
	for (FTSCTimberTree& T : TimberTrees)
	{
		ATSCPrimActor* Tree = T.Tree.Get();
		if (!Tree || Tree->IsHidden() || T.Stage >= 2) { continue; }
		ATSCUnit* Claimant = T.Owner.Get();
		if (Claimant && !Claimant->IsTimberWorker()) { T.Owner.Reset(); Claimant = nullptr; }
		if (Claimant == Worker) { OutSpot = T.WorkSpot; return Tree; }
		if (Claimant) { continue; }
		// An exact free neighbour is required: FindPath otherwise snaps a blocked goal to another cell.
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				if ((DX == 0 && DY == 0) || IsBlocked(T.Cell.X + DX, T.Cell.Y + DY)) { continue; }
				const FVector Spot = T.Base + FVector(DX * CELL * 0.65f, DY * CELL * 0.65f, 0.f);
				const FIntPoint C = WorldToCell(Spot);
				if (IsBlocked(C.X, C.Y)) { continue; }
				if (Nav.IsBlockedAt(Spot, TSCNavLayer::People)) { continue; }
				const float Cost = Nav.CostAt(Reach, Spot);
				if (Cost >= MAX_flt) { continue; }
				if (Cost < BestCost) { BestCost = Cost; Best = &T; OutSpot = Spot; }
			}
		}
	}
	if (!Best) { return nullptr; }
	Best->Owner = Worker;
	Best->WorkSpot = OutSpot;
	return Best->Tree.Get();
}

bool ATSCGameMode::ValidateTimber(ATSCUnit* Worker, ATSCPrimActor* Tree, FVector& OutSpot) const
{
	if (!IsValid(Worker) || !Worker->IsTimberWorker() || !IsValid(Tree) || Tree->IsHidden()) { return false; }
	for (const FTSCTimberTree& T : TimberTrees)
	{
		if (T.Tree.Get() != Tree) { continue; }
		const FIntPoint C = WorldToCell(T.WorkSpot);
		if (T.Owner.Get() != Worker || T.Stage >= 2 || IsBlocked(C.X, C.Y)) { return false; }
		OutSpot = T.WorkSpot;
		return true;
	}
	return false;
}

void ATSCGameMode::ReleaseTimber(ATSCUnit* Worker)
{
	for (FTSCTimberTree& T : TimberTrees) { if (T.Owner.Get() == Worker) { T.Owner.Reset(); } }
}

int32 ATSCGameMode::CompleteTimberStage(ATSCUnit* Worker, ATSCPrimActor* Tree)
{
	FVector Spot;
	if (!ValidateTimber(Worker, Tree, Spot) || FVector::Dist2D(Worker->Loc2D(), Spot) > 40.f) { ReleaseTimber(Worker); return 0; }
	for (FTSCTimberTree& T : TimberTrees)
	{
		if (T.Tree.Get() != Tree) { continue; }
		++T.Stage;
		if (T.Stage == 1)
		{
			T.FallDir = (T.Base - Worker->Loc2D()).GetSafeNormal2D();
			T.FallTime = 0.f;
			SetBlocked(T.Cell.X, T.Cell.Y, false);
		}
		else
		{
			Tree->SetActorHiddenInGame(true);
			Tree->SetActorEnableCollision(false);
			T.Owner.Reset();
		}
		return T.Stage;
	}
	return 0;
}

void ATSCGameMode::SimTimber(float Dt)
{
	for (FTSCTimberTree& T : TimberTrees)
	{
		ATSCPrimActor* Tree = T.Tree.Get();
		if (!Tree || Tree->IsHidden())
		{
			if (T.Stage < 2) { SetBlocked(T.Cell.X, T.Cell.Y, false); T.Stage = 2; }
			T.Owner.Reset();
			continue;
		}
		if (T.Owner.IsValid() && !T.Owner->IsTimberWorker()) { T.Owner.Reset(); }
		if (T.Stage != 1) { continue; }
		T.FallTime = FMath::Min(1.3f, T.FallTime + Dt);
		const float A = T.FallTime / 1.3f;
		const float Ang = A * A * HALF_PI * 0.985f;
		const FVector Up = T.FallDir * FMath::Sin(Ang) + FVector::UpVector * FMath::Cos(Ang);
		Tree->SetActorRotation(FQuat::FindBetweenNormals(FVector::UpVector, Up) * T.Rotation);
	}
}

void ATSCGameMode::TestTimber()
{
#if !UE_BUILD_SHIPPING
	// Run only in a disposable PIE/game world; these checks use actual generated trees and units.
	if (Phase == ETSCPhase::Title) { StartGame(); }
	Phase = ETSCPhase::Day;
	int32 Passed = 0, Failed = 0;
	auto Check = [&](bool bOK, const TCHAR* Name)
	{
		if (bOK) { ++Passed; } else { ++Failed; }
		UE_LOG(LogSaga, Display, TEXT("[TimberTest] %s %s"), bOK ? TEXT("PASS") : TEXT("FAIL"), Name);
	};
	ATSCUnit* A = SpawnUnit(ETSCRole::Worker, TEXT("TimberTestA"), CellF(12.f, 7.f));
	ATSCUnit* B = SpawnUnit(ETSCRole::Worker, TEXT("TimberTestB"), CellF(12.f, 7.f));
	for (ATSCUnit* U : { A, B }) { U->SetJob(ETSCJob::Wood); U->OrderWork(CellF(7.f, 9.f), CellF(12.f, 7.f)); }
	FVector PA, PB;
	ATSCPrimActor* TA = ReserveTimber(A, PA);
	ATSCPrimActor* TB = ReserveTimber(B, PB);
	Check(TA && TB && TA != TB, TEXT("competing workers reserve distinct reachable trees"));
	if (!TA || !TB) { A->Destroy(); B->Destroy(); return; }
	Check(ReserveTimber(A, PA) == TA, TEXT("acquire is idempotent"));
	Check(CompleteTimberStage(B, TA) == 0, TEXT("another worker cannot complete a claimed tree"));
	A->Teleport(PA); // teleport cancels reservations, then the real Sim selects again
	A->Sim(0.1f);
	TA = A->TimberTarget.Get();
	Check(TA != nullptr, TEXT("real worker selects a target"));
	const float WoodBefore = Wood;
	int32 Guard = 0;
	while (TA && !TA->IsHidden() && Guard++ < 500)
	{
		A->Sim(0.1f); SimTimber(0.1f);
		for (const FTSCTimberTree& T : TimberTrees)
		{
			if (T.Tree.Get() == TA && T.Stage == 1 && T.FallTime >= 1.3f)
			{
				Check(T.Owner.Get() == A && !TA->IsHidden() && FVector::DotProduct(TA->GetActorUpVector(), FVector::UpVector) < 0.1f,
					TEXT("first completed stage falls and remains visible with the same owner"));
				Guard = 0;
				while (!TA->IsHidden() && Guard++ < 100) { A->Sim(0.1f); SimTimber(0.1f); }
				break;
			}
		}
	}
	Check(TA && TA->IsHidden() && A->WorkPhase == 1 && !A->TimberTarget.IsValid(), TEXT("second stage removes tree and begins carry"));
	Check(Wood == WoodBefore, TEXT("no duplicate award outside the rate economy"));
	Guard = 0;
	while (!A->TimberTarget.IsValid() && Guard++ < 800) { A->Sim(0.1f); SimTimber(0.1f); }
	Check(A->TimberTarget.IsValid() && A->TimberTarget.Get() != TA, TEXT("delivery completes and worker selects next tree"));
	ATSCPrimActor* Next = A->TimberTarget.Get();
	A->SetJob(ETSCJob::Food);
	FVector Dummy;
	Check(!A->TimberTarget.IsValid() && !ValidateTimber(A, Next, Dummy), TEXT("job change releases claim"));
	// Cancellation must make the actual owner slot reusable, not just clear the worker's pointer.
	auto HasClaim = [&](ATSCUnit* U)
	{
		for (const FTSCTimberTree& T : TimberTrees) { if (T.Owner.Get() == U) { return true; } }
		return false;
	};
	A->SetJob(ETSCJob::Wood);
	A->OrderWork(CellF(7.f, 9.f), CellF(12.f, 7.f));
	A->Sim(1.1f);
	Check(HasClaim(A), TEXT("worker reacquires after job change"));
	A->OrderHold(A->Loc2D());
	Check(!HasClaim(A), TEXT("hold order frees owner slot"));
	A->OrderWork(CellF(7.f, 9.f), CellF(12.f, 7.f));
	A->Sim(1.1f);
	Next = A->TimberTarget.Get();
	if (ValidateTimber(A, Next, Dummy))
	{
		const FIntPoint C = WorldToCell(Dummy);
		SetBlocked(C.X, C.Y, true);
		A->Sim(0.1f);
		bool bOldOwned = false;
		for (const FTSCTimberTree& T : TimberTrees) { if (T.Tree.Get() == Next && T.Owner.Get() == A) { bOldOwned = true; } }
		Check(!bOldOwned, TEXT("newly blocked work spot releases old target"));
		SetBlocked(C.X, C.Y, false);
	}
	else { Check(false, TEXT("reachable target for blocking check")); }
	A->CancelTimber();
	A->Sim(1.1f);
	Next = A->TimberTarget.Get();
	if (Next) { Next->Destroy(); }
	SimTimber(0.1f);
	A->Sim(0.1f);
	bool bDestroyedClaim = false;
	for (const FTSCTimberTree& T : TimberTrees) { if (!T.Tree.IsValid() && T.Owner.IsValid()) { bDestroyedClaim = true; } }
	Check(Next && !bDestroyedClaim, TEXT("destroyed target cannot leave a reservation"));
	A->OrderShelter(A->Loc2D());
	Check(!HasClaim(A), TEXT("shelter cancels timber work"));
	B->Sim(0.1f);
	TB = B->TimberTarget.Get();
	B->Kill();
	Check(!B->TimberTarget.IsValid() && !ValidateTimber(B, TB, Dummy), TEXT("death releases claim"));
	A->Destroy(); B->Destroy();
	UE_LOG(LogSaga, Display, TEXT("[TimberTest] RESULT passed=%d failed=%d"), Passed, Failed);
#endif
}

// =====================================================================================================================
// Nav dev checks
// =====================================================================================================================

void ATSCGameMode::TSCNavStats()
{
#if !UE_BUILD_SHIPPING
	UE_LOG(LogSaga, Display, TEXT("[Nav] STATS violations=%d deepOverlaps=%d maxPen=%.0f"), NavViolations, NavDeepOverlaps, NavMaxPen);
#endif
}

void ATSCGameMode::CheckNav()
{
#if !UE_BUILD_SHIPPING
	TArray<ATSCUnit*, TInlineAllocator<64>> Act;
	for (ATSCUnit* U : Units) { if (TSCIsNavActive(U)) { Act.Add(U); } }
	for (ATSCUnit* U : Wildlife) { if (TSCIsNavActive(U)) { Act.Add(U); } }
	if (NavCheckVersion != GridVersion)
	{
		// The map changed under the units (gate, building, felled timber): not a violation.
		for (ATSCUnit* U : Act) { U->bNavWasBlocked = Nav.IsBlockedAt(U->Loc2D(), U->NavLayer()); }
		NavCheckVersion = GridVersion;
		return;
	}
	for (ATSCUnit* U : Act)
	{
		const bool bIn = Nav.IsBlockedAt(U->Loc2D(), U->NavLayer());
		if (bIn && !U->bNavWasBlocked)
		{
			++NavViolations;
			if (NavViolations <= 30)
			{
				UE_LOG(LogSaga, Warning, TEXT("[Nav] VIOLATION %s role=%d entered a blocked cell at %s"), *U->GetName(), (int32)U->Role, *U->Loc2D().ToString());
			}
		}
		U->bNavWasBlocked = bIn;
	}
	for (int32 I = 0; I < Act.Num(); ++I)
	{
		for (int32 J = I + 1; J < Act.Num(); ++J)
		{
			const float R = Act[I]->NavRadius + Act[J]->NavRadius;
			const float D = FVector::Dist2D(Act[I]->Loc2D(), Act[J]->Loc2D());
			if (D < 0.5f * R)
			{
				++NavDeepOverlaps;
				NavMaxPen = FMath::Max(NavMaxPen, R - D);
			}
		}
	}
	if (WorldTime - NavStatsTime >= 30.f)
	{
		NavStatsTime = WorldTime;
		TSCNavStats();
	}
#endif
}

void ATSCGameMode::DrawNavDebug()
{
#if !UE_BUILD_SHIPPING
	UWorld* World = GetWorld();
	if (!World) { return; }
	const int32 W = Nav.Width(), H = Nav.Height();
	const int32 L = TSCNavLayer::People;
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			if (!Nav.IsBlockedCell(X, Y, L)) { continue; }
			const bool bEdge = !Nav.IsBlockedCell(X + 1, Y, L) || !Nav.IsBlockedCell(X - 1, Y, L) || !Nav.IsBlockedCell(X, Y + 1, L) || !Nav.IsBlockedCell(X, Y - 1, L);
			if (!bEdge) { continue; }
			FVector C = Nav.CellCenter(X, Y);
			C.Z = GroundReliefAt(C) + 20.f;
			DrawDebugBox(World, C, FVector(24.f, 24.f, 2.f), Nav.IsPropCell(X, Y) ? FColor::Magenta : FColor::Red, false, 0.f);
		}
	}
	auto DrawUnit = [&](const ATSCUnit* U)
	{
		if (!TSCIsNavActive(U)) { return; }
		FVector C = U->Loc2D();
		C.Z = GroundReliefAt(C) + 20.f;
		DrawDebugCircle(World, C, U->NavRadius, 24, FColor::White, false, 0.f, 0, 1.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
		FVector Prev = C;
		for (int32 I = U->PathIdx; I < U->Path.Num(); ++I)
		{
			FVector P = U->Path[I];
			P.Z = C.Z;
			DrawDebugLine(World, Prev, P, FColor::Green, false, 0.f);
			Prev = P;
		}
	};
	for (const ATSCUnit* U : Units) { DrawUnit(U); }
	for (const ATSCUnit* U : Wildlife) { DrawUnit(U); }
#endif
}

void ATSCGameMode::TestNav()
{
#if !UE_BUILD_SHIPPING
	if (Phase == ETSCPhase::Title) { StartGame(); }
	Phase = ETSCPhase::Day;
	int32 Passed = 0, Failed = 0;
	auto Check = [&](bool bOK, const TCHAR* Name)
	{
		if (bOK) { ++Passed; } else { ++Failed; }
		UE_LOG(LogSaga, Display, TEXT("[NavTest] %s %s"), bOK ? TEXT("PASS") : TEXT("FAIL"), Name);
	};
	const int32 L = TSCNavLayer::People;
	auto Run = [&](const TArray<ATSCUnit*>& Us, float Seconds, TFunctionRef<void()> Each)
	{
		for (float T = 0.f; T < Seconds; T += 0.05f)
		{
			for (ATSCUnit* U : Us) { U->Sim(0.05f); }
			SeparateUnits(0.05f);
			Each();
		}
	};
	auto Spawn = [&](const FVector& Pos, const FVector& Goal)
	{
		ATSCUnit* U = SpawnUnit(ETSCRole::Worker, TEXT("NavTest"), Pos);
		U->OrderHold(Goal);
		return U;
	};
	auto Remove = [&](const TArray<ATSCUnit*>& Us)
	{
		for (ATSCUnit* U : Us) { Units.Remove(U); U->Destroy(); }
	};

	// 1. path goes around a building
	{
		TArray<FVector> P;
		const FVector Goal = CellF(23.5f, 6.f);
		const bool bFound = FindPath(CellF(20.5f, 6.f), Goal, P, L);
		bool bOK = bFound && P.Num() >= 2 && FVector::Dist2D(P.Last(), Goal) <= 1.f;
		FVector Prev = CellF(20.5f, 6.f);
		for (const FVector& Pt : P)
		{
			if (Nav.IsBlockedAt(Pt, L) || !Nav.LineFree(Prev, Pt, L))
			{
				bOK = false;
				UE_LOG(LogSaga, Display, TEXT("[NavTest]   leg %s -> %s blockedEnd=%d lineFree=%d"), *Prev.ToString(), *Pt.ToString(), Nav.IsBlockedAt(Pt, L) ? 1 : 0, Nav.LineFree(Prev, Pt, L) ? 1 : 0);
			}
			Prev = Pt;
		}
		UE_LOG(LogSaga, Display, TEXT("[NavTest]   path found=%d num=%d lastDist=%.1f startBlocked=%d"), bFound ? 1 : 0, P.Num(), P.Num() ? FVector::Dist2D(P.Last(), Goal) : -1.f, Nav.IsBlockedAt(CellF(20.5f, 6.f), L) ? 1 : 0);
		Check(bOK, TEXT("path goes around a building"));
	}
	// 2. blocked goal snaps to free ground
	{
		TArray<FVector> P;
		const FVector Goal = CellF(22.f, 6.f);
		const bool bFound = FindPath(CellF(20.5f, 6.f), Goal, P, L);
		Check(bFound && P.Num() > 0 && !Nav.IsBlockedAt(P.Last(), L) && FVector::Dist2D(P.Last(), Goal) <= 250.f, TEXT("blocked goal snaps to free ground"));
	}
	// 3. closed gate has no way round
	{
		const bool bWasClosed = bGateClosed;
		if (!bWasClosed) { SetGateCells(true); }
		TArray<FVector> P;
		// the barred gate stops the enemy layer, but a villager walks through it
		const bool bFound = FindPath(CellF(10.5f, 12.f), CellF(5.f, 12.f), P, TSCNavLayer::Hostile);
		Check(!bFound && P.Num() > 0 && IsInsideSettlement(P.Last()), TEXT("closed gate has no way round"));
		TArray<FVector> PV;
		Check(FindPath(CellF(10.5f, 12.f), CellF(5.f, 12.f), PV, L) && !PV.IsEmpty() && !IsInsideSettlement(PV.Last()), TEXT("villagers pass the closed gate"));
		if (!bWasClosed) { SetGateCells(false); }
	}
	// 4. walker never enters a blocked cell
	{
		ATSCUnit* U = Spawn(CellF(20.5f, 6.f), CellF(23.5f, 6.f));
		bool bEntered = false;
		Run({ U }, 40.f, [&]() { if (Nav.IsBlockedAt(U->Loc2D(), L)) { bEntered = true; } });
		Check(!bEntered && U->AtHoldPos(), TEXT("walker never enters a blocked cell"));
		UE_LOG(LogSaga, Display, TEXT("[NavTest]   walker entered=%d atHold=%d pos=%s"), bEntered ? 1 : 0, U->AtHoldPos() ? 1 : 0, *U->Loc2D().ToString());
		Remove({ U });
	}
	// 5. head-on walkers pass
	{
		ATSCUnit* A = Spawn(CellF(23.f, 7.6f), CellF(25.4f, 7.6f));
		ATSCUnit* B = Spawn(CellF(25.4f, 7.6f), CellF(23.f, 7.6f));
		float MinD = MAX_flt;
		Run({ A, B }, 25.f, [&]() { MinD = FMath::Min(MinD, FVector::Dist2D(A->Loc2D(), B->Loc2D())); });
		Check(A->AtHoldPos() && B->AtHoldPos() && MinD >= 28.f, TEXT("head-on walkers pass"));
		UE_LOG(LogSaga, Display, TEXT("[NavTest]   head-on atHoldA=%d atHoldB=%d minDist=%.1f"), A->AtHoldPos() ? 1 : 0, B->AtHoldPos() ? 1 : 0, MinD);
		Remove({ A, B });
	}
	// 6. six on one spot spread out
	{
		TArray<ATSCUnit*> Six;
		const FVector Spot = CellF(24.2f, 7.6f);
		for (int32 I = 0; I < 6; ++I) { Six.Add(Spawn(Spot, Spot)); }
		Run(Six, 8.f, []() {});
		float MinD = MAX_flt;
		for (int32 I = 0; I < Six.Num(); ++I)
		{
			for (int32 J = I + 1; J < Six.Num(); ++J) { MinD = FMath::Min(MinD, FVector::Dist2D(Six[I]->Loc2D(), Six[J]->Loc2D())); }
		}
		Check(MinD >= 40.f, TEXT("six on one spot spread out"));
		UE_LOG(LogSaga, Display, TEXT("[NavTest]   six minDist=%.1f"), MinD);
		Remove(Six);
	}
	// 7. wildlife passes between trunks
	{
		bool bOK = false;
		for (const FTSCTimberTree& T : TimberTrees)
		{
			if (T.Cell.X > 5 || !T.Tree.IsValid() || T.Stage != 0) { continue; }
			if (!Nav.IsBlockedAt(T.Base, TSCNavLayer::Wildlife)) { continue; }
			const FVector C = CellCenter(T.Cell.X, T.Cell.Y);
			for (int32 K = 0; K < 4; ++K)
			{
				const FVector P = C + FVector((K & 1) ? 90.f : -90.f, (K & 2) ? 90.f : -90.f, 0.f);
				if (Nav.IsBlockedAt(P, TSCNavLayer::People) && !Nav.IsBlockedAt(P, TSCNavLayer::Wildlife)) { bOK = true; }
			}
			if (bOK) { break; }
		}
		Check(bOK, TEXT("wildlife passes between trunks"));
	}
	// 8. props are obstacles
	{
		bool bOK = Nav.NumProps() > 0;
		for (int32 I = 0; I < Nav.NumProps(); ++I)
		{
			if (!Nav.IsPropEnabled(I)) { continue; }
			const FVector2D C = Nav.PropCenter(I);
			if (!Nav.IsBlockedAt(FVector(C.X, C.Y, 0.f), L)) { bOK = false; }
		}
		Check(bOK, TEXT("props are obstacles"));
	}
	UE_LOG(LogSaga, Display, TEXT("[NavTest] RESULT passed=%d failed=%d"), Passed, Failed);
#endif
}
