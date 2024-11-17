// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderCompilerMemoryLimit.cpp: Wrapper for Windows specific Job Object functionality.
=============================================================================*/

#include "ShaderCompilerMemoryLimit.h"
#include "HAL/ConsoleManager.h"


#if PLATFORM_WINDOWS

FWindowsResourceRestrictedJobObject::FWindowsResourceRestrictedJobObject(const FString& InJobName, int32 InitialJobMemoryLimitMiB) :
	JobName(InJobName)
{
	JobObject = ::CreateJobObject(nullptr, *JobName);
	if (JobObject == nullptr)
	{
		UE_LOG(LogWindows, Warning, TEXT("Failed to create job object \"%s\": %s"), *JobName, *GetErrorMessage());
		return;
	}
	CreateAndLinkCompletionPort();
	if (InitialJobMemoryLimitMiB > 0)
	{
		SetMemoryLimit(InitialJobMemoryLimitMiB);
	}
}

FWindowsResourceRestrictedJobObject::~FWindowsResourceRestrictedJobObject()
{
	if (JobObject != nullptr)
	{
		::CloseHandle(JobObject);
	}
	if (CompletionPort != nullptr)
	{
		::CloseHandle(CompletionPort);
	}
}

void FWindowsResourceRestrictedJobObject::AssignProcess(const FProcHandle& Process)
{
	if (IsValid())
	{
		// Assign input process to job object
		if (::AssignProcessToJobObject(JobObject, Process.Get()) == 0)
		{
			UE_LOG(LogWindows, Warning, TEXT("Failed to assign process to job object \"%s\": %s"), *JobName, *GetErrorMessage());
		}
	}
}

void FWindowsResourceRestrictedJobObject::SetMemoryLimit(int32 InJobMemoryLimitMiB)
{
	checkf(InJobMemoryLimitMiB >= 1024, TEXT("Cannot launch ShaderCompileWorker processes with memory restriction of less than 1024 MiB (%u MiB was specified)"), InJobMemoryLimitMiB);
	if (MemoryLimit != InJobMemoryLimitMiB)
	{
		if (IsValid())
		{
			JOBOBJECT_NOTIFICATION_LIMIT_INFORMATION JobLimitInfo = {};

			JobLimitInfo.JobMemoryLimit = 1024 * 1024 * static_cast<SIZE_T>(InJobMemoryLimitMiB);
			JobLimitInfo.LimitFlags = JOB_OBJECT_LIMIT_JOB_MEMORY;

			if (::SetInformationJobObject(JobObject, JobObjectNotificationLimitInformation, &JobLimitInfo, sizeof(JobLimitInfo)) == 0)
			{
				UE_LOG(LogWindows, Warning, TEXT("Failed to set restriction information for job object \"%s\": %s"), *JobName, *GetErrorMessage());
			}
		}
		MemoryLimit = InJobMemoryLimitMiB;
	}
}

bool FWindowsResourceRestrictedJobObject::QueryStatus(FJobObjectLimitationInfo& OutInfo)
{
	JOBOBJECT_LIMIT_VIOLATION_INFORMATION LimitViolationInfo = {};
	DWORD ReturnLength = 0;
	if (::QueryInformationJobObject(JobObject, JobObjectLimitViolationInformation, &LimitViolationInfo, sizeof(LimitViolationInfo), &ReturnLength) != 0 &&
		ReturnLength == sizeof(LimitViolationInfo) &&
		LimitViolationInfo.JobMemoryLimit > 0)
	{
		OutInfo.MemoryLimit = LimitViolationInfo.JobMemoryLimit;
		OutInfo.MemoryUsed = LimitViolationInfo.JobMemory;
		return true;
	}
	return false;
}

bool FWindowsResourceRestrictedJobObject::QueryLimitViolationStatus(FJobObjectLimitationInfo& OutInfo)
{
	if (IsValid())
	{
		// Check if a packet has been dequeued from the IO completion port
		constexpr DWORD WaitDurationInMilliseconds = 0;
		DWORD NumBytesTransferred = 0;
		ULONG_PTR CompletionKey = 0;
		LPOVERLAPPED Overlapped = nullptr;
		while (::GetQueuedCompletionStatus(CompletionPort, &NumBytesTransferred, &CompletionKey, &Overlapped, WaitDurationInMilliseconds) != 0)
		{
			// Check if this is the message we are looking for, i.e. notification about limiation violation
			if (NumBytesTransferred == JOB_OBJECT_MSG_NOTIFICATION_LIMIT)
			{
				// Query violation information from job object
				JOBOBJECT_LIMIT_VIOLATION_INFORMATION LimitViolationInfo = {};
				DWORD ReturnLength = 0;
				if (::QueryInformationJobObject(JobObject, JobObjectLimitViolationInformation, &LimitViolationInfo, sizeof(LimitViolationInfo), &ReturnLength) != 0 &&
					ReturnLength == sizeof(LimitViolationInfo))
				{
					// Even though the limit has been exceeded as indicated by the JOB_OBJECT_MSG_NOTIFICATION_LIMIT message,
					// by the time we query the information it might have already gone down again,
					// so check if the memory information is actually exceeding our limit here.
					if (LimitViolationInfo.JobMemory >= LimitViolationInfo.JobMemoryLimit)
					{
						OutInfo.MemoryLimit = LimitViolationInfo.JobMemoryLimit;
						OutInfo.MemoryUsed = LimitViolationInfo.JobMemory;
						return true;
					}
				}
			}
		}
	}
	return false;
}

FString FWindowsResourceRestrictedJobObject::GetErrorMessage()
{
	TCHAR ErrorMessage[4096];
	return FString(FPlatformMisc::GetSystemErrorMessage(ErrorMessage, UE_ARRAY_COUNT(ErrorMessage), 0));
}

bool FWindowsResourceRestrictedJobObject::IsValid() const
{
	return JobObject != nullptr && CompletionPort != nullptr;
}

void FWindowsResourceRestrictedJobObject::CreateAndLinkCompletionPort()
{
	// Create completion port
	CompletionPort = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
	if (CompletionPort == nullptr)
	{
		UE_LOG(LogWindows, Warning, TEXT("Failed to create I/O completion port for job object \"%s\": %s"), *JobName, *GetErrorMessage());
		return;
	}

	// Link completion port to job object
	JOBOBJECT_ASSOCIATE_COMPLETION_PORT CompletionPortInfo = {};

	CompletionPortInfo.CompletionKey = JobObject;
	CompletionPortInfo.CompletionPort = CompletionPort;

	if (::SetInformationJobObject(JobObject, JobObjectAssociateCompletionPortInformation, &CompletionPortInfo, sizeof(CompletionPortInfo)) == 0)
	{
		UE_LOG(LogWindows, Warning, TEXT("Failed to set completion port for job object \"%s\": %s"), *JobName, *GetErrorMessage());
	}
}

#else

FGenericResourceRestrictedJobObject::FGenericResourceRestrictedJobObject(const FString& JobName, int32 InitialJobMemoryLimitMiB)
{
	// dummy
}

void FGenericResourceRestrictedJobObject::AssignProcess(const FProcHandle& Process)
{
	// dummy
}

void FGenericResourceRestrictedJobObject::SetMemoryLimit(int32 InJobMemoryLimitMiB)
{
	// dummy
}

bool FGenericResourceRestrictedJobObject::QueryStatus(FJobObjectLimitationInfo& OutInfo)
{
	return false; // dummy
}

bool FGenericResourceRestrictedJobObject::QueryLimitViolationStatus(FJobObjectLimitationInfo& OutInfo)
{
	return false; // dummy
}

#endif // PLATFORM_WINDOWS



