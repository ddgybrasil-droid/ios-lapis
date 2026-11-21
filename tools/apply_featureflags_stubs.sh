#!/bin/bash
# Seed empty FeatureFlags Domain/Settings plists so init_featureflags + launchd
# stop ENOENT-spamming missing Unified/Domain and Preferences FeatureFlags paths.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=${1:?image}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

DOMAINS=(
  Settings SettingsDisclosures ProfilesFeatureSets SettingsFeatureSets
  ProfilesSubscriptions SettingsSubscriptions AppKit SiriUI InCallService
  Messages Kettle SystemStatus Accessibility UserManagement MemoryTelemetry
  launchd AirPlay AppInstallationMetrics CoreAudioServices SoundAnalysis
  AVConference TelephonyUtilities CameraCapture FileIndexer TextComposer
  IntelligenceFlow LaunchServices CoreMedia ModelManager NanoTimeKit
  Security SpotlightKnowledge usbaudiod AppleCVHWA
)
TOP=(
  Settings SettingsDisclosures ProfilesFeatureSets SettingsFeatureSets
  ProfilesSubscriptions SettingsSubscriptions
)

python3 - "$TMP" "${DOMAINS[@]}" -- "${TOP[@]}" <<'PY'
import plistlib, pathlib, sys
args = sys.argv[1:]
sep = args.index("--")
base = pathlib.Path(args[0])
domains, top = args[1:sep], args[sep + 1 :]
empty = {}
for n in domains:
    for sub in (
        f"System/Library/FeatureFlags/Domain/{n}.plist",
        f"System/Library/FeatureFlags/Unified/Domain/{n}.plist",
        f"Library/Preferences/FeatureFlags/Domain/{n}.plist",
    ):
        p = base / sub
        p.parent.mkdir(parents=True, exist_ok=True)
        with p.open("wb") as f:
            plistlib.dump(empty, f)
for n in top:
    for root in (
        "System/Library/FeatureFlags",
        "System/Library/FeatureFlags/Unified",
        "Library/Preferences/FeatureFlags",
    ):
        p = base / f"{root}/{n}.plist"
        p.parent.mkdir(parents=True, exist_ok=True)
        with p.open("wb") as f:
            plistlib.dump(empty, f)
PY

python3 "$ROOT/tools/lapisfs_add.py" "$IMG" \
  --dir System/Library/FeatureFlags \
  --dir System/Library/FeatureFlags/Domain \
  --dir System/Library/FeatureFlags/Unified \
  --dir System/Library/FeatureFlags/Unified/Domain \
  --dir Library/Preferences/FeatureFlags \
  --dir Library/Preferences/FeatureFlags/Domain

mapfile -t FILES < <(find "$TMP" -type f | sort)
ARGS=(python3 "$ROOT/tools/lapisfs_add.py" "$IMG")
for f in "${FILES[@]}"; do
  rel=${f#"$TMP"/}
  ARGS+=("$rel=$f")
done
"${ARGS[@]}"
echo "apply_featureflags_stubs: done (${#FILES[@]} files)"
