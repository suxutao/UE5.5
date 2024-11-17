// Copyright Epic Games, Inc. All Rights Reserved.

#include "PreflightValidator.h"

#include "Logic/PreflightService.h"
#include "Logic/TagService.h"
#include "Logging/SubmitToolLog.h"
#include "Internationalization/Regex.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"

bool FPreflightValidator::Activate()
{
	bIsValidSetup = FValidatorBase::Activate();
	PreflightTag = ServiceProvider.Pin()->GetService<FTagService>()->GetTag(TEXT("#preflight"));

	bIsValidSetup = bIsValidSetup && PreflightTag != nullptr;

	return bIsValidSetup;
}

bool FPreflightValidator::Validate(const FString& InCLDescription, const TArray<FSourceControlStateRef>& InFilteredFilesInCL, const TArray<const FTag*>& InTags)
{
	if(!PreflightTag->IsEnabled() || PreflightTag->GetValues().Num() == 0)
	{
		Skip();
		return true;
	}
	else
	{
		for(const FString& PreflightId : PreflightTag->GetValues())
		{
			if(PreflightId.Equals(TEXT("skip")) || PreflightId.Equals(TEXT("none")))
			{
				// if we have at least one skip or none value, skip the validation altogether
				Skip();
				return true;
			}
		}
	}
	TSharedPtr<FPreflightService> PreflightService = ServiceProvider.Pin()->GetService<FPreflightService>();
	if(!PreflightUpdateHandler.IsValid())
	{
		PreflightUpdateHandler = PreflightService->OnPreflightDataUpdated.AddRaw(this, &FPreflightValidator::ValidatePreflights);
	}

	if(!TagUpdateHandler.IsValid())
	{
		TagUpdateHandler = PreflightTag->OnTagUpdated.AddLambda([this, PreflightService](const FTag&){ ValidatePreflights(PreflightService->GetPreflightData(), PreflightService->GetUnlinkedPreflights()); });
	}

	if(PreflightService->GetPreflightData().IsValid())
	{
		ValidatePreflights(PreflightService->GetPreflightData(), PreflightService->GetUnlinkedPreflights());
	}

	return true;
}

void FPreflightValidator::ValidatePreflights(const TUniquePtr<FPreflightList>& InPreflightListPtr, const TMap<FString, FPreflightData>& InUnlinkedPreflights)
{
	if (!InPreflightListPtr.IsValid())
	{
		return;
	}

	size_t IgnoredPreflights = 0;
	size_t RunningPreflights = 0;
	TArray<const FPreflightData*, TInlineAllocator<8>> ValidPreflights;
	TArray<const FPreflightData*, TInlineAllocator<8>> FailedPreflights;
	TArray<FString, TInlineAllocator<8>> UnverifiedPreflights;

	const TArray<FString>& PreflightValues = PreflightTag->GetValues();
	if(PreflightValues.Num() != 0)
	{
		for(FString PreflightId : PreflightValues)
		{
			if(PreflightId.Equals(TEXT("skip")) || PreflightId.Equals(TEXT("none")))
			{
				// if we have at least one skip or none value, skip the validation altogether
				Skip();
				return;
			}

			if(PreflightId.Contains(TEXT("/")))
			{
				int32 SlashIdx;
				PreflightId.FindLastChar(TCHAR('/'), SlashIdx);
				PreflightId = PreflightId.RightChop(SlashIdx + 1);
			}

			PreflightId.TrimStartAndEndInline();

			FRegexPattern Pattern = FRegexPattern(TEXT("(?:[0-9]|[a-f]){24}"), ERegexPatternFlags::CaseInsensitive);
			FRegexMatcher regex = FRegexMatcher(Pattern, PreflightId);
			bool match = regex.FindNext();
			if(match)
			{
				// Look into the linked preflights and if we don't find it check the unlinked preflights
				const FPreflightData* FoundData = InPreflightListPtr->PreflightList.FindByPredicate([&PreflightId](const FPreflightData& InData) { return InData.ID == PreflightId; }); 
				if(FoundData == nullptr)
				{
					FoundData = InUnlinkedPreflights.Find(PreflightId);
				}

				if(FoundData != nullptr && Definition != nullptr)
				{
					if(Definition->bInvalidatesWhenOutOfDate)
					{
						IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
						for(const FSourceControlStateRef& file : ServiceProvider.Pin()->GetService<FChangelistService>()->GetFilesInCL())
						{
							const FDateTime FileTimestampUTC = PlatformFile.GetTimeStamp(*file->GetFilename());
							if(FileTimestampUTC > FoundData->CreateTime)
							{
								LogFailure(FString::Printf(TEXT("[%s] %s is out of date! Please run a new preflight with the newest set of files."), *GetValidatorName(), *PreflightId));
								FailedPreflights.Add(FoundData);
								continue;
							}
						}
					}

					switch(FoundData->CachedResults.State)
					{
						case EPreflightState::Running:
						case EPreflightState::Ready:
							++RunningPreflights;
							break;

						case EPreflightState::Completed:
							switch(FoundData->CachedResults.Outcome)
							{
								case EPreflightOutcome::Success:
									FoundData->CachedResults.WasSuccessful() ? ValidPreflights.Add(FoundData) : FailedPreflights.Add(FoundData);
									break;
								case EPreflightOutcome::Unspecified:
								case EPreflightOutcome::Failure:
									FailedPreflights.Add(FoundData);
									break;
								case EPreflightOutcome::Warnings:
									if(Definition->bTreatWarningsAsErrors)
									{
										LogFailure(FString::Printf(TEXT("[%s] %s preflight has completed with warnings and they are treated as errors."), *GetValidatorName(), *PreflightId));
										FailedPreflights.Add(FoundData);
									}
									else
									{
										ValidPreflights.Add(FoundData);
									}
									break;
							}
							break;

						case EPreflightState::Skipped:
						case EPreflightState::Unspecified:
							FailedPreflights.Add(FoundData);
							break;

					}
				}
				else
				{
					UnverifiedPreflights.Add(PreflightId);
				}
			}
			else
			{
				UE_LOG(LogValidators, Log, TEXT("[%s] Tag value '%s' is not a valid preflight id or the preflight list is empty."), *GetValidatorName(), *PreflightId);
				++IgnoredPreflights;
			}
		}
	}
	else
	{
		Skip();
		return;
	}

	// Fail early even if other PF are running
	if(!FailedPreflights.IsEmpty())
	{
		for(const FPreflightData* PreflightPtr : FailedPreflights)
		{
			LogFailure(FString::Printf(TEXT("[%s] %s preflight has failed with errors."), *GetValidatorName(), *PreflightPtr->ID));

			for(const FString& ErrorString : PreflightPtr->CachedResults.Errors)
			{
				LogFailure(FString::Printf(TEXT("[%s] Reported error: %s"), *GetValidatorName(), *ErrorString));
			}
		}

		ValidationFinished(false);
	}
	else if(RunningPreflights == 0)
	{
		for(const FString& PreflightId : UnverifiedPreflights)
		{
			UE_LOG(LogValidators, Error, TEXT("[%s] %s preflight can't be verified with Horde, check that the id is correct and there are no connection errors."), *GetValidatorName(), *PreflightId);
			UE_LOG(LogValidatorsResult, Error, TEXT("[%s] %s preflight can't be verified with Horde, check that the id is correct and there are no connection errors."), *GetValidatorName(), *PreflightId);
		}		

		for(const FPreflightData* PreflightPtr : ValidPreflights)
		{
			UE_LOG(LogValidators, Log, TEXT("[%s] %s preflight is valid and has succeeded"), *GetValidatorName(), *PreflightPtr->ID);
			UE_LOG(LogValidatorsResult, Log, TEXT("[%s] %s preflight is valid and has succeeded"), *GetValidatorName(), *PreflightPtr->ID);
		}

		ValidationFinished(ValidPreflights.Num() == (PreflightValues.Num() - IgnoredPreflights));
	}
}

void FPreflightValidator::Skip()
{
	RemoveCallbacks();
	FValidatorBase::Skip();
}

void FPreflightValidator::RemoveCallbacks()
{
	if(PreflightUpdateHandler.IsValid())
	{
		ServiceProvider.Pin()->GetService<FPreflightService>()->OnPreflightDataUpdated.Remove(PreflightUpdateHandler);
		PreflightUpdateHandler.Reset();
	}

	if(TagUpdateHandler.IsValid())
	{
		PreflightTag->OnTagUpdated.Remove(TagUpdateHandler);
		TagUpdateHandler.Reset();
	}
}

void FPreflightValidator::ValidationFinished(bool bSuccess)
{
	RemoveCallbacks();
	FValidatorBase::ValidationFinished(bSuccess);
}
