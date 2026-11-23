#!/usr/bin/env bash
# Rebuilds, validates and installs Heft.vst3 on macOS with a single command.
# The macOS counterpart of build.ps1, with the same steps and options.
#
#   ./build.sh                    # build + pluginval (level 10) + install
#   ./build.sh --strictness 5     # faster validation
#   ./build.sh --measure          # also measure the sound (HeftMeasure) and run the unit tests (HeftTests)
#   ./build.sh --skip-validation  # build and install only
#   ./build.sh --clean            # delete build/ and build from scratch
#   ./build.sh --host             # only build JUCE's AudioPluginHost, a second host for testing
#   ./build.sh --install-dir DIR  # install somewhere else (default: ~/Library/Audio/Plug-Ins/VST3)

set -euo pipefail

strictness=10
skipValidation=0
measure=0
clean=0
buildHost=0
installDir="$HOME/Library/Audio/Plug-Ins/VST3"

step() { printf '\n\033[36m==> %s\033[0m\n' "$1"; }
fail() { printf '\n\033[31mERROR: %s\033[0m\n' "$1"; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --strictness)      strictness="${2:-}"; shift ;;
        --skip-validation) skipValidation=1 ;;
        --measure)         measure=1 ;;
        --clean)           clean=1 ;;
        --host)            buildHost=1 ;;
        --install-dir)     installDir="${2:-}"; shift ;;
        -h|--help)         sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)                 fail "Unknown option: $1 (see ./build.sh --help)" ;;
    esac
    shift
done

case "$strictness" in
    [1-9]|10) ;;
    *) fail "--strictness must be a number from 1 to 10." ;;
esac
[ -n "$installDir" ] || fail "--install-dir needs a folder."

root="$(cd "$(dirname "$0")" && pwd)"
buildDir="$root/build"
pluginBuilt="$buildDir/Heft_artefacts/Release/VST3/Heft.vst3"
measureExe="$buildDir/HeftMeasure_artefacts/Release/HeftMeasure"
testsExe="$buildDir/HeftTests_artefacts/Release/HeftTests"
pluginvalDir="$root/tools/pluginval"
pluginvalExe="$pluginvalDir/pluginval.app/Contents/MacOS/pluginval"
pluginvalUrl="https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_macOS.zip"

command -v cmake > /dev/null || fail 'CMake not found. Install it with "brew install cmake".'
xcode-select -p > /dev/null 2>&1 || fail 'Xcode not found. Install Xcode from the App Store and open it once.'

getPluginval() {
    [ -x "$pluginvalExe" ] && return
    step 'Downloading pluginval v1.0.4'
    mkdir -p "$pluginvalDir"
    curl -fsSL -o "$pluginvalDir/pluginval_macOS.zip" "$pluginvalUrl" || fail "Couldn't download pluginval from $pluginvalUrl"
    unzip -oq "$pluginvalDir/pluginval_macOS.zip" -d "$pluginvalDir"
    [ -x "$pluginvalExe" ] || fail "pluginval not found at $pluginvalExe after unzipping."
}

# Xcode prints every command it runs; -quiet keeps only warnings and errors.
build() { cmake --build "$1" --config Release --target "$2" -- -quiet; }

# --- Optional: JUCE's AudioPluginHost, to test Heft in a second host -------------
# Uses the JUCE source the normal build already downloaded (no second download) and
# its own folder, build/juce-host. Later runs only rebuild what changed.
if [ "$buildHost" = 1 ]; then
    juceSource="$buildDir/_deps/juce-src"
    hostDir="$buildDir/juce-host"
    hostApp="$hostDir/extras/AudioPluginHost/AudioPluginHost_artefacts/Release/AudioPluginHost.app"

    if [ ! -f "$juceSource/CMakeLists.txt" ]; then
        step 'Configuring the project first (downloads JUCE, takes a few minutes)'
        cmake -S "$root" -B "$buildDir" -G Xcode || fail 'CMake configuration failed.'
    fi

    if [ ! -f "$hostDir/CMakeCache.txt" ]; then
        step 'Configuring the JUCE extras'
        cmake -S "$juceSource" -B "$hostDir" -G Xcode -DJUCE_BUILD_EXTRAS=ON -DJUCE_BUILD_EXAMPLES=OFF \
            || fail 'Configuring the AudioPluginHost failed.'
    fi

    step 'Building the AudioPluginHost (the first time takes a few minutes)'
    build "$hostDir" AudioPluginHost || fail 'Building the AudioPluginHost failed.'
    [ -d "$hostApp" ] || fail "AudioPluginHost.app not found at $hostApp"

    printf '\n\033[32mAudioPluginHost ready in %s s:\033[0m\n  %s\n' "$SECONDS" "$hostApp"
    echo 'To load Heft in it, see "Testing in a second host" in the README.'
    exit 0
fi

# --- 1. Configure --------------------------------------------------------------
if [ "$clean" = 1 ] && [ -d "$buildDir" ]; then
    step 'Deleting build/'
    rm -rf "$buildDir"
fi

if [ ! -f "$buildDir/CMakeCache.txt" ]; then
    step 'Configuring the project (the first time it downloads JUCE, takes a few minutes)'
    cmake -S "$root" -B "$buildDir" -G Xcode || fail 'CMake configuration failed.'
fi

# --- 2. Build ------------------------------------------------------------------
step 'Building in Release'
build "$buildDir" Heft_VST3 || fail 'Build failed. Check the errors above.'
[ -d "$pluginBuilt" ] || fail "Built plugin not found at $pluginBuilt"

# --- 3. Validate ---------------------------------------------------------------
if [ "$skipValidation" = 0 ]; then
    getPluginval
    step "Validating with pluginval (strictness $strictness)"
    logDir="$buildDir/pluginval"
    "$pluginvalExe" --strictness-level "$strictness" --output-dir "$logDir" --validate "$pluginBuilt" \
        || fail "pluginval found errors (log in $logDir). The plugin was NOT installed."
    printf '\033[32mpluginval: all tests passed.\033[0m\n'
fi

if [ "$measure" = 1 ]; then
    step 'Measuring the sound (HeftMeasure)'
    build "$buildDir" HeftMeasure || fail 'Building HeftMeasure failed.'
    "$measureExe" "$pluginBuilt" || fail 'Some measurements failed. The plugin was NOT installed.'

    step 'Running the unit tests (HeftTests)'
    build "$buildDir" HeftTests || fail 'Building HeftTests failed.'
    "$testsExe" || fail 'Some unit tests failed. The plugin was NOT installed.'
fi

# --- 4. Install ----------------------------------------------------------------
step "Installing to $installDir"
mkdir -p "$installDir"
dest="$installDir/Heft.vst3"
# Delete and copy rather than overwrite: a host that has the old one loaded keeps
# running it safely until it restarts.
rm -rf "$dest"
cp -R "$pluginBuilt" "$dest"

printf '\n\033[32mDone in %s s. Plugin installed at:\033[0m\n  %s\n' "$SECONDS" "$dest"
echo 'If REAPER is open, quit and reopen it to load this build. If Heft is new to it:'
echo 'Settings (Cmd+,) > Plug-ins > VST > Re-scan.'
