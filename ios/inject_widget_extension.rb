#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Shares the WidgetKit target between a CMake-generated Xcode project and
# a packaging-only project created for a prebuilt Ninja app. Run after app
# compilation and before `xcodebuild archive`; generated projects stay in build/.
#
# Requires the `xcodeproj` gem:  gem install xcodeproj
#
# Usage (optional third argument creates a packaging-only project):
#   ruby ios/inject_widget_extension.rb <path-to-Decenza.xcodeproj> <repo-root> [Ninja-built.app]
#
# Env (manual signing — matches release.yml):
#   TEAM_ID                            Apple Developer team id
#   WIDGET_PROVISIONING_PROFILE_NAME   distribution profile *name* for the
#                                      widget App ID (io.github.kulitorum.decenza.widget)
#
# Idempotent: re-running on an already-patched project is a no-op.

require "xcodeproj"
require "shellwords"

proj_path = ARGV[0] or abort "usage: inject_widget_extension.rb <xcodeproj> <repo-root>"
repo_root = ARGV[1] or abort "usage: inject_widget_extension.rb <xcodeproj> <repo-root>"
prebuilt_app = ARGV[2]

APP_TARGET   = "Decenza"
EXT_TARGET   = "DecenzaWidgetExtension"
EXT_BUNDLE   = "io.github.kulitorum.decenza.widget"
WIDGET_DIR   = File.join(repo_root, "ios", "widget")
SWIFT_FILES  = %w[MachineStatusSnapshot.swift DecenzaWidget.swift]
INFO_PLIST   = File.join(WIDGET_DIR, "Info.plist")
ENTITLEMENTS = File.join(WIDGET_DIR, "DecenzaWidgetExtension.entitlements")

if prebuilt_app
  # Ninja owns app compilation; this small project owns Apple's archive,
  # asset-catalog processing, signing and the same widget target below.
  prebuilt_app = File.expand_path(prebuilt_app)
  plist_path = File.join(prebuilt_app, "Info.plist")
  plist = Xcodeproj::Plist.read_from_path(plist_path)
  abort "Missing Ninja-built executable" unless File.file?(File.join(prebuilt_app, "Decenza"))
  project = Xcodeproj::Project.new(proj_path)
  app = project.new_target(:application, APP_TARGET, :ios, plist.fetch("MinimumOSVersion"))
  app_group = project.main_group.new_group("Archive")
  # Xcode links a tiny placeholder, then installs Ninja's real executable
  # before generating its dSYM and signing. CI checks the archived UUID.
  placeholder = File.join(File.dirname(proj_path), "archive_main.c")
  File.write(placeholder, "int main(void) { return 0; }\n")
  app.add_file_references([app_group.new_reference(File.expand_path(placeholder))])
  assets = app_group.new_reference(File.join(repo_root, "ios", "Assets.xcassets"))
  app.resources_build_phase.add_file_reference(assets)
  storyboard = File.join(prebuilt_app, "LaunchScreen.storyboard")
  app.resources_build_phase.add_file_reference(app_group.new_reference(storyboard)) if File.file?(storyboard)
  app.build_configurations.each do |cfg|
    s = cfg.build_settings
    s["PRODUCT_BUNDLE_IDENTIFIER"] = plist.fetch("CFBundleIdentifier")
    s["MARKETING_VERSION"] = plist.fetch("CFBundleShortVersionString")
    s["CURRENT_PROJECT_VERSION"] = plist.fetch("CFBundleVersion")
    s["INFOPLIST_FILE"] = plist_path
    s["GENERATE_INFOPLIST_FILE"] = "NO"
    s["CODE_SIGN_ENTITLEMENTS"] = File.join(repo_root, "ios", "Decenza.entitlements")
    s["ASSETCATALOG_COMPILER_APPICON_NAME"] = "AppIcon"
    s.delete("ASSETCATALOG_COMPILER_GLOBAL_ACCENT_COLOR_NAME")
    s["TARGETED_DEVICE_FAMILY"] = "1,2"
    s["DEBUG_INFORMATION_FORMAT"] = "dwarf-with-dsym"
    s["ENABLE_USER_SCRIPT_SANDBOXING"] = "NO"
    s["SKIP_INSTALL"] = "NO"
  end
  install = app.new_shell_script_build_phase("Install Ninja-built app")
  install.shell_script = <<~SH
    set -eu
    /usr/bin/rsync -a --exclude=Info.plist --exclude=Assets.xcassets \\
      --exclude=LaunchScreen.storyboard --exclude=PlugIns --exclude=_CodeSignature \\
      --exclude=embedded.mobileprovision #{Shellwords.escape(prebuilt_app + "/")} "$TARGET_BUILD_DIR/$WRAPPER_NAME/"
  SH
  install.always_out_of_date = "1"
else
  project = Xcodeproj::Project.open(proj_path)
end

app = project.targets.find { |t| t.name == APP_TARGET }
abort "App target '#{APP_TARGET}' not found in #{proj_path}" unless app

existing = project.targets.find { |t| t.name == EXT_TARGET }
if existing
  # Only a no-op if the injection is COMPLETE. A target present without the
  # app's embed phase referencing its .appex is a partial/corrupt injection
  # that would archive successfully but ship an IPA with no widget — fail
  # loudly instead of silently exiting 0.
  embedded = app.copy_files_build_phases.any? do |ph|
    ph.symbol_dst_subfolder_spec == :plug_ins &&
      ph.files_references.any? { |r| r == existing.product_reference }
  end
  if embedded
    puts "[inject] '#{EXT_TARGET}' already fully present — nothing to do."
    exit 0
  end
  abort "[inject] '#{EXT_TARGET}' exists but is NOT embedded in '#{APP_TARGET}' "\
        "(partial injection). Refusing to proceed — regenerate the project."
end

team    = ENV["TEAM_ID"].to_s
profile = ENV["WIDGET_PROVISIONING_PROFILE_NAME"].to_s

# --- Create the app-extension target -----------------------------------------
ext = project.new_target(
  :app_extension, EXT_TARGET, :ios, "18.0", project.products_group, :swift
)

# Source group + files
group = project.main_group.find_subpath("DecenzaWidgetExtension", true)
group.set_source_tree("<group>")

SWIFT_FILES.each do |name|
  ref = group.new_reference(File.join(WIDGET_DIR, name))
  ext.add_file_references([ref])
end
# Info.plist / entitlements are referenced via build settings, not compiled.
group.new_reference(INFO_PLIST)
group.new_reference(ENTITLEMENTS)

# System frameworks the SwiftUI widget links.
%w[WidgetKit.framework SwiftUI.framework].each do |fw|
  ext.add_system_framework(fw.sub(".framework", ""))
end

# --- Build settings (manual signing, matches the app's archive flags) --------
ext.build_configurations.each do |cfg|
  s = cfg.build_settings
  app_settings = app.build_configurations.find { |c| c.name == cfg.name }.build_settings
  %w[MARKETING_VERSION CURRENT_PROJECT_VERSION].each { |key| s[key] = app_settings[key] }
  s["PRODUCT_NAME"]                 = EXT_TARGET
  s["PRODUCT_BUNDLE_IDENTIFIER"]    = EXT_BUNDLE
  s["INFOPLIST_FILE"]               = INFO_PLIST
  s["CODE_SIGN_ENTITLEMENTS"]       = ENTITLEMENTS
  s["IPHONEOS_DEPLOYMENT_TARGET"]   = "18.0"  # keep in step with CMakeLists.txt
  s["TARGETED_DEVICE_FAMILY"]       = "1,2"
  s["SWIFT_VERSION"]                = "5.0"
  s["GENERATE_INFOPLIST_FILE"]      = "NO"
  s["SKIP_INSTALL"]                 = "NO"
  s["CODE_SIGN_STYLE"]              = "Manual"
  s["CODE_SIGN_IDENTITY"]           = "iPhone Distribution"
  s["DEVELOPMENT_TEAM"]             = team    unless team.empty?
  s["PROVISIONING_PROFILE_SPECIFIER"] = profile unless profile.empty?
  s["SWIFT_ACTIVE_COMPILATION_CONDITIONS"] ||= "$(inherited)"
end

# --- App target: pin its own profile so the global command-line specifier
#     can be dropped (a global PROVISIONING_PROFILE_SPECIFIER on the archive
#     command would otherwise clobber the extension's per-target profile). ----
app_profile = ENV["APP_PROVISIONING_PROFILE_NAME"].to_s
unless app_profile.empty?
  app.build_configurations.each do |cfg|
    s = cfg.build_settings
    s["CODE_SIGN_STYLE"]                = "Manual"
    s["CODE_SIGN_IDENTITY"]             = "iPhone Distribution"
    s["DEVELOPMENT_TEAM"]               = team unless team.empty?
    s["PROVISIONING_PROFILE_SPECIFIER"] = app_profile
  end
end

# --- Embed the .appex into the app and build it as a dependency --------------
app.add_dependency(ext)

embed = app.new_copy_files_build_phase("Embed App Extensions")
embed.symbol_dst_subfolder_spec = :plug_ins
appex_ref = ext.product_reference
build_file = embed.add_file_reference(appex_ref)
build_file.settings = { "ATTRIBUTES" => ["RemoveHeadersOnCopy"] }
# Ensure the extension is signed when copied into the app bundle.
build_file.settings["ATTRIBUTES"] << "CodeSignOnCopy"

project.save
if prebuilt_app
  scheme = Xcodeproj::XCScheme.new
  scheme.configure_with_targets(app, nil)
  scheme.save_as(proj_path, APP_TARGET)
end
puts "[inject] Added '#{EXT_TARGET}' (#{EXT_BUNDLE}) + embed phase to '#{APP_TARGET}'."
