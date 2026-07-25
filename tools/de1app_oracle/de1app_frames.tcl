#!/usr/bin/env tclsh
#
# Print the extraction frames de1app ITSELF builds for a .tcl profile.
#
#   tclsh de1app_frames.tcl <de1app-checkout> <profile.tcl>
#
# de1app's profile.tcl is sourced verbatim from the checkout — nothing here
# reimplements its logic. That is the entire point: a reimplementation would be
# one more thing that can disagree with de1app, and disagreeing with de1app is
# the bug class this exists to find.
#
# Output is line-oriented and stable:
#   TYPE\t<settings_profile_type>
#   COUNTSTART\t<NumberOfPreinfuseFrames de1app derived, empty for advanced>
#   FRAME\tkey=value\tkey=value...      (one per extraction frame, in order)
#
# An empty value means de1app did not set that key. That is meaningful, not
# noise: for a pressure frame de1app writes no `flow` at all, and anything we
# put there instead is our invention.

if {[llength $argv] < 2} {
    puts stderr "usage: tclsh de1app_frames.tcl <de1app-checkout> <profile.tcl>"
    exit 2
}
set de1app [lindex $argv 0]
set profile [lindex $argv 1]

# de1app's profile.tcl expects the full app around it. Supply the three things
# it actually touches rather than loading the GUI.
rename package __real_package
proc package {args} {
    if {[lindex $args 0] in {require provide}} { return }
    return [__real_package {*}$args]
}
proc msg {args} {}
# Verbatim from de1plus/gui.tcl.
proc ifexists {fieldname2 {defvalue {}}} {
    upvar $fieldname2 fieldname
    if {[info exists fieldname]} { return $fieldname }
    if {$defvalue ne ""} { set fieldname $defvalue; return $defvalue }
    return ""
}

# profile_vars lives in vars.tcl, which drags in the whole app if sourced. Pull
# just that proc out of the checkout so it stays current with de1app rather than
# being copied here and going stale.
set vf [open [file join $de1app de1plus vars.tcl] r]
fconfigure $vf -encoding utf-8
set varsrc [read $vf]
close $vf
set profile_vars_src {}
set capturing 0
foreach line [split $varsrc \n] {
    if {!$capturing && [string match "proc profile_vars *" $line]} { set capturing 1 }
    if {$capturing} {
        append profile_vars_src $line \n
        # The proc body is a single `return { ... }`, so the first line that is
        # just a closing brace ends it.
        if {[string trim $line] eq "\}" && [string length $profile_vars_src] > 25} break
    }
}
if {$profile_vars_src eq ""} {
    puts stderr "de1app_frames: could not find proc profile_vars in vars.tcl"
    exit 3
}
eval $profile_vars_src

source [file join $de1app de1plus profile.tcl]

# convert_all_legacy_to_v2 (profile.tcl, "Disable limits by default") presets
# these BEFORE overlaying the profile file, so a profile that omits them
# converts with its limits OFF. Without this de1app's own builder aborts on its
# own shipped profiles, which is how we learned the presets are load-bearing.
set ::settings(preinfusion_flow_rate) 4
set ::settings(maximum_flow) 0
set ::settings(maximum_pressure) 0
set ::settings(maximum_flow_range_advanced) 0.6
set ::settings(maximum_pressure_range_advanced) 0.6
set ::settings(maximum_flow_range_default) 1.0
set ::settings(maximum_pressure_range_default) 0.9
set ::settings(profile_hide) 0

set fh [open $profile r]
fconfigure $fh -encoding utf-8
array set ::settings [read $fh]
close $fh

# de1app's own dispatch, sync_from_legacy(). Note the DEFAULT arm: anything that
# is not 2b/2c/2c2 goes through the pressure builder, including a missing type.
switch -- [ifexists ::settings(settings_profile_type)] {
    settings_2b  { array set out [::profile::flow_to_advanced_list] }
    settings_2c  -
    settings_2c2 { array set out [::profile::settings_to_advanced_list] }
    default      { array set out [::profile::pressure_to_advanced_list] }
}

puts "TYPE\t[ifexists ::settings(settings_profile_type)]"
puts "COUNTSTART\t[ifexists out(final_desired_shot_volume_advanced_count_start)]"
foreach step $out(advanced_shot) {
    array unset p
    array set p $step
    set vals {}
    foreach k {name pump sensor transition temperature pressure flow seconds volume \
               exit_if exit_type exit_pressure_over exit_pressure_under \
               exit_flow_over exit_flow_under max_flow_or_pressure \
               max_flow_or_pressure_range weight} {
        lappend vals "$k=[ifexists p($k)]"
    }
    puts "FRAME\t[join $vals \t]"
}
