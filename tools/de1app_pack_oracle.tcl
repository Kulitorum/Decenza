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

# --- read the profile's advanced_shot and preinfuse count ---------------------
set fh [open $profile r]
fconfigure $fh -encoding utf-8
set content [read $fh]
close $fh

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
