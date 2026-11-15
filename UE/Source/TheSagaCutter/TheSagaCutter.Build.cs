// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TheSagaCutter : ModuleRules
{
	public TheSagaCutter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		RuntimeDependencies.Add("$(ProjectDir)/Content/Vikings/Environment/Strandvik/Data/Strandvik_heights_cm.f32", StagedFileType.UFS);
		RuntimeDependencies.Add("$(ProjectDir)/Content/Vikings/Environment/Strandvik/Data/Strandvik_heights_river_cm.f32", StagedFileType.UFS);
		RuntimeDependencies.Add("$(ProjectDir)/Content/Vikings/Environment/Strandvik/Data/Strandvik_MountainRiverRoute.f32", StagedFileType.UFS);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"GameplayTags", "AIModule", "NavigationSystem", "UMG"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StateTreeModule", "GameplayStateTreeModule", "SmartObjectsModule", "Slate", "SlateCore",
			"CascadeRuntime", "ProceduralMeshComponent"
		});
	}
}
