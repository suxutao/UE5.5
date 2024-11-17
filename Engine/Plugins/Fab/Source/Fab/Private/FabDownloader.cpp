// Copyright Epic Games, Inc. All Rights Reserved.

#include "FabDownloader.h"

#include "FabLog.h"
#include "HttpModule.h"

#include "Importers/BuildPatchInstallerLibHelper.h"

#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IPluginManager.h"

#include "Misc/DateTime.h"
#include "Misc/Timespan.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#include "Runtime/Launch/Resources/Version.h"

#include "Utilities/FabAssetsCache.h"

TUniquePtr<BpiLib::IBpiLib> FFabDownloadRequest::BuildPatchServices;
FTSTicker::FDelegateHandle FFabDownloadRequest::BpsTickerHandle;

FFabDownloadRequest::FFabDownloadRequest(const FString& InAssetID, const FString& InDownloadURL, const FString& InDownloadLocation, EFabDownloadType InDownloadType)
	: AssetID(InAssetID)
	, DownloadURL(InDownloadURL)
	, DownloadLocation(InDownloadLocation)
	, DownloadType(InDownloadType)
{}

bool FFabDownloadRequest::LoadBuildPatchServices()
{
	if (BuildPatchServices)
	{
		return true;
	}

	constexpr auto WinLibName   = TEXT("BuildPatchInstallerLib.dll");
	constexpr auto LinuxLibName = TEXT("libBuildPatchInstallerLib.so");
	constexpr auto MacArmLibName   = TEXT("BuildPatchInstallerLib-arm.dylib");
	constexpr auto Macx86LibName   = TEXT("BuildPatchInstallerLib-x86.dylib");
	constexpr auto DLLName      = PLATFORM_WINDOWS ? WinLibName : PLATFORM_LINUX ? LinuxLibName : PLATFORM_MAC_ARM64 ? MacArmLibName : PLATFORM_MAC_X86 ? Macx86LibName : nullptr;
	if constexpr (DLLName == nullptr)
	{
		return false;
	}

	const FString PluginPath = IPluginManager::Get().FindPlugin(TEXT("Fab"))->GetBaseDir();
	const FString LibPath    = FPaths::ConvertRelativePathToFull(FPaths::Combine(PluginPath, TEXT("ThirdParty"), DLLName));

	BuildPatchServices = BpiLib::FBpiLibHelperFactory::Create(LibPath);
	if (BuildPatchServices)
	{
		BpsTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda(
				[](const float Delta)
				{
					BuildPatchServices->Tick(Delta);
					return true;
				}
			)
		);
	}

	return BuildPatchServices != nullptr;
}

void FFabDownloadRequest::ShutdownBpsModule()
{
	if (BpsTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(BpsTickerHandle);
	}
	if (BuildPatchServices.IsValid())
	{
		BuildPatchServices.Reset();
	}
}

void FFabDownloadRequest::ExecuteHTTPRequest()
{
	const FString FullFileName = GetFilenameFromURL(DownloadURL);
	const FString SaveFilename = DownloadLocation / GetFilenameFromURL(DownloadURL);

	FHttpModule& HTTPModule = FHttpModule::Get();

	DownloadRequest = HTTPModule.CreateRequest().ToSharedPtr();
	DownloadRequest->SetURL(DownloadURL);
	DownloadRequest->OnHeaderReceived().BindLambda(
		[this, FullFileName](FHttpRequestPtr Request, const FString& HeaderName, const FString& HeaderValue)
		{
			if (HeaderName == "Content-Length")
			{
				DownloadStats.TotalBytes    = FCString::Atoi(*HeaderValue);
				const FString CachedAssetId = AssetID / FullFileName;
				OnDownloadProgressDelegate.Broadcast(this, DownloadStats);

				if (FFabAssetsCache::IsCached(CachedAssetId, DownloadStats.TotalBytes))
				{
					DownloadStats.PercentComplete     = 100.0f;
					DownloadStats.DownloadCompletedAt = FDateTime::Now().ToUnixTimestamp();
					DownloadStats.bIsSuccess          = true;
					DownloadStats.CompletedBytes      = DownloadStats.TotalBytes;
					DownloadStats.DownloadedFiles     = {
						FFabAssetsCache::GetCachedFile(CachedAssetId)
					};

					Request->CancelRequest();
				}
			}
		}
	);
	#if (ENGINE_MAJOR_VERSION >=5 && ENGINE_MINOR_VERSION <=3)
	DownloadRequest->OnRequestProgress().BindLambda(
		[this](FHttpRequestPtr Request, uint32 UploadedBytes, uint32 DownloadedBytes)
		#else
	DownloadRequest->OnRequestProgress64().BindLambda(
		[this](FHttpRequestPtr Request, uint64 UploadedBytes, uint64 DownloadedBytes)
		#endif
		{
			DownloadStats.CompletedBytes  = static_cast<uint64>(DownloadedBytes);
			DownloadStats.PercentComplete = 100.0f * static_cast<float>(DownloadStats.CompletedBytes) / static_cast<float>(DownloadStats.TotalBytes);

			// TODO: Calculate Download Speed
			OnDownloadProgressDelegate.Broadcast(this, DownloadStats);
		}
	);
	DownloadRequest->OnProcessRequestComplete().BindLambda(
		[this, SaveFilename](FHttpRequestPtr Request, FHttpResponsePtr Response, const bool bRequestComplete)
		{
			if (bRequestComplete)
			{
				const TArray<uint8>& Data         = Response->GetContent();
				DownloadStats.bIsSuccess          = FFileHelper::SaveArrayToFile(Data, *SaveFilename);
				DownloadStats.DownloadCompletedAt = FDateTime::Now().ToUnixTimestamp();
				if (DownloadStats.bIsSuccess)
				{
					DownloadStats.DownloadedFiles = {
						SaveFilename
					};
				}
			}

			OnDownloadCompleteDelegate.Broadcast(this, DownloadStats);
		}
	);

	DownloadStats.DownloadStartedAt = FDateTime::Now().ToUnixTimestamp();
	DownloadRequest->ProcessRequest();
}

void FFabDownloadRequest::ExecuteBuildPatchRequest()
{
	DownloadStats.DownloadStartedAt = FDateTime::Now().ToUnixTimestamp();
	if (!LoadBuildPatchServices())
	{
		FAB_LOG_ERROR("Failed to load BuildPatchServicesModule");
		DownloadStats.bIsSuccess = false;
		OnDownloadCompleteDelegate.Broadcast(this, DownloadStats);
		return;
	}

	FString ManifestURL, BaseURL;
	DownloadURL.Split(",", &ManifestURL, &BaseURL, ESearchCase::CaseSensitive);
	FHttpModule& HTTPModule = FHttpModule::Get();

	DownloadRequest = HTTPModule.CreateRequest().ToSharedPtr();
	DownloadRequest->SetURL(ManifestURL);
	DownloadRequest->OnProcessRequestComplete().BindLambda(
		[this, BaseURL](FHttpRequestPtr Request, FHttpResponsePtr Response, const bool bRequestComplete)
		{
			if (bRequestComplete)
			{
				ManifestData = Response->GetContent();
				OnManifestDownloaded(BaseURL);
			}
			else
			{
				DownloadStats.bIsSuccess = false;
				OnDownloadCompleteDelegate.Broadcast(this, DownloadStats);
			}
		}
	);

	DownloadRequest->ProcessRequest();
}

void FFabDownloadRequest::OnManifestDownloaded(const FString& BaseURL)
{
	if (bPendingCancel)
	{
		DownloadStats.bIsSuccess = false;
		OnDownloadCompleteDelegate.Broadcast(this, DownloadStats);
		return;
	}

	BuildPatchServices::FBuildInstallerConfiguration BuildInstallerConfiguration({});
	BuildInstallerConfiguration.InstallDirectory = DownloadLocation;
	BuildInstallerConfiguration.StagingDirectory = FFabAssetsCache::GetCacheLocation() / AssetID;
	BuildInstallerConfiguration.InstallMode      = BuildPatchServices::EInstallMode::NonDestructiveInstall;
	BaseURL.ParseIntoArray(BuildInstallerConfiguration.CloudDirectories, TEXT(","));

	auto Manifest = BuildPatchServices->MakeManifestFromData(ManifestData);
	if (!Manifest.IsValid())
	{
		FAB_LOG_ERROR("Invalid Manifest");
		DownloadStats.bIsSuccess = false;
		OnDownloadCompleteDelegate.Broadcast(this, DownloadStats);
		return;
	}

	DownloadStats.DownloadedFiles = Manifest.GetBuildFileList();
	if (DownloadStats.DownloadedFiles.ContainsByPredicate(
		[](const FString& File)
		{
			const FString Ext = FPaths::GetExtension(File);
			return Ext == "uproject" || Ext == "uplugin";
		}
	))
	{
		FAB_LOG_ERROR("Invalid pack - either contains a uproject or a uplugin file");
		DownloadStats.bIsSuccess = false;
		DownloadStats.DownloadedFiles.Empty();
		OnDownloadCompleteDelegate.Broadcast(this, DownloadStats);
		return;
	}

	auto OnComplete = FBuildPatchInstallerDelegate::CreateLambda(
		[this](const IBuildInstallerRef& Installer)
		{
			DownloadStats.DownloadCompletedAt = FDateTime::Now().ToUnixTimestamp();
			DownloadStats.PercentComplete     = 100.0f;
			DownloadStats.bIsSuccess          = true;
			if (BpsProgressTickerHandle.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(BpsProgressTickerHandle);
			}
			OnDownloadCompleteDelegate.Broadcast(this, DownloadStats);
		}
	);
	BpsInstaller = BuildPatchServices->CreateInstaller(Manifest, MoveTemp(BuildInstallerConfiguration), MoveTemp(OnComplete)).ToSharedPtr();
	BpsInstaller->StartInstallation();

	auto OnProgress = FTickerDelegate::CreateLambda(
		[this, Installer = BpsInstaller.ToSharedRef()](const float Delta)
		{
			const int64 TotalDownloaded = BuildPatchServices->GetTotalDownloaded(Installer);
			// const float UpdateProgress        = BuildPatchServices->GetUpdateProgress(Installer);
			const int64 TotalDownloadRequired = BuildPatchServices->GetTotalDownloadRequired(Installer);
			DownloadStats.CompletedBytes      = TotalDownloaded;
			DownloadStats.TotalBytes          = TotalDownloadRequired;
			DownloadStats.PercentComplete     = (static_cast<float>(DownloadStats.CompletedBytes) / DownloadStats.TotalBytes) * 100.0f;


			OnDownloadProgressDelegate.Broadcast(this, DownloadStats);
			return true;
		}
	);
	BpsProgressTickerHandle = FTSTicker::GetCoreTicker().AddTicker(MoveTemp(OnProgress), 1.0f);
}

FString FFabDownloadRequest::GetFilenameFromURL(const FString& URL)
{
	int32 SlashIndex = -1;
	if (!URL.FindLastChar('/', SlashIndex) || SlashIndex == -1)
	{
		SlashIndex = 0; // Local file without scheme maybe?
	}

	int32 QuestionIndex = -1;
	if (!URL.FindChar('?', QuestionIndex) || QuestionIndex == -1)
	{
		QuestionIndex = URL.Len();
	}
	return URL.Mid(SlashIndex + 1, QuestionIndex - 1 - SlashIndex);
}

void FFabDownloadRequest::StartDownload()
{
	if (bPendingCancel)
	{
		DownloadStats.bIsSuccess = false;
		OnDownloadCompleteDelegate.Broadcast(this, DownloadStats);
		return;
	}
	if (DownloadType == EFabDownloadType::HTTP)
	{
		ExecuteHTTPRequest();
	}
	else if (DownloadType == EFabDownloadType::BuildPatchRequest)
	{
		ExecuteBuildPatchRequest();
	}
}

void FFabDownloadRequest::ExecuteRequest()
{
	FFabDownloadQueue::AddDownloadToQueue(this);
}

void FFabDownloadRequest::Cancel()
{
	bool bWasCancelled = false;
	if (DownloadRequest.IsValid() && DownloadRequest->GetStatus() == EHttpRequestStatus::Processing)
	{
		DownloadStats.bIsSuccess = false;
		DownloadStats.DownloadedFiles.Empty();
		DownloadRequest->CancelRequest();
		bWasCancelled = true;
	}
	if (BpsInstaller.IsValid() && !BpsInstaller->IsComplete() && !BpsInstaller->IsCanceled())
	{
		DownloadStats.bIsSuccess = false;
		DownloadStats.DownloadedFiles.Empty();
		BuildPatchServices->CancelInstall(BpsInstaller.ToSharedRef());
		bWasCancelled = true;
	}
	if (!bWasCancelled)
	{
		bPendingCancel = true;
	}
}

int32 FFabDownloadQueue::DownloadQueueLimit = 2;
TSet<FFabDownloadRequest*> FFabDownloadQueue::DownloadQueue;
TQueue<FFabDownloadRequest*> FFabDownloadQueue::WaitingQueue;

void FFabDownloadQueue::AddDownloadToQueue(FFabDownloadRequest* DownloadRequest)
{
	if (DownloadQueue.Num() >= DownloadQueueLimit)
	{
		WaitingQueue.Enqueue(DownloadRequest);
	}
	else
	{
		DownloadQueue.Add(DownloadRequest);
		DownloadRequest->OnDownloadComplete().AddLambda(
			[DownloadRequest](const FFabDownloadRequest* Request, const FFabDownloadStats& Stats)
			{
				DownloadQueue.Remove(DownloadRequest);
				if (FFabDownloadRequest* NewRequest = nullptr; WaitingQueue.Dequeue(NewRequest))
				{
					AddDownloadToQueue(NewRequest);
				}
			}
		);
		DownloadRequest->StartDownload();
	}
}
