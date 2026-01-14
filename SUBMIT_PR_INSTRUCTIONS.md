# Instructions to Submit the PR for docling-parse boolean_t Fix

## Step 1: Create a Fork on GitHub

1. Go to https://github.com/DS4SD/docling-parse
2. Click the "Fork" button in the top-right corner
3. Select your GitHub account (timblaktu) as the destination

## Step 2: Add Your Fork as a Remote

```bash
cd /home/tim/src/docling-parse
git remote add fork https://github.com/timblaktu/docling-parse.git
```

## Step 3: Push Your Branch to Your Fork

```bash
git push fork fix/boolean-t-wrapper
```

## Step 4: Create the Pull Request

### Option A: Using GitHub CLI (after authentication)
```bash
# First authenticate if needed:
gh auth login

# Then create PR:
gh pr create \
  --repo DS4SD/docling-parse \
  --base main \
  --head timblaktu:fix/boolean-t-wrapper \
  --title "Fix: Use nlohmann::json::boolean_t wrapper for bool conversion" \
  --body-file PR_DESCRIPTION.md
```

### Option B: Using GitHub Web Interface

1. Go to https://github.com/DS4SD/docling-parse
2. You should see a banner saying "timblaktu:fix/boolean-t-wrapper had recent pushes"
3. Click "Compare & pull request"
4. Use the title: "Fix: Use nlohmann::json::boolean_t wrapper for bool conversion"
5. Copy the contents of PR_DESCRIPTION.md into the PR description
6. Click "Create pull request"

## Additional Context for Maintainers

You may want to mention in the PR comments that:

1. This fix enables docling-parse to be packaged in NixOS/nixpkgs
2. The issue affects any build environment using C++20 with strict template resolution
3. The fix is minimal and follows nlohmann_json's recommended practices
4. You've tested this extensively in the NixOS packaging context

## After PR Creation

1. Monitor for CI/CD results
2. Address any reviewer feedback
3. Once merged, update nixpkgs to remove the patch and reference the upstream fix

## Tracking the Fix in nixpkgs

After the PR is merged, you'll need to:
1. Update the docling-parse package in nixpkgs to use the new version
2. Remove the patch from `/home/tim/src/nixcfg/pkgs/patches/`
3. Submit a PR to nixpkgs updating the package