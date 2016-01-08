// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ModuleManager.h"

class UMovieSceneCapture;

class IMovieSceneCaptureDialogModule : public IModuleInterface
{
public:
	static IMovieSceneCaptureDialogModule& Get()
	{
		static const FName ModuleName(TEXT("MovieSceneCaptureDialog"));
		return FModuleManager::LoadModuleChecked<IMovieSceneCaptureDialogModule>(ModuleName);
	}
	virtual void OpenDialog(const TSharedRef<class FTabManager>& TabManager, UMovieSceneCapture* CaptureObject) = 0;
};

