// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "BlueprintCompilerExtension.h"
#include "Commandlets/Commandlet.h"

#include "BPCheckCore.h"
#include "IDetailCustomization.h"

#include "BPCheckEditor.generated.h"

struct FResultTreeItem;
DECLARE_LOG_CATEGORY_EXTERN(LogBPCheckEditor, Log, All);

namespace BPCheck::Editor
{
	UEdGraphNode* ResolveNode(const UBlueprint* Blueprint, FGuid NodeGuid);
}

class SBPCheckSuggestionsPanel;
class FBPCheckEditorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void CreateMenuItem();
	void OnButtonClicked();
	void OnSettingsChanged();

	void SetSuggestionsForBlueprint(const UBlueprint* Blueprint, TArray<FSuggestionContext>& NewSuggestions);

private:
	TSharedRef<SDockTab> CreateSuggestionsTab(const FSpawnTabArgs&);
	TArray<TSharedPtr<FResultTreeItem>> BuildTree() const;
	void ApplyNodeMarkers();
	static void BuildNodeSection(UToolMenu* InMenu);

	TWeakPtr<SBPCheckSuggestionsPanel> SuggestionsPanel;
	TMap<const UBlueprint*, TArray<FSuggestionContext>> Suggestions;
	FDelegateHandle SettingsChangedHandle;
};

enum class EItemSeverity : uint8 { Info, Warning, Error, Group };

struct FResultTreeItem
{
	// BP Name or Suggestion Code
	FString Label;
	FString Description;
	EItemSeverity Severity;
	bool bIsGroup = false;
	TWeakObjectPtr<UEdGraphNode> Node;
	TArray<TSharedPtr<FResultTreeItem>> Children;
};

class SBPCheckSuggestionsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBPCheckSuggestionsPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetResults(TArray<TSharedPtr<FResultTreeItem>> InNodes);

private:
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FResultTreeItem> Node, const TSharedRef<STableViewBase>& Owner);
	void OnGetChildren(TSharedPtr<FResultTreeItem> Node, TArray<TSharedPtr<FResultTreeItem>>& OutChildren);

	TArray<TSharedPtr<FResultTreeItem>> RootNodes;
	TSharedPtr<STreeView<TSharedPtr<FResultTreeItem>>> TreeView;

	void OnItemDoubleClicked(TSharedPtr<FResultTreeItem> Item);
};

UCLASS()
class BPCHECKEDITOR_API UBPCompilerExtension : public UBlueprintCompilerExtension
{
	GENERATED_BODY()

protected:
	virtual void ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data) override;
};

UCLASS()
class BPCHECKEDITOR_API UBPCheckCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString&) override;
};

class FBPCheckSettingsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FBPCheckSettingsCustomization>();
	}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};