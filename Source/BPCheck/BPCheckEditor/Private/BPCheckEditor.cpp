// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPCheckEditor.h"
#include "ToolMenus.h"
#include "BPCheckCore.h"
#include "BlueprintCompilationManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "FBPCheckModule"

DEFINE_LOG_CATEGORY(LogBPCheckEditor);

UEdGraphNode* BPCheck::Editor::ResolveNode(const UBlueprint* Blueprint, FGuid NodeGuid)
{
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);

	for (auto* Graph : Graphs)
	{
		for (auto Node : Graph->Nodes)
		{
			if (Node->NodeGuid == NodeGuid) return Node;
		}
	}

	return nullptr;
}

void FBPCheckEditorModule::StartupModule()
{
	UE_LOG(LogBPCheckEditor, Log, TEXT("BPCheck-Editor started."));
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBPCheckEditorModule::CreateMenuItem));

	UBPCompilerExtension* Extension = NewObject<UBPCompilerExtension>();
	FBlueprintCompilationManager::RegisterCompilerExtension(UBlueprint::StaticClass(), Extension);
}

void FBPCheckEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FBPCheckEditorModule::CreateMenuItem()
{
	FToolMenuOwnerScoped Owner(this);

	UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	FToolMenuSection& Section = ToolBar->FindOrAddSection("BPCheck");

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"BPCheckButton",
		FUIAction(FExecuteAction::CreateRaw(this, &FBPCheckEditorModule::OnButtonClicked)),
		LOCTEXT("BPCheckLabel", "BP Check"),
		LOCTEXT("BPCheckTooltip", "Run blueprint checks"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.ViewOptions")
	));
}

void FBPCheckEditorModule::OnButtonClicked()
{
}

void UBPCompilerExtension::ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data)
{
	if (!CompilationContext.Blueprint->GetPackage()->GetName().StartsWith("/Game/"))
	{
		// This skips analysis of any engine side Blueprints
		// TODO(Seb): This also skips all plugins, which might be an issue
		return;
	}

	double Seconds = 0.0;
	FAnalyzerResult Result;
	{
		FScopedDurationTimer Timer(Seconds);
		FAnalyzer Analyzer;
		Result = Analyzer.Analyze(CompilationContext, Data);
	}


	for (auto& Suggestion : Result.Suggestions)
	{
		UEdGraphNode* Node = BPCheck::Editor::ResolveNode(CompilationContext.Blueprint, Suggestion.NodeGuid);
		if (!Node)
		{
			// Couldn't properly resolve the reported node? Should probably warn...
		}

		const FSuggestionInfo& Info = GetSuggestionInfo(Suggestion.Suggestion);


		TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Warning);
		Msg->AddToken(FTextToken::Create(FText::FromString(Info.SuggestionCode)));

		// Note(Seb): I wasn't able to make it work without adding the Callback, which forces the open in the node editor...
		Msg->AddToken(FUObjectToken::Create(Node, Node->GetNodeTitle(ENodeTitleType::ListView))->OnMessageTokenActivated(
			FOnMessageTokenActivated::CreateLambda([](const TSharedRef<IMessageToken>& Token)
				{
					const TSharedRef<FUObjectToken> Obj = StaticCastSharedRef<FUObjectToken>(Token);
					if (UObject* Resolved = Obj->GetObject().Get())
						FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Resolved);
				})));

		Msg->AddToken(FTextToken::Create(FText::FromString(Info.SuggestionDescription)));

		FMessageLog("BPCheck").AddMessage(Msg);
	}

	Super::ProcessBlueprintCompiled(CompilationContext, Data);
}

// Run using "unreal_bin" "project_path" -run=BPCheck -NoShaderCompile -AllowStdOutLogVerbosity 2>&1 | Select-String "BPCheck"
int32 UBPCheckCommandlet::Main(const FString& String)
{
	UE_LOG(LogBPCheckEditor, Log, TEXT("Running BPCheck Commandlet."));

	auto& AssetRegistryMod = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	auto& AssetRegistry = AssetRegistryMod.Get();

	TArray<FAssetData> FoundAssets;
	AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), FoundAssets);

	TArray<FAnalyzerResult> Results;

	for (const auto& AssetData : FoundAssets)
	{
		if (!AssetData.GetPackage()->GetName().StartsWith("/Game/"))
		{
			// Ditto with the TODO above
			continue;
		}

		UBlueprint* AssetObject = Cast<UBlueprint>(AssetData.GetAsset());

		if (!AssetObject)
		{
			continue;
		}

		FAnalyzer Analyzer;
		Results.Push(Analyzer.Analyze(AssetObject));
	}

	FAnalyzerReport Report;
	Report.Results = MoveTemp(Results);

	FString Json;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(FJsonObjectConverter::UStructToJsonObject(Report).ToSharedRef(), Writer);
	UE_LOG(LogBPCheckEditor, Log, TEXT("%s"), *Json);

	return 0;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBPCheckEditorModule, BPCheckEditor)