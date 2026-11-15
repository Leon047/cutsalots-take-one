#pragma once
#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// The imported Landscape and the procedural colony share the same georeferenced height samples.
namespace TSCStrandvik
{
inline bool IsActive(const UWorld* World)
{
    return World && World->GetMapName().Contains(TEXT("Map_Saga_Tirreein"));
}
// Placement of the terrain under the unchanged Map_Saga village. "Terrain" coordinates are those of the original
// georeferenced binding; the Landscape actor in the map carries the same rigid transform (yaw + offset).
// World = Rot(PlacementYawDeg) * (Terrain - PlacementPivot) + PlacementOffset.
constexpr double PlacementYawDeg = 13.0;
// Old river mouth (2200, RiverY(2200) = 3893.3), moved 1300 west so the coast meets the old shoreline at the village,
// Y is the least-squares fit of the landscape's channel onto Map_Saga's river over the village reach: the mouth lies
// 800 north of Map_Saga's mouth (Y 3893 vs 4693), the middle reach up to 900 south of the old line.
inline FVector2D PlacementPivot() { return FVector2D(2385.6378, 353.1069); }
inline FVector2D PlacementOffset() { return FVector2D(900.0, 3893.3); }
inline FVector2D ToWorld(const FVector2D& T)
{
    const double A = FMath::DegreesToRadians(PlacementYawDeg), C = FMath::Cos(A), S = FMath::Sin(A);
    const FVector2D Q = T - PlacementPivot();
    return FVector2D(Q.X * C - Q.Y * S, Q.X * S + Q.Y * C) + PlacementOffset();
}
inline FVector2D ToTerrain(const FVector2D& W)
{
    const double A = FMath::DegreesToRadians(PlacementYawDeg), C = FMath::Cos(A), S = FMath::Sin(A);
    const FVector2D Q = W - PlacementOffset();
    return FVector2D(Q.X * C + Q.Y * S, -Q.X * S + Q.Y * C) + PlacementPivot();
}
// River mouth in world space: point, downstream direction and its left-hand normal.
inline FVector2D MouthPoint() { return PlacementOffset(); }
inline FVector2D MouthDir()
{
    const double A = FMath::DegreesToRadians(PlacementYawDeg), C = FMath::Cos(A), S = FMath::Sin(A);
    return FVector2D(0.97531301 * C + 0.22082688 * S, 0.97531301 * S - 0.22082688 * C);
}
inline FVector2D MouthSide() { const FVector2D D = MouthDir(); return FVector2D(-D.Y, D.X); }
inline const TArray<FVector4f>& MountainRiverRoute()
{
    static TArray<FVector4f> PointsV2;
    static bool bRouteLoadedV2 = false;
    if (!bRouteLoadedV2)
    {
        TArray<uint8> Bytes;
        if (FFileHelper::LoadFileToArray(Bytes, *(FPaths::ProjectContentDir() / TEXT("Vikings/Environment/Strandvik/Data/Strandvik_MountainRiverRoute.f32")))
            && Bytes.Num() % sizeof(FVector4f) == 0)
        {
            PointsV2.SetNumUninitialized(Bytes.Num() / sizeof(FVector4f));
            FMemory::Memcpy(PointsV2.GetData(), Bytes.GetData(), Bytes.Num());
            for (FVector4f& Point : PointsV2)
            {
                const FVector2D W = ToWorld(FVector2D(Point.X, Point.Y));
                Point.X = float(W.X); Point.Y = float(W.Y);
            }
            bRouteLoadedV2 = true;
        }
    }
    return PointsV2;
}
// Map_Saga's own river code runs on this level; only its centreline and water level follow the landscape's channel.
// The flag is refreshed whenever the ocean or the terrain is built (ATSCTerrain::RiverY / RiverZ are static).
inline bool& RiverFollowsLandscape() { static bool bFollow = false; return bFollow; }
inline void SyncRiver(const UWorld* World) { RiverFollowsLandscape() = IsActive(World) && MountainRiverRoute().Num() > 24; }
// Route sample at world X: Component 1 = centreline Y, 2 = water level. The route is monotonic in X.
inline float RouteAtX(float X, int32 Component)
{
    const auto& Points = MountainRiverRoute();
    const int32 Last = Points.Num() - 1;
    if (Last < 24) { return 0.f; }
    if (X <= Points[0].X)
    {
        if (Component == 2) { return Points[0].Z; }
        const float Slope = (Points[24].Y - Points[0].Y) / FMath::Max(Points[24].X - Points[0].X, 1.f);
        return Points[0].Y + Slope * (X - Points[0].X);
    }
    if (X >= Points[Last].X) { return Component == 2 ? FMath::Max(Points[Last].Z, 3.f) : Points[Last].Y; }
    int32 Lo = 0, Hi = Last;
    while (Hi - Lo > 1)
    {
        const int32 Mid = (Lo + Hi) / 2;
        if (Points[Mid].X <= X) { Lo = Mid; } else { Hi = Mid; }
    }
    const float T = (X - Points[Lo].X) / FMath::Max(Points[Hi].X - Points[Lo].X, 0.001f);
    return Component == 2 ? FMath::Max(FMath::Lerp(Points[Lo].Z, Points[Hi].Z, T), 3.f) : FMath::Lerp(Points[Lo].Y, Points[Hi].Y, T);
}
// Distance from the water edge; negative inside the channel.
inline float RiverClearance(const FVector2D& P)
{
    const auto& Points = MountainRiverRoute();
    double Best = BIG_NUMBER;
    for (int32 I = 1; I < Points.Num(); ++I)
    {
        const FVector2D A(Points[I-1].X, Points[I-1].Y), B(Points[I].X, Points[I].Y);
        const FVector2D D = B - A;
        const double T = FMath::Clamp(FVector2D::DotProduct(P-A,D) / FMath::Max(D.SizeSquared(),1.0),0.0,1.0);
        Best = FMath::Min(Best, (P-A-D*T).Size() - FMath::Lerp(double(Points[I-1].W),double(Points[I].W),T));
    }
    return float(Best);
}
// Coordinate binding only. Appearance formulas below are those used by Map_Saga.
inline float SagaMouthDistance(const FVector2D& P)
{
    return FVector2D::DotProduct(P - MouthPoint(), MouthDir());
}
inline float Height(const FVector2D& WorldP)
{
    constexpr float TerrainScale = 0.125f;
    const FVector2D P = (ToTerrain(WorldP) - FVector2D(2500.f, 0.f)) / TerrainScale;
    static TArray<uint8> DataV7;
    static FString LoadedVersionV7;
    if (LoadedVersionV7 != TEXT("estuary_0125_v7_flatvillage"))
    {
        if (!FFileHelper::LoadFileToArray(DataV7, *(FPaths::ProjectContentDir() / TEXT("Vikings/Environment/Strandvik/Data/Strandvik_heights_river_cm.f32")))) { return -7.f; }
        LoadedVersionV7 = TEXT("estuary_0125_v7_flatvillage");
    }
    constexpr int32 N = 1009;
    if (DataV7.Num() != N * N * sizeof(float)) { return -7.f; }
    const double East = 315018.01910359866 + (P.X + P.Y) * 0.007071067811865475;
    const double North = 6673660.99640523 + (-P.X + P.Y) * 0.007071067811865475;
    const double U = FMath::Clamp((East - 312818.0) / 3400.0 * (N - 1), 0.0, double(N - 1));
    const double V = FMath::Clamp((North - 6672761.0) / 3400.0 * (N - 1), 0.0, double(N - 1));
    const int32 X = FMath::Min(FMath::FloorToInt(U), N - 2), Y = FMath::Min(FMath::FloorToInt(V), N - 2);
    const float* Z = reinterpret_cast<const float*>(DataV7.GetData());
    return TerrainScale * FMath::Lerp(FMath::Lerp(Z[Y*N+X], Z[Y*N+X+1], float(U-X)),
        FMath::Lerp(Z[(Y+1)*N+X], Z[(Y+1)*N+X+1], float(U-X)), float(V-Y));
}
}
