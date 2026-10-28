<h1 align="center">
    <div align="center">
        <img width="850" alt="bpcheck_logo" src="https://github.com/user-attachments/assets/2b4fe264-46df-4731-a5cd-63a74bb6735b#gh-light-mode-only" />
        <img width="450" alt="bpcheck_logo" src="https://github.com/user-attachments/assets/a7aa43ad-dd7c-4499-8dbb-53bf63b6e32c#gh-dark-mode-only" />
    </div>
</h1>

# BPCheck

BPCheck is an Unreal Engine plugin that scans your Blueprints for potential performance and architecture problems.

## What it does

Blueprints make it easy to introduce performance overhead, memory bloat, or unintended behavior without noticing. BPCheck detects these patterns during compilation and reports them directly to you, so you can fix them early instead of hunting them down later.

<div align="center">
    <img width="850" alt="bpcheck_logo" src="https://github.com/user-attachments/assets/f5bd9570-a56c-4e3d-ac36-8151d8882f96" />
</div>

## Install
1. Copy the BPCheck folder into your project's Plugins folder.
2. Open the project and enable the plugin in the Plugins window.
3. Restart the editor.

## Usage
Compile your Blueprints and then check out the BPCheck Suggestions window. You will see entries of issues that point directly to the nodes, graphs or Blueprints.

## Engine version
I developed the plugin at UE 5.8, but I expect that it should work with earlier versions of UE too.

## License
MIT
