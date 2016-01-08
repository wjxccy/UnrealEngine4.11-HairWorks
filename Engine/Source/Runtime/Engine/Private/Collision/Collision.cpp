// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Collision.cpp: AActor collision implementation
=============================================================================*/

#include "EnginePrivate.h"
#include "Collision.h"

// TEMP until crash is fixed in IsCollisionEnabled().
#include "LandscapeHeightfieldCollisionComponent.h"

//////////////////////////////////////////////////////////////////////////
// FHitResult

FHitResult::FHitResult(class AActor* InActor, class UPrimitiveComponent* InComponent, FVector const& HitLoc, FVector const& HitNorm)
{
	FMemory::Memzero(this, sizeof(FHitResult));
	Location = HitLoc;
	ImpactPoint = HitLoc;
	Normal = HitNorm;
	ImpactNormal = HitNorm;
	Actor = InActor;
	Component = InComponent;
}

AActor* FHitResult::GetActor() const
{
	return Actor.Get();
}

UPrimitiveComponent* FHitResult::GetComponent() const
{
	return Component.Get();
}

bool FHitResult::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	// Most of the time the vectors are the same values, use that as an optimization
	bool bImpactPointEqualsLocation = 0, bImpactNormalEqualsNormal = 0;

	// Often times the indexes are invalid, use that as an optimization
	bool bInvalidItem = 0, bInvalidFaceIndex = 0, bNoPenetrationDepth = 0;

	if (Ar.IsSaving())
	{
		bImpactPointEqualsLocation = (ImpactPoint == Location);
		bImpactNormalEqualsNormal = (ImpactNormal == Normal);
		bInvalidItem = (Item == INDEX_NONE);
		bInvalidFaceIndex = (FaceIndex == INDEX_NONE);
		bNoPenetrationDepth = (PenetrationDepth == 0.0f);
	}

	// pack bitfield with flags
	uint8 Flags = (bBlockingHit << 0) | (bStartPenetrating << 1) | (bImpactPointEqualsLocation << 2) | (bImpactNormalEqualsNormal << 3) | (bInvalidItem << 4) | (bInvalidFaceIndex << 5) | (bInvalidFaceIndex << 6);
	Ar.SerializeBits(&Flags, 7); 
	bBlockingHit = (Flags & (1 << 0)) ? 1 : 0;
	bStartPenetrating = (Flags & (1 << 1)) ? 1 : 0;
	bImpactPointEqualsLocation = (Flags & (1 << 2)) ? 1 : 0;
	bImpactNormalEqualsNormal = (Flags & (1 << 3)) ? 1 : 0;
	bInvalidItem = (Flags & (1 << 4)) ? 1 : 0;
	bInvalidFaceIndex = (Flags & (1 << 5)) ? 1 : 0;
	bNoPenetrationDepth = (Flags & (1 << 6)) ? 1 : 0;

	Ar << Time;

	bOutSuccess = true;

	bool bOutSuccessLocal = true;

	Location.NetSerialize(Ar, Map, bOutSuccessLocal);
	bOutSuccess &= bOutSuccessLocal;
	Normal.NetSerialize(Ar, Map, bOutSuccessLocal);
	bOutSuccess &= bOutSuccessLocal;

	if (!bImpactPointEqualsLocation)
	{
		ImpactPoint.NetSerialize(Ar, Map, bOutSuccessLocal);
		bOutSuccess &= bOutSuccessLocal;
	}
	else if (Ar.IsLoading())
	{
		ImpactPoint = Location;
	}
	
	if (!bImpactNormalEqualsNormal)
	{
		ImpactNormal.NetSerialize(Ar, Map, bOutSuccessLocal);
		bOutSuccess &= bOutSuccessLocal;
	}
	else if (Ar.IsLoading())
	{
		ImpactNormal = Normal;
	}
	TraceStart.NetSerialize(Ar, Map, bOutSuccessLocal);
	bOutSuccess &= bOutSuccessLocal;
	TraceEnd.NetSerialize(Ar, Map, bOutSuccessLocal);
	bOutSuccess &= bOutSuccessLocal;

	if (!bNoPenetrationDepth)
	{
		Ar << PenetrationDepth;
	}
	else if(Ar.IsLoading())
	{
		PenetrationDepth = 0.0f;
	}
	
	if (!bInvalidItem)
	{
		Ar << Item;
	}
	else if (Ar.IsLoading())
	{
		Item = INDEX_NONE;
	}

	Ar << PhysMaterial;
	Ar << Actor;
	Ar << Component;
	Ar << BoneName;
	if (!bInvalidFaceIndex)
	{
		Ar << FaceIndex;
	}
	else if (Ar.IsLoading())
	{
		FaceIndex = INDEX_NONE;
	}
	

	return true;
}

//////////////////////////////////////////////////////////////////////////
// FOverlapResult

AActor* FOverlapResult::GetActor() const
{
	return Actor.Get();
}

UPrimitiveComponent* FOverlapResult::GetComponent() const
{
	return Component.Get();
}


//////////////////////////////////////////////////////////////////////////
// FCollisionQueryParams

FCollisionQueryParams::FCollisionQueryParams(FName InTraceTag, bool bInTraceComplex, const AActor* InIgnoreActor)
{
	bTraceComplex = bInTraceComplex;
	TraceTag = InTraceTag;
	bTraceAsyncScene = false;
	bFindInitialOverlaps = true;
	bReturnFaceIndex = false;
	bReturnPhysicalMaterial = false;
	bComponentListUnique = true;
	IgnoreMask = 0;
	bIgnoreBlocks = false;

	AddIgnoredActor(InIgnoreActor);
	if (InIgnoreActor != NULL)
	{
		OwnerTag = InIgnoreActor->GetFName();
	}
}


static FORCEINLINE_DEBUGGABLE bool IsQueryCollisionEnabled(const UPrimitiveComponent* PrimComponent)
{
	const ECollisionEnabled::Type CollisionEnabled = PrimComponent->GetCollisionEnabled();

	return CollisionEnabled == ECollisionEnabled::QueryAndPhysics
		|| CollisionEnabled == ECollisionEnabled::QueryOnly;
}

static FORCEINLINE_DEBUGGABLE bool CheckForCollision(const AActor* Actor)
{
	// Note: This code operates differently in the editor because the actors in the editor can have their collision setting become out of sync with physx.  This happens because even when collision is disabled on an actor, in the editor we 
	// tell phsx that we still require queries( See FBodyInstance::UpdatePhysicsFilterData).  Doing so allows us to perform editor only traces against objects with collision disabled.  Due to this, we cannot assume that the collision enabled flag here
	// setting is correct compared to physx and so we still ignore specified components regardless of their collision setting
#if WITH_EDITOR
	const bool bCheckForCollision = Actor && Actor->GetWorld() && Actor->GetWorld()->IsGameWorld();
#else
	const bool bCheckForCollision = true;
#endif
	return bCheckForCollision;
}


static FORCEINLINE_DEBUGGABLE bool CheckForCollision(const UPrimitiveComponent* PrimComponent)
{
	// Note: This code operates differently in the editor because the actors in the editor can have their collision setting become out of sync with physx.  This happens because even when collision is disabled on an actor, in the editor we 
	// tell phsx that we still require queries( See FBodyInstance::UpdatePhysicsFilterData).  Doing so allows us to perform editor only traces against objects with collision disabled.  Due to this, we cannot assume that the collision enabled flag here
	// setting is correct compared to physx and so we still ignore specified components regardless of their collision setting
#if WITH_EDITOR
	bool bCheckForCollision = PrimComponent && PrimComponent->GetWorld() && PrimComponent->GetWorld()->IsGameWorld();
#else
	const bool bCheckForCollision = true;
#endif
	return bCheckForCollision;
}


void FCollisionQueryParams::AddIgnoredActor(const AActor* InIgnoreActor)
{
	if (InIgnoreActor)
	{	
		const bool bCheckForCollision = CheckForCollision(InIgnoreActor);
		const int32 InitialCount = IgnoreComponents.Num();
		for (const UActorComponent* ActorComponent : InIgnoreActor->GetComponents())
		{
			const UPrimitiveComponent* PrimComponent = Cast<const UPrimitiveComponent>(ActorComponent);
			if (PrimComponent && (!bCheckForCollision || IsQueryCollisionEnabled(PrimComponent)) )
			{
				IgnoreComponents.Add(PrimComponent->GetUniqueID());
			}
		}

		// If we added entries to a non-empty array, we aren't sure if the list is unique. Assumes GetComponents() above contains no duplicates.
		if ((InitialCount != 0) && (IgnoreComponents.Num() - InitialCount > 0))
		{
			bComponentListUnique = false;
		}
	}
}

void FCollisionQueryParams::AddIgnoredActors(const TArray<AActor*>& InIgnoreActors)
{
	for (int32 Idx = 0; Idx < InIgnoreActors.Num(); ++Idx)
	{
		AddIgnoredActor(InIgnoreActors[Idx]);
	}
}

void FCollisionQueryParams::AddIgnoredActors(const TArray<TWeakObjectPtr<AActor> >& InIgnoreActors)
{
	for (int32 Idx = 0; Idx < InIgnoreActors.Num(); ++Idx)
	{
		AddIgnoredActor(InIgnoreActors[Idx].Get());
	}
}

FORCEINLINE_DEBUGGABLE void FCollisionQueryParams::Internal_AddIgnoredComponent(const UPrimitiveComponent* InIgnoreComponent)
{
	if (InIgnoreComponent && (!CheckForCollision(InIgnoreComponent) || IsQueryCollisionEnabled(InIgnoreComponent)))
	{
		IgnoreComponents.Add(InIgnoreComponent->GetUniqueID());
		bComponentListUnique = false;
	}
}

void FCollisionQueryParams::AddIgnoredComponent(const UPrimitiveComponent* InIgnoreComponent)
{
	Internal_AddIgnoredComponent(InIgnoreComponent);
}

void FCollisionQueryParams::AddIgnoredComponents(const TArray<UPrimitiveComponent*>& InIgnoreComponents)
{
	for (const UPrimitiveComponent* IgnoreComponent : InIgnoreComponents)
	{
		Internal_AddIgnoredComponent(IgnoreComponent);
	}
}

void FCollisionQueryParams::AddIgnoredComponents(const TArray<TWeakObjectPtr<UPrimitiveComponent>>& InIgnoreComponents)
{
	for (TWeakObjectPtr<UPrimitiveComponent> IgnoreComponent : InIgnoreComponents)
	{
		Internal_AddIgnoredComponent(IgnoreComponent.Get());
	}
}

void FCollisionQueryParams::AddIgnoredComponent_LikelyDuplicatedRoot(const UPrimitiveComponent* InIgnoreComponent)
{
	if (InIgnoreComponent && (!CheckForCollision(InIgnoreComponent) || IsQueryCollisionEnabled(InIgnoreComponent)))
	{
		// Code calling this is usually just making sure they don't add the root component to queries right before the actual query.
		// We try to avoid invalidating the uniqueness of the array if this is the case.
		const uint32 ComponentID = InIgnoreComponent->GetUniqueID();
		if (IgnoreComponents.Num() == 0 || IgnoreComponents[0] != ComponentID)
		{
			IgnoreComponents.Add(ComponentID);
			bComponentListUnique = false;
		}
	}
}

const FCollisionQueryParams::IgnoreComponentsArrayType& FCollisionQueryParams::GetIgnoredComponents() const
{
	if (!bComponentListUnique)
	{
		// Make unique
		bComponentListUnique = true;
		if (IgnoreComponents.Num() > 1)
		{
			// For adding a collection to ignore it's faster to sort and remove duplicates
			// than to check for duplicates at each addition.
			IgnoreComponents.Sort();
			uint32* U = IgnoreComponents.GetData();
			uint32* D = U+1;
			uint32* E = U + IgnoreComponents.Num();
			while (D != E)
			{
				if (*U != *D)
				{
					*(++U) = *D;
				}
				D += 1;
			}
			IgnoreComponents.SetNum(U - IgnoreComponents.GetData() + 1, /*bAllowShrinking=*/ false);
		}
	}

	return IgnoreComponents;
}

void FCollisionQueryParams::SetNumIgnoredComponents(int32 NewNum)
{
	if (NewNum > 0)
	{
		// We can only make it smaller (and uniqueness does not change).
		if (NewNum < IgnoreComponents.Num())
		{
			IgnoreComponents.SetNum(NewNum, /*bAllowShrinking=*/ false);
		}
	}
	else
	{
		ClearIgnoredComponents();
	}
}

//////////////////////////////////////////////////////////////////////////
// FSeparatingAxisPointCheck

TArray<FVector> FSeparatingAxisPointCheck::TriangleVertices;

bool FSeparatingAxisPointCheck::TestSeparatingAxisCommon(const FVector& Axis, float ProjectedPolyMin, float ProjectedPolyMax)
{
	const float ProjectedCenter = FVector::DotProduct(Axis, BoxCenter);
	const float ProjectedExtent = FVector::DotProduct(Axis.GetAbs(), BoxExtent);
	const float ProjectedBoxMin = ProjectedCenter - ProjectedExtent;
	const float ProjectedBoxMax = ProjectedCenter + ProjectedExtent;

	if (ProjectedPolyMin > ProjectedBoxMax || ProjectedPolyMax < ProjectedBoxMin)
	{
		return false;
	}

	if (bCalcLeastPenetration)
	{
		const float AxisMagnitudeSqr = Axis.SizeSquared();
		if (AxisMagnitudeSqr > (SMALL_NUMBER * SMALL_NUMBER))
		{
			const float InvAxisMagnitude = FMath::InvSqrt(AxisMagnitudeSqr);
			const float MinPenetrationDist = (ProjectedBoxMax - ProjectedPolyMin) * InvAxisMagnitude;
			const float	MaxPenetrationDist = (ProjectedPolyMax - ProjectedBoxMin) * InvAxisMagnitude;

			if (MinPenetrationDist < BestDist)
			{
				BestDist = MinPenetrationDist;
				HitNormal = -Axis * InvAxisMagnitude;
			}

			if (MaxPenetrationDist < BestDist)
			{
				BestDist = MaxPenetrationDist;
				HitNormal = Axis * InvAxisMagnitude;
			}
		}
	}

	return true;
}

bool FSeparatingAxisPointCheck::TestSeparatingAxisTriangle(const FVector& Axis)
{
	const float ProjectedV0 = FVector::DotProduct(Axis, PolyVertices[0]);
	const float ProjectedV1 = FVector::DotProduct(Axis, PolyVertices[1]);
	const float ProjectedV2 = FVector::DotProduct(Axis, PolyVertices[2]);
	const float ProjectedTriMin = FMath::Min3(ProjectedV0, ProjectedV1, ProjectedV2);
	const float ProjectedTriMax = FMath::Max3(ProjectedV0, ProjectedV1, ProjectedV2);

	return TestSeparatingAxisCommon(Axis, ProjectedTriMin, ProjectedTriMax);
}

bool FSeparatingAxisPointCheck::TestSeparatingAxisGeneric(const FVector& Axis)
{
	float ProjectedPolyMin = TNumericLimits<float>::Max();
	float ProjectedPolyMax = TNumericLimits<float>::Lowest();
	for (const auto& Vertex : PolyVertices)
	{
		const float ProjectedVertex = FVector::DotProduct(Axis, Vertex);
		ProjectedPolyMin = FMath::Min(ProjectedPolyMin, ProjectedVertex);
		ProjectedPolyMax = FMath::Max(ProjectedPolyMax, ProjectedVertex);
	}

	return TestSeparatingAxisCommon(Axis, ProjectedPolyMin, ProjectedPolyMax);
}

bool FSeparatingAxisPointCheck::FindSeparatingAxisTriangle()
{
	check(PolyVertices.Num() == 3);
	const FVector EdgeDir0 = PolyVertices[1] - PolyVertices[0];
	const FVector EdgeDir1 = PolyVertices[2] - PolyVertices[1];
	const FVector EdgeDir2 = PolyVertices[0] - PolyVertices[2];

	// Box Z edge x triangle edges.

	if (!TestSeparatingAxisTriangle(FVector(EdgeDir0.Y, -EdgeDir0.X, 0.0f)) ||
		!TestSeparatingAxisTriangle(FVector(EdgeDir1.Y, -EdgeDir1.X, 0.0f)) ||
		!TestSeparatingAxisTriangle(FVector(EdgeDir2.Y, -EdgeDir2.X, 0.0f)))
	{
		return false;
	}

	// Box Y edge x triangle edges.

	if (!TestSeparatingAxisTriangle(FVector(-EdgeDir0.Z, 0.0f, EdgeDir0.X)) ||
		!TestSeparatingAxisTriangle(FVector(-EdgeDir1.Z, 0.0f, EdgeDir1.X)) ||
		!TestSeparatingAxisTriangle(FVector(-EdgeDir2.Z, 0.0f, EdgeDir2.X)))
	{
		return false;
	}

	// Box X edge x triangle edges.

	if (!TestSeparatingAxisTriangle(FVector(0.0f, EdgeDir0.Z, -EdgeDir0.Y)) ||
		!TestSeparatingAxisTriangle(FVector(0.0f, EdgeDir1.Z, -EdgeDir1.Y)) ||
		!TestSeparatingAxisTriangle(FVector(0.0f, EdgeDir2.Z, -EdgeDir2.Y)))
	{
		return false;
	}

	// Box faces.

	if (!TestSeparatingAxisTriangle(FVector(0.0f, 0.0f, 1.0f)) ||
		!TestSeparatingAxisTriangle(FVector(1.0f, 0.0f, 0.0f)) ||
		!TestSeparatingAxisTriangle(FVector(0.0f, 1.0f, 0.0f)))
	{
		return false;
	}

	// Triangle normal.

	if (!TestSeparatingAxisTriangle(FVector::CrossProduct(EdgeDir1, EdgeDir0)))
	{
		return false;
	}

	return true;
}

bool FSeparatingAxisPointCheck::FindSeparatingAxisGeneric()
{
	check(PolyVertices.Num() > 3);
	int32 LastIndex = PolyVertices.Num() - 1;
	for (int32 Index = 0; Index < PolyVertices.Num(); Index++)
	{
		const FVector& V0 = PolyVertices[LastIndex];
		const FVector& V1 = PolyVertices[Index];
		const FVector EdgeDir = V1 - V0;

		// Box edges x polygon edge

		if (!TestSeparatingAxisGeneric(FVector(EdgeDir.Y, -EdgeDir.X, 0.0f)) ||
			!TestSeparatingAxisGeneric(FVector(-EdgeDir.Z, 0.0f, EdgeDir.X)) ||
			!TestSeparatingAxisGeneric(FVector(0.0f, EdgeDir.Z, -EdgeDir.Y)))
		{
			return false;
		}

		LastIndex = Index;
	}

	// Box faces.

	if (!TestSeparatingAxisGeneric(FVector(0.0f, 0.0f, 1.0f)) ||
		!TestSeparatingAxisGeneric(FVector(1.0f, 0.0f, 0.0f)) ||
		!TestSeparatingAxisGeneric(FVector(0.0f, 1.0f, 0.0f)))
	{
		return false;
	}

	// Polygon normal.

	int32 Index0 = PolyVertices.Num() - 2;
	int32 Index1 = Index0 + 1;
	for (int32 Index2 = 0; Index2 < PolyVertices.Num(); Index2++)
	{
		const FVector& V0 = PolyVertices[Index0];
		const FVector& V1 = PolyVertices[Index1];
		const FVector& V2 = PolyVertices[Index2];

		const FVector EdgeDir0 = V1 - V0;
		const FVector EdgeDir1 = V2 - V1;

		FVector Normal = FVector::CrossProduct(EdgeDir1, EdgeDir0);
		if (Normal.SizeSquared() > SMALL_NUMBER)
		{
			if (!TestSeparatingAxisGeneric(Normal))
			{
				return false;
			}
			break;
		}

		Index0 = Index1;
		Index1 = Index2;
	}

	return true;
}



#if !UE_BUILD_SHIPPING

DECLARE_LOG_CATEGORY_EXTERN(LogCollisionCommands, Log, All);
DEFINE_LOG_CATEGORY(LogCollisionCommands);

namespace CollisionResponseConsoleCommands
{
	static const FString ResponseStrings[] = {TEXT("Ignore"), TEXT("Overlap"), TEXT("Block")};

	FString FillString(TCHAR Char, int32 Count)
	{
		FString Result;
		if (Count > 0)
		{
			Result.Reserve(Count);
			for (int32 i=0; i < Count; i++)
			{
				Result.AppendChar(TEXT('-'));
			}
		}
		return Result;
	}

	FString GetDisplayNameText(const UEnum* Enum, int32 Index, const FString& Fallback)
	{
		if (Enum)
		{
			#if WITH_EDITOR
				return Enum->GetDisplayNameText(Index).ToString();
			#else
				return FName::NameToDisplayString(Enum->GetEnumName(Index), false);
			#endif
		}
		return Fallback;
	}

	FString FormatObjectName(UObject* Obj)
	{
		FString Result;
		if (Obj)
		{
			if (Obj->GetOuter())
			{
				Result = TEXT("'") + Obj->GetPathName(Obj->GetOuter()->GetOuter()) + TEXT("'");
			}
			else
			{
				Result = TEXT("'") + Obj->GetPathName(Obj->GetOuter()) + TEXT("'");
			}
		}
		return Result;
	}

	void ListCollisionProfileNames()
	{
		TArray<TSharedPtr<FName>> ProfileNameList;
		UCollisionProfile::Get()->GetProfileNames(ProfileNameList);
		int32 Index = 0;
		for (TSharedPtr<FName> NamePtr : ProfileNameList)
		{
			const FName TemplateName = *NamePtr;
			UE_LOG(LogCollisionCommands, Log, TEXT("%2d: %s"), Index++, *TemplateName.ToString());
		}
	}

	void ListCollisionChannelNames()
	{
		const UEnum *ChannelEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("ECollisionChannel"), true);
		if (ChannelEnum)
		{
			for (int32 ChannelIndex = 0; ChannelIndex < ECollisionChannel::ECC_MAX; ChannelIndex++)
			{
				const FString ChannelShortName = ChannelEnum->GetEnumName(ChannelIndex);
				const FString ChannelDisplayName = GetDisplayNameText(ChannelEnum, ChannelIndex, ChannelShortName);
				UE_LOG(LogCollisionCommands, Log, TEXT("%2d: %s (%s)"), ChannelIndex, *ChannelShortName, *ChannelDisplayName);
			}
		}
	}

	struct FSortComponentsWithResponseToProfile
	{
		FSortComponentsWithResponseToProfile(ECollisionResponse InRequiredResponse)
		: RequiredResponse(InRequiredResponse)
		{
		}

		bool operator()(const UPrimitiveComponent& A, const UPrimitiveComponent& B) const
		{
			UObject* AOwner = A.GetOuter();
			UObject* BOwner = B.GetOuter();
			if (AOwner && BOwner)
			{
				// For overlaps, sort first by bGenerateOverlapEvents
				if (RequiredResponse == ECollisionResponse::ECR_Overlap)
				{
					if (A.bGenerateOverlapEvents != B.bGenerateOverlapEvents)
					{
						return A.bGenerateOverlapEvents;
					}
				}

				// Sort by name.
				return AOwner->GetName() < BOwner->GetName();
			}
			else
			{
				return false;
			}
		}

	private:
		ECollisionResponse RequiredResponse;
	};

	ECollisionResponse StringToCollisionResponse(const FString& InString)
	{
		int32 TestIndex = 0;
		for (const FString& CurrentString : ResponseStrings)
		{
			if (CurrentString == InString)
			{
				return ECollisionResponse(TestIndex);
			}
			TestIndex++;
		}

		return ECollisionResponse::ECR_MAX;
	}

	ECollisionChannel StringToCollisionChannel(const FString& InString)
	{
		const UEnum *ChannelEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("ECollisionChannel"), true);
		if (ChannelEnum)
		{
			int32 ChannelInt = INDEX_NONE;

			// Try the name they gave us.
			ChannelInt = ChannelEnum->GetValueByName(FName(*InString));
			if (ChannelInt != INDEX_NONE)
			{
				return ECollisionChannel(ChannelInt);
			}

			// Try with adding the prefix, ie "ECC_".
			const FString WithPrefixName = ChannelEnum->GenerateEnumPrefix() + TEXT("_") + InString;
			ChannelInt = ChannelEnum->GetValueByName(FName(*WithPrefixName));
			if (ChannelInt != INDEX_NONE)
			{
				return ECollisionChannel(ChannelInt);
			}

			// Try matching the display name
			const FString NullString(TEXT(""));
			for (int32 ChannelIndex = 0; ChannelIndex < ChannelEnum->NumEnums(); ChannelIndex++)
			{
				const FString ChannelDisplayName = GetDisplayNameText(ChannelEnum, ChannelIndex, NullString);
				if (ChannelDisplayName == InString)
				{
					return ECollisionChannel(ChannelEnum->GetValueByIndex(ChannelIndex));
				}
			}
		}

		// Try parsing a digit, as in from the ListChannels command
		const int32 ChannelIndex = FCString::IsNumeric(*InString) ? FCString::Atoi(*InString) : INDEX_NONE;
		return (ChannelIndex >= 0 && ChannelIndex < ECollisionChannel::ECC_MAX) ? ECollisionChannel(ChannelIndex) : ECollisionChannel::ECC_MAX;
	}

	FName StringToCollisionProfile(const FString& InString)
	{
		const FName InStringAsName(*InString);
		FCollisionResponseTemplate Template;
		if (UCollisionProfile::Get()->GetProfileTemplate(InStringAsName, Template))
		{
			return InStringAsName;
		}

		if (FCString::IsNumeric(*InString))
		{
			const int32 ProfileIndex = FCString::Atoi(*InString);
			const FCollisionResponseTemplate* TemplateByIndex = UCollisionProfile::Get()->GetProfileByIndex(ProfileIndex);
			if (TemplateByIndex)
			{
				return TemplateByIndex->Name;
			}
		}

		return NAME_None;
	}

	//////////////////////////////////////////////////////////////////////////
	void ListComponentsWithResponseToProfile(const ECollisionResponse RequiredResponse, const FName& ProfileToCheck)
	{
		// Look at all components and check response to profile
		TSet<UPrimitiveComponent*> Results;
		FCollisionResponseTemplate Template;
		if (UCollisionProfile::Get()->GetProfileTemplate(ProfileToCheck, Template))
		{
			for (TObjectIterator<UPrimitiveComponent> Iter(RF_NoFlags, /*bIncludeDerivedClasses*/ true, EInternalObjectFlags::PendingKill); Iter; ++Iter)
			{
				UPrimitiveComponent* Comp = *Iter;
				
				// TEMP CRASH WORKAROUND: IsCollisionEnabled() fails on ULandscapeComponent CDO.
				if (Cast<ULandscapeHeightfieldCollisionComponent>(Comp))
				{
					continue;
				}

				if (IsQueryCollisionEnabled(Comp))
				{
					const ECollisionResponse CompResponse = Comp->GetCollisionResponseToChannel(Template.ObjectType.GetValue());
					const ECollisionResponse TemplateResponse = Template.ResponseToChannels.GetResponse(Comp->GetCollisionObjectType());
					const ECollisionResponse MinResponse = FMath::Min<ECollisionResponse>(CompResponse, TemplateResponse);
					if (MinResponse == RequiredResponse)
					{
						Results.Add(Comp);
					}
				}
			}
		}
		else
		{
			UE_LOG(LogCollisionCommands, Warning, TEXT("Could not find collision profile '%s'. Use 'Collision.ListProfiles' to see a full list of available profiles."), *ProfileToCheck.ToString());
			return;
		}

		// Log results.
		if (Results.Num() > 0)
		{
			Results.Sort(FSortComponentsWithResponseToProfile(RequiredResponse));
			const UEnum *ChannelEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("ECollisionChannel"), true);

			// Get max column widths for some data
			int32 MaxNameWidth = 0;
			int32 MaxChannelWidth = 0;
			int32 MaxProfileWidth = 0;
			for (UPrimitiveComponent* Comp : Results)
			{
				UObject* Outer = Comp->GetOuter();
				if (Outer)
				{
					const FString PathName = FormatObjectName(Comp);
					MaxNameWidth = FMath::Max<int32>(MaxNameWidth, PathName.Len());
				}

				if (ChannelEnum)
				{
					const FString ChannelName = ChannelEnum->GetNameByIndex((int32)Comp->GetCollisionObjectType()).ToString();
					const FString ChannelDisplayName = GetDisplayNameText(ChannelEnum, (int32)Comp->GetCollisionObjectType(), ChannelName);
					MaxChannelWidth = FMath::Max<int32>(MaxChannelWidth, ChannelDisplayName.Len());
				}

				MaxProfileWidth = FMath::Max<int32>(MaxProfileWidth, Comp->GetCollisionProfileName().ToString().Len());
			}

			// Column headings
			FString Output;
			if (RequiredResponse == ECollisionResponse::ECR_Overlap)
			{
				Output = FString::Printf(TEXT("  #, GenerateEvents, %-*s, %-*s, %-*s, Path"), MaxNameWidth, TEXT("Component"), MaxChannelWidth, TEXT("ObjectType"), MaxProfileWidth, TEXT("Profile"));
			}
			else
			{
				Output = FString::Printf(TEXT("  #, %-*s, %-*s, %-*s, Path"), MaxNameWidth, TEXT("Component"), MaxChannelWidth, TEXT("ObjectType"), MaxProfileWidth, TEXT("Profile"));
			}
			UE_LOG(LogCollisionCommands, Log, TEXT("%s"), *Output);
			const int32 TotalLen = Output.Len() + 16;
			const FString LineMarker = FillString(TCHAR('-'), TotalLen);
			UE_LOG(LogCollisionCommands, Log, TEXT("%s"), *LineMarker);

			// Data
			int32 Index=0;
			for (UPrimitiveComponent* Comp : Results)
			{
				const FString ChannelName = (ChannelEnum ? ChannelEnum->GetNameByIndex((int32)Comp->GetCollisionObjectType()).ToString() : TEXT("<unknown>"));
				const FString ChannelDisplayName = GetDisplayNameText(ChannelEnum, (int32)Comp->GetCollisionObjectType(), ChannelName);
				UObject* Outer = Comp->GetOuter();
				if (Outer)
				{
					const FString OtherProfileName = Comp->GetCollisionProfileName().ToString();
					const FString PathName = FormatObjectName(Comp);
					if (RequiredResponse == ECollisionResponse::ECR_Overlap)
					{
						UE_LOG(LogCollisionCommands, Log, TEXT("%3d, %-14s, %-*s, %-*s, %-*s, %s"),
							   Index, Comp->bGenerateOverlapEvents ? TEXT("true"):TEXT("false"), MaxNameWidth, *PathName, MaxChannelWidth, *ChannelDisplayName, MaxProfileWidth, *OtherProfileName, Outer->GetOuter() ? *GetPathNameSafe(Outer->GetOuter()) : *GetPathNameSafe(Outer));
					}
					else
					{
						UE_LOG(LogCollisionCommands, Log, TEXT("%3d, %-*s, %-*s, %-*s, %s"),
							   Index, MaxNameWidth, *PathName, MaxChannelWidth, *ChannelDisplayName, MaxProfileWidth, *OtherProfileName, Outer->GetOuter() ? *GetPathNameSafe(Outer->GetOuter()) : *GetPathNameSafe(Outer));
					}
					Index++;
				}
			}
			UE_LOG(LogCollisionCommands, Log, TEXT("%s"), *LineMarker);
		}
		check(RequiredResponse < ECollisionResponse::ECR_MAX);
		UE_LOG(LogCollisionCommands, Log, TEXT("Found %d components with '%s' response to profile '%s'."), Results.Num(), *ResponseStrings[(int32)RequiredResponse], *ProfileToCheck.ToString());
	}

	//////////////////////////////////////////////////////////////////////////
	// Args: <Response> <Profile>
	void Parse_ListComponentsWithResponseToProfile(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogCollisionCommands, Warning, TEXT("Usage: 'Collision.ListComponentsWithResponseToProfile <Response> <Profile>'."));
			UE_LOG(LogCollisionCommands, Warning, TEXT("  Response: Ignore, Overlap, Block"));
			UE_LOG(LogCollisionCommands, Warning, TEXT("  Profile:  Collision profile name or index. Use 'Collision.ListProfiles' to see a full list."));
			return;
		}

		// Arg0 : Response
		const FString& ResponseString = Args[0];
		ECollisionResponse RequiredResponse = StringToCollisionResponse(ResponseString);
		if (RequiredResponse == ECollisionResponse::ECR_MAX)
		{
			UE_LOG(LogCollisionCommands, Warning, TEXT("Unknown response '%s'. Must be Ignore, Overlap, or Block."), *ResponseString);
			return;
		}

		// Arg1 : Profile
		const FString& ProfileNameString = Args[1];
		const FName ProfileToCheck = StringToCollisionProfile(ProfileNameString);
		if (ProfileToCheck == NAME_None)
		{
			UE_LOG(LogCollisionCommands, Warning, TEXT("Could not find collision profile '%s'. Use 'Collision.ListProfiles' to see a full list of available profiles."), *ProfileToCheck.ToString());
			return;
		}

		ListComponentsWithResponseToProfile(RequiredResponse, ProfileToCheck);
	}

	//////////////////////////////////////////////////////////////////////////
	void ListProfilesWithResponseToChannel(const ECollisionResponse RequiredResponse, const ECollisionChannel TestChannel)
	{
		TSet<FName> Results;
		TArray<TSharedPtr<FName>> ProfileNameList;
		UCollisionProfile::Get()->GetProfileNames(ProfileNameList);
		int32 Index = 0;
		for (TSharedPtr<FName> NamePtr : ProfileNameList)
		{
			const FName TemplateName = *NamePtr;
			FCollisionResponseTemplate Template;
			if (UCollisionProfile::Get()->GetProfileTemplate(TemplateName, Template))
			{
				const ECollisionResponse TemplateResponse = Template.ResponseToChannels.GetResponse(TestChannel);
				if (TemplateResponse == RequiredResponse)
				{
					Results.Add(TemplateName);
				}
			}
			++Index;
		}

		if (Results.Num() > 0)
		{
			Results.Sort([](const FName& A, const FName& B) { return A < B; });
			for (FName& ResultName : Results)
			{
				UE_LOG(LogCollisionCommands, Log, TEXT("%s"), *ResultName.ToString());
			}
		}
		check(RequiredResponse < ECollisionResponse::ECR_MAX);
		const UEnum *ChannelEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("ECollisionChannel"), true);
		const FString ChannelName = (ChannelEnum ? ChannelEnum->GetEnumName(TestChannel) : TEXT("<unknown>"));
		const FString ChannelDisplayName = GetDisplayNameText(ChannelEnum, TestChannel, ChannelName);
		UE_LOG(LogCollisionCommands, Log, TEXT("----------------------------------------------------------------------"));
		UE_LOG(LogCollisionCommands, Log, TEXT("Found %d profiles with '%s' response to channel '%s' ('%s')"), Results.Num(), *ResponseStrings[(int32)RequiredResponse], *ChannelName, *ChannelDisplayName);
	}

	//////////////////////////////////////////////////////////////////////////
	// Args: <Response> <Channel>
	void Parse_ListProfilesWithResponseToChannel(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogCollisionCommands, Warning, TEXT("Usage: 'Collision.ListProfilesWithResponseToChannel <Response> <Channel>'."));
			UE_LOG(LogCollisionCommands, Warning, TEXT("  Response: Ignore, Overlap, Block"));
			UE_LOG(LogCollisionCommands, Warning, TEXT("  Profile:  Collision channel name or index. Use 'Collision.ListChannels' to see a full list."));
			return;
		}

		// Arg0 : Response
		const FString& ResponseString = Args[0];
		const ECollisionResponse RequiredResponse = StringToCollisionResponse(ResponseString);
		if (RequiredResponse == ECollisionResponse::ECR_MAX)
		{
			UE_LOG(LogCollisionCommands, Warning, TEXT("Unknown response '%s'. Must be Ignore, Overlap, or Block."), *ResponseString);
			return;
		}

		// Arg1 : Channel
		const FString& ChannelNameString = Args[1];
		const ECollisionChannel Channel = StringToCollisionChannel(ChannelNameString);
		if (Channel == ECollisionChannel::ECC_MAX)
		{
			UE_LOG(LogCollisionCommands, Warning, TEXT("Unknown channel '%s. Use 'Collision.ListChannels' to see a full list.'"), *ChannelNameString);
			return;
		}

		ListProfilesWithResponseToChannel(RequiredResponse, Channel);
	}


	//////////////////////////////////////////////////////////////////////////
	// Console commands

	static FAutoConsoleCommand ListProfilesCommand(
		TEXT("Collision.ListProfiles"),
		TEXT("ListProfiles"),
		FConsoleCommandDelegate::CreateStatic(ListCollisionProfileNames)
	);

	static FAutoConsoleCommand ListChannelsCommand(
		TEXT("Collision.ListChannels"),
		TEXT("ListChannels"),
		FConsoleCommandDelegate::CreateStatic(ListCollisionChannelNames)
	);

	static FAutoConsoleCommandWithWorldAndArgs ListComponentsWithResponseToProfileCommand(
		TEXT("Collision.ListComponentsWithResponseToProfile"),
		TEXT(""),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(CollisionResponseConsoleCommands::Parse_ListComponentsWithResponseToProfile)
	);

	static FAutoConsoleCommandWithWorldAndArgs ListProfilesWithResponseToChannelCommand(
		TEXT("Collision.ListProfilesWithResponseToChannel"),
		TEXT(""),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(CollisionResponseConsoleCommands::Parse_ListProfilesWithResponseToChannel)
	);

}

#endif //!UE_BUILD_SHIPPING




