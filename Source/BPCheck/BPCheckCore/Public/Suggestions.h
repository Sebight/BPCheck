#pragma once

#include "CoreMinimal.h"

using FSuggestionCode = FString;

enum class ESuggestion
{
	PrintInHotPath,
	HardCastToBPClass,
	CastWithoutFailHandler,
	Disconnected,
	Unreachable
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
		{ ESuggestion::Unreachable, {"BP005", TEXT("Execution chain starting here is unreachable and will never run.")} }
	};

	return Data[S];
}