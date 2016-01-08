// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UE4Game : ModuleRules
{
	public UE4Game(TargetInfo Target)
	{
		PrivateDependencyModuleNames.Add("Core");
	
		DynamicallyLoadedModuleNames.Add("OnlineSubsystemNull");

		if (Target.Platform == UnrealTargetPlatform.Win32 ||
			Target.Platform == UnrealTargetPlatform.Win64 ||
			Target.Platform == UnrealTargetPlatform.Mac)
		{
			if (UEBuildConfiguration.bCompileSteamOSS == true)
			{
				DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");
			}
		}
		if (Target.Platform == UnrealTargetPlatform.IOS || Target.Platform == UnrealTargetPlatform.TVOS)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "OnlineSubsystem", "OnlineSubsystemUtils" });
			DynamicallyLoadedModuleNames.Add("OnlineSubsystemIOS");
			if (Target.Platform == UnrealTargetPlatform.IOS)
			{
				DynamicallyLoadedModuleNames.Add("OnlineSubsystemFacebook");
				DynamicallyLoadedModuleNames.Add("IOSAdvertising");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "OnlineSubsystem", "OnlineSubsystemUtils" });
			DynamicallyLoadedModuleNames.Add("AndroidAdvertising");
			DynamicallyLoadedModuleNames.Add("OnlineSubsystemGooglePlay");
		}
	}
}
