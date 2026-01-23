#!/bin/bash

# Decenza DE1 Build Script
# usage: ./build.sh --target <OSX|ANDROID> [--debug]

# Default values
TARGET="OSX"
BUILD_TYPE="Release"
CLEAN=false
OS=$(uname)

# Usage information
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  --target <OSX|ANDROID>     Target platform (default: OSX)"
    echo "  --debug                    Build in Debug mode (default: Release)"
    echo "  --clean                    Remove build directory before building"
    echo "  --help                     Show this help message"
    exit 1
}

# Parse arguments
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --target) 
            TARGET=$(echo "$2" | tr '[:lower:]' '[:upper:]')
            shift 
            ;;
        --debug)
            BUILD_TYPE="Debug"
            ;;
        --clean)
            CLEAN=true
            ;;
        --help) 
            usage 
            ;;
        *) 
            echo "Unknown parameter passed: $1"
            usage 
            ;;
    esac
    shift
done

echo "Building for target: $TARGET ($BUILD_TYPE)"

# Find tools based on platform
if [[ "$OS" == "Darwin" ]]; then
    # macOS host
    QT_PATH=~/Qt
    NINJA=$(find "$QT_PATH/Tools" -name "ninja" | head -1)

    if [ -z "$NINJA" ]; then
        echo "Error: Ninja not found in $QT_PATH/Tools"
        exit 1
    fi

    case "$TARGET" in
        "OSX")
            QT_CMAKE=$(find "$QT_PATH" -name "qt-cmake" -path "*/macos/*" | head -1)
            BUILD_DIR="build/Qt_6_10_1_for_macOS_$BUILD_TYPE"
            ;;
        "ANDROID")
            QT_CMAKE=$(find "$QT_PATH" -name "qt-cmake" -path "*/android_arm64_v8a/*" | head -1)
            BUILD_DIR="build/Qt_6_10_1_for_Android_arm64_v8a_$BUILD_TYPE"
            
            # Android SDK/NDK detection
            if [ -z "$ANDROID_SDK_ROOT" ]; then
                export ANDROID_SDK_ROOT="$HOME/Library/Android/sdk"
                echo "Notice: ANDROID_SDK_ROOT not set, using default: $ANDROID_SDK_ROOT"
            fi
            if [ -z "$ANDROID_NDK_ROOT" ]; then
                # Find the latest NDK version
                NDK_VERSION=$(ls -1 "$ANDROID_SDK_ROOT/ndk" | sort -V | tail -1)
                export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/$NDK_VERSION"
                echo "Notice: ANDROID_NDK_ROOT not set, using detected: $ANDROID_NDK_ROOT"
            fi
            
            # Detect QT_HOST_PATH (required for cross-compilation)
            QT_VERSION_DIR=$(echo "$QT_CMAKE" | grep -oE "[0-9]+\.[0-9]+\.[0-9]+")
            QT_HOST_PATH=$(find "$QT_PATH" -maxdepth 4 -name "macos" -path "*/$QT_VERSION_DIR/*" | head -1)
            if [ -z "$QT_HOST_PATH" ]; then
                echo "Error: Could not detect QT_HOST_PATH for version $QT_VERSION_DIR"
                exit 1
            fi
            echo "Notice: Using QT_HOST_PATH: $QT_HOST_PATH"
            
            # Detect latest build-tools and add to PATH
            BUILD_TOOLS_VERSION=$(ls -1 "$ANDROID_SDK_ROOT/build-tools" | sort -V | tail -1)
            export PATH="$ANDROID_SDK_ROOT/build-tools/$BUILD_TOOLS_VERSION:$PATH"
            echo "Notice: Adding build-tools $BUILD_TOOLS_VERSION to PATH"
            
            # Detect JAVA_HOME
            if [ -z "$JAVA_HOME" ]; then
                export JAVA_HOME=$(/usr/libexec/java_home -v 17 2>/dev/null || /usr/libexec/java_home 2>/dev/null)
                echo "Notice: JAVA_HOME not set, using detected: $JAVA_HOME"
            fi
            
            EXTRA_CMAKE_ARGS="-DQT_HOST_PATH=$QT_HOST_PATH -DANDROID_SDK_ROOT=$ANDROID_SDK_ROOT -DANDROID_NDK_ROOT=$ANDROID_NDK_ROOT -DJAVA_HOME=$JAVA_HOME"
            ;;
        *)
            echo "Error: Unsupported target $TARGET on macOS host."
            exit 1
            ;;
    esac

    # Initialize EXTRA_CMAKE_ARGS if not set
    if [ -z "$EXTRA_CMAKE_ARGS" ]; then
        EXTRA_CMAKE_ARGS=""
    fi

    if [ -z "$QT_CMAKE" ]; then
        echo "Error: qt-cmake not found for target $TARGET"
        exit 1
    fi

    # Configure and Build (incremental - each target has its own subdir)
    if [ "$CLEAN" = true ]; then
        echo "Clean build requested, removing $BUILD_DIR"
        rm -rf "$BUILD_DIR"
    fi
    mkdir -p "$BUILD_DIR"
    abs_build_dir=$(pwd)/"$BUILD_DIR"
    cd "$BUILD_DIR" || exit

    # Note: We don't quote EXTRA_CMAKE_ARGS so that they're split into multiple arguments
    "$QT_CMAKE" ../.. -G Ninja \
        -DCMAKE_MAKE_PROGRAM="$NINJA" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        $EXTRA_CMAKE_ARGS

    "$NINJA"

    if [ $? -eq 0 ]; then
        echo "Build successful!"
        if [[ "$TARGET" == "OSX" ]]; then
            echo "Artifact location: $abs_build_dir/Decenza_DE1.app"
        elif [[ "$TARGET" == "ANDROID" ]]; then
            # APK location is deep in android-build folder
            artifact=$(find . -name "*.apk" | head -1)
            if [ -n "$artifact" ]; then
                echo "Artifact location: $abs_build_dir/${artifact#./}"
            else
                echo "Android build completed, but APK not found. Check android-build/build/outputs/apk/"
            fi
        fi
    else
        echo "Build failed!"
        exit 1
    fi
else
    echo "Unsupported host OS: $OS. This script is intended for macOS hosts."
    exit 1
fi
