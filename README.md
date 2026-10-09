<h1 align="center">
    <div align="center">
        <img width="600" alt="bpcheck_logo" src="https://github.com/user-attachments/assets/a7aa43ad-dd7c-4499-8dbb-53bf63b6e32c" />
    </div>
</h1>

# BPCheck

BPCheck is an Unreal Engine plugin that scans your Blueprints for potential performance and architecture problems.

## What it does

BPCheck statically analyzes your Blueprints and flags common issues before they become problems in your project.

The plugin covers **architecture** and **performance**. Casting to a Blueprint class makes a hard reference, so loading one asset loads everything it casts to, and BPCheck warns you against that. It also catches patterns that tend to cost frame time or cause unsafe behavior, so you can fix them early instead of hunting them down later.

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
