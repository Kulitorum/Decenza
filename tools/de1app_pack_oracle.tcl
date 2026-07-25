# Pack a de1app .tcl profile into DE1 BLE frames using de1app's OWN packer.
#
# This is the differential oracle for tools/profile_pack_diff: it runs
# de1app's `de1_packed_shot` (binary.tcl) directly, so the bytes it emits are
# the bytes de1app would put on the wire — not a re-implementation of them.
#
# Usage:  tclsh de1app_pack_oracle.tcl <de1plus-dir> <profile.tcl>
# Output: one line per frame, "<index> <hex>", then "header <hex>".
#
# The shims below stand in for de1app globals that binary.tcl references but
# that do not participate in packing. Each is deliberately inert; if one ever
# supplies a value that reaches a packed byte, the comparison against Decenza
# would be measuring this file rather than de1app, so keep them trivial.

proc ifexists {varname {default {}}} {
    upvar 1 $varname v
    if {[info exists v]} { return $v }
    return $default
}
proc translate {s args} { return $s }
proc msg {args} {}
proc round_to_integer {n} { return [expr {round($n)}] }
proc round_to_one_digits {n} { return [expr {round($n * 10) / 10.0}] }

# binary.tcl's package requires, satisfied without loading the app.
package provide lambda 1.0
package provide de1_event 1.0
package provide de1_logging 1.0
package provide de1_profile 2.0

set srcdir  [lindex $argv 0]
set profile [lindex $argv 1]

source $srcdir/binary.tcl
source $srcdir/profile.tcl

# profile.tcl's conversion procs call profile_vars, which lives in vars.tcl —
# 3000 lines of GUI state that cannot be sourced here. It is a pure data proc, so
# pull that one definition out verbatim rather than transcribing the field list:
# a transcription would silently go stale on a de1app bump and quietly change
# which fields survive the conversion.
proc extract_proc {src name} {
    set idx [string first "proc ${name} " $src]
    if {$idx < 0} { error "proc $name not found" }
    set depth 0
    set groups 0
    for {set j [string first "\{" $src $idx]} {$j < [string length $src]} {incr j} {
        set ch [string index $src $j]
        if {$ch eq "\{"} { incr depth } elseif {$ch eq "\}"} {
            incr depth -1
            if {$depth == 0} {
                incr groups
                if {$groups == 2} { return [string range $src $idx $j] }
            }
        }
    }
    error "unbalanced proc $name"
}
set vfh [open $srcdir/vars.tcl r]; fconfigure $vfh -encoding utf-8
eval [extract_proc [read $vfh] profile_vars]
close $vfh

# --- read the profile's advanced_shot and preinfuse count ---------------------
set fh [open $profile r]
fconfigure $fh -encoding utf-8
set content [read $fh]
close $fh

# A SIMPLE profile's stored advanced_shot is not what de1app brews. de1app
# rebuilds the frames from the scalar fields at load — pressure_to_advanced_list
# for settings_2a, flow_to_advanced_list for settings_2b — and packs THOSE; the
# advanced_shot sitting in the file is a stale by-product. Packing it instead
# compares Decenza's regenerated frames against de1app's leftovers and reports
# every simple profile as a divergence. Decenza does the same regeneration
# (Profile::regenerateSimpleFrames), so run de1app's real conversion here and
# the two are comparable.
#
# Advanced profiles (settings_2c) keep their stored frames verbatim, which is
# what settings_to_advanced_list does.
set ptype ""
regexp -line {^settings_profile_type\s+(\S+)} $content -> ptype
if {$ptype eq "settings_2a" || $ptype eq "settings_2b"} {
    # Seed de1app's DEFAULT settings before overlaying the profile. In the app,
    # ::settings already holds them when select_profile loads a file, so a profile
    # that omits maximum_flow / maximum_pressure_range_default (several do)
    # inherits the app default rather than being missing the key. Without this the
    # conversion aborts on four profiles, and "the oracle could not run" would
    # quietly become "these four are not covered".
    #
    # Extracted verbatim from machine.tcl's `array set ::settings {...}` for the
    # same reason as profile_vars: a copied default drifts on a de1app bump and
    # the drift is invisible.
    set mfh [open $srcdir/machine.tcl r]; fconfigure $mfh -encoding utf-8
    set msrc [read $mfh]; close $mfh
    set ds [string first "array set ::settings \{" $msrc]
    if {$ds < 0} { error "machine.tcl: default ::settings block not found" }
    set dstart [string first "\{" $msrc $ds]
    set depth 0
    for {set j $dstart} {$j < [string length $msrc]} {incr j} {
        set ch [string index $msrc $j]
        if {$ch eq "\{"} { incr depth } elseif {$ch eq "\}"} {
            incr depth -1
            if {$depth == 0} { break }
        }
    }
    array set ::settings [string range $msrc [expr {$dstart + 1}] [expr {$j - 1}]]

    array set ::settings $content
    if {$ptype eq "settings_2a"} {
        array set converted [::profile::pressure_to_advanced_list]
    } else {
        array set converted [::profile::flow_to_advanced_list]
    }
    set count_start 0
    if {[info exists converted(final_desired_shot_volume_advanced_count_start)]} {
        set count_start $converted(final_desired_shot_volume_advanced_count_start)
    }
    set packed [de1_packed_shot [list advanced_shot $converted(advanced_shot) \
        final_desired_shot_volume_advanced_count_start $count_start]]
    set hdr    [lindex $packed 0]
    set frames [lindex $packed 1]
    binary scan $hdr H* hdrhex
    puts "header $hdrhex"
    set i 0
    foreach f $frames {
        binary scan $f H* fhex
        puts "$i $fhex"
        incr i
    }
    exit 0
}

# advanced_shot {{...} {...}} — take the outermost balanced brace group.
if {![regexp -indices {advanced_shot\s+\{} $content m]} {
    puts stderr "no advanced_shot in $profile"
    exit 1
}
set start [lindex $m 1]
set depth 0
set end -1
for {set i $start} {$i < [string length $content]} {incr i} {
    set ch [string index $content $i]
    if {$ch eq "\{"} { incr depth } elseif {$ch eq "\}"} {
        incr depth -1
        if {$depth == 0} { set end $i ; break }
    }
}
# Strip the outer braces: de1_packed_shot wants a Tcl LIST of frames, and
# leaving them on makes the whole thing one element.
set shot_list [string range $content [expr {$start + 1}] [expr {$end - 1}]]

set count_start 0
if {[regexp {final_desired_shot_volume_advanced_count_start\s+(\d+)} $content -> cs]} {
    set count_start $cs
}

# de1_packed_shot does `array set profile $shot_list` in its own scope and reads
# the count from there, so the count has to travel INSIDE the shot list.
set packed [de1_packed_shot [list advanced_shot $shot_list \
    final_desired_shot_volume_advanced_count_start $count_start]]

# make_chunked_packed_shot_sample returns {header {frame frame ...}}
set hdr    [lindex $packed 0]
set frames [lindex $packed 1]

binary scan $hdr H* hdrhex
puts "header $hdrhex"
set i 0
foreach f $frames {
    binary scan $f H* fhex
    puts "$i $fhex"
    incr i
}
