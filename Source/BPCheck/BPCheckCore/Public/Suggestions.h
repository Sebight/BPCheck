#pragma once

#include "CoreMinimal.h"

using FSuggestionCode = FString;

UENUM()
enum class ESuggestion : uint8
{
	PrintInHotPath			UMETA(DisplayName = "Print In Hot Path"),
	HardCastToBPClass		UMETA(DisplayName = "Hard Cast To BP Class"),
	CastWithoutFailHandler	UMETA(DisplayName = "Cast Without Fail Handler"),
	Disconnected			UMETA(DisplayName = "Disconnected Node"),
	Unreachable				UMETA(DisplayName = "Unreachable"),
	HotGetActorsOfClass		UMETA(DisplayName = "GetAllActorsOfClass In Tick"),
	LatentNodeInLoop		UMETA(DisplayName = "Latent Node In Loop")
};

struct FSuggestionInfo
{
	FSuggestionCode SuggestionCode;
	FString SuggestionDescription;
};

inline const FSuggestionInfo& GetSuggestionInfo(ESuggestion S)
{
	static const TMap<ESuggestion, FSuggestionInfo> Data = {
		{ ESuggestion::PrintInHotPath, {"BP001", TEXT("Print node in hot path can cause performance issues.")} },
		{ ESuggestion::HardCastToBPClass, {"BP002", TEXT("Cast To a Blueprint class creates a hard reference that loads the target and its dependencies whenever this Blueprint loads.")} },
		{ ESuggestion::CastWithoutFailHandler, {"BP003", TEXT("Cast Failed pin is no connected: a failed cast continues with a null result. Connect the pin or guard the output with IsValid.")} },
		{ ESuggestion::Disconnected, {"BP004", TEXT("Node is disconnected from the graph. It has no effect and can be deleted.")} },
		{ ESuggestion::Unreachable, {"BP005", TEXT("Execution chain starting here is unreachable and will never run.")} },
		{ ESuggestion::HotGetActorsOfClass, {"BP006", TEXT("'{Node}' in Tick performs a full world scan every frame, cache in BeginPlay if possible.")} },
		{ ESuggestion::LatentNodeInLoop, {"BP007", TEXT("'{Node}' in loop body triggers all iterations in one frame. Use recursion or a timer instead.")} }
	};

	return Data[S];
}