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

static const FName BPCheckTabId = "BPCheckSuggestions";

class SBPCheckSuggestionsPanel;

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

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(BPCheckTabId, FOnSpawnTab::CreateRaw(this, &FBPCheckEditorModule::CreateSuggestionsTab)).SetDisplayName(LOCTEXT("BPCheckSuggestionsList", "BPCheck Suggestions"));
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
		LOCTEXT("BPCheckLabel", "BPCheck"),
		LOCTEXT("BPCheckTooltip", "See BPCheck Suggestions"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.ViewOptions")
	));
}

void FBPCheckEditorModule::OnButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(BPCheckTabId);
}

void FBPCheckEditorModule::SetSuggestionsForBlueprint(const UBlueprint* Blueprint, TArray<FSuggestionContext>& NewSuggestions)
{
	Suggestions.Add(Blueprint, MoveTemp(NewSuggestions));

	if (TSharedPtr<SBPCheckSuggestionsPanel> P = SuggestionsPanel.Pin())
	{
		P->SetResults(BuildTree());
	}
}

TSharedRef<SDockTab> FBPCheckEditorModule::CreateSuggestionsTab(const FSpawnTabArgs&)
{
	TSharedRef<SBPCheckSuggestionsPanel> Panel = SNew(SBPCheckSuggestionsPanel);
	SuggestionsPanel = Panel;
	Panel->SetResults(BuildTree());
	return SNew(SDockTab).TabRole(NomadTab)[Panel];
}

TArray<TSharedPtr<FResultTreeItem>> FBPCheckEditorModule::BuildTree() const
{
	TArray<TSharedPtr<FResultTreeItem>> Roots;

	for (const auto& Pair : Suggestions)
	{
		const UBlueprint* BP = Pair.Key;
		if (!BP || Pair.Value.Num() == 0) continue;

		TSharedPtr<FResultTreeItem> Group = MakeShared<FResultTreeItem>();
		Group->Label = FString::Printf(TEXT("%s (%d)"), *BP->GetName(), Pair.Value.Num());

		for (const FSuggestionContext& S : Pair.Value)
		{
			const FSuggestionInfo& Info = GetSuggestionInfo(S.Suggestion);

			TSharedPtr<FResultTreeItem> Leaf = MakeShared<FResultTreeItem>();
			Leaf->Label = FString::Printf(TEXT("%s  %s"), *Info.SuggestionCode, *Info.SuggestionDescription);
			Leaf->Node = BPCheck::Editor::ResolveNode(BP, S.NodeGuid);
			Group->Children.Add(Leaf);
		}
		Roots.Add(Group);
	}
	return Roots;
}

void SBPCheckSuggestionsPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SAssignNew(TreeView, STreeView<TSharedPtr<FResultTreeItem>>)
				.TreeItemsSource(&RootNodes)
				.OnGenerateRow(this, &SBPCheckSuggestionsPanel::OnGenerateRow)
				.OnGetChildren(this, &SBPCheckSuggestionsPanel::OnGetChildren)
				.SelectionMode(ESelectionMode::Single)
				.OnMouseButtonDoubleClick(this, &SBPCheckSuggestionsPanel::OnItemDoubleClicked)
		];
}

void SBPCheckSuggestionsPanel::SetResults(TArray<TSharedPtr<FResultTreeItem>> InNodes)
{
	RootNodes = MoveTemp(InNodes);

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();

		for (const TSharedPtr<FResultTreeItem>& Node : RootNodes)
			TreeView->SetItemExpansion(Node, true);
	}
}

TSharedRef<ITableRow> SBPCheckSuggestionsPanel::OnGenerateRow(TSharedPtr<FResultTreeItem> Node, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FResultTreeItem>>, Owner)
		[
			SNew(STextBlock).Text(FText::FromString(Node->Label))
		];
}

void SBPCheckSuggestionsPanel::OnGetChildren(TSharedPtr<FResultTreeItem> Node, TArray<TSharedPtr<FResultTreeItem>>& OutChildren)
{
	if (Node.IsValid())
	{
		OutChildren = Node->Children;
	}
}

void SBPCheckSuggestionsPanel::OnItemDoubleClicked(TSharedPtr<FResultTreeItem> Item)
{
	if (!Item.IsValid()) return;

	const UObject* Resolved = Item->Node.Get();
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Resolved);
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

	FModuleManager::GetModuleChecked<FBPCheckEditorModule>("BPCheckEditor").SetSuggestionsForBlueprint(CompilationContext.Blueprint, Result.Suggestions);

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