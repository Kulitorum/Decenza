# Apply ONE parameter edit to a profile using the plugin's OWN prep/update procs.
#
# This is the oracle for the edit matrix. The wire test proved that identical
# frames pack to identical bytes; this answers the question upstream of it —
# given the same profile and the same edit, does Decenza produce the frames the
# plugin produces?
#
# Usage:
#   tclsh de1app_edit_oracle.tcl <plugin-src> <profile.tcl> <proc-suffix> <global> <value>
#
#   <plugin-src>   A_Flow/code.tcl or D_Flow_Espresso_Profile/plugin.tcl
#   <proc-suffix>  A-Flow | D-Flow   (selects update_A-Flow / update_D-Flow)
#   <global>       e.g. Aflow_pouring_temperature, ramp_down_enabled
#   <value>        the new value
#
# Output: one line per resulting frame, "<index> <key> <value> <key> <value> ...".
#
# WHY EXTRACT RATHER THAN SOURCE. Both plugin files are GUI code — `dui add ...`
# at top level, thousands of lines of layout — and sourcing either needs the
# whole de1app UI stack. So this pulls out just `prep` and `update_*` by
# brace-matched text extraction and evals them verbatim. The procs that run are
# byte-for-byte the plugin's; only their surroundings are synthesised. Anything
# they call that is UI-only is stubbed inert below, and those stubs are listed
# explicitly so it is obvious if one ever grows teeth.

proc extract_proc {src name} {
    # Locate the proc header, then return the whole definition brace-matched.
    # NB: no unbalanced brace may appear in a comment inside a braced body —
    # Tcl counts braces in comments too, and one stray opener swallows the
    # rest of the file into this proc.
    set pat "proc ${name} "
    set idx [string first $pat $src]
    if {$idx < 0} { error "proc $name not found" }
    # walk to the opening brace of the BODY (second brace group)
    set i [string first "\{" $src $idx]
    set depth 0
    set groups 0
    for {set j $i} {$j < [string length $src]} {incr j} {
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

# --- inert stubs -------------------------------------------------------------
# Every one of these is UI-only in the plugin. If any ever affects a frame
# value, the extracted procs would be reading it and this oracle would be
# reporting its own behaviour instead of de1app's.
proc ifexists {varname {default {}}} {
    upvar 1 $varname v
    if {[info exists v]} { return $v }
    return $default
}
proc msg {args} {}
proc translate {s args} { return $s }
proc round_to_integer {n} { return [expr {round($n)}] }
proc round_to_one_digits {n} { return [expr {round($n * 10) / 10.0}] }
proc round_to_two_digits {n} { return [expr {round($n * 100) / 100.0}] }
proc range_check_shot_variables {args} {}
proc profile_has_changed_set {args} {}
proc save_profile {args} {}
namespace eval ::A_Flow  { proc demo_graph {args} {} ; variable page_set "" }
namespace eval ::plugins::D_Flow_Espresso_Profile { proc demo_graph {args} {} }
namespace eval ::dui {}
proc dui {args} {}
# A-Flow's prep calls these three purely to restyle toggle buttons.
proc update_2nd_fill {args} {}
proc update_flow_up {args} {}
proc update_ramp_down {args} {}

set plugin_src [lindex $argv 0]
set profile    [lindex $argv 1]
set suffix     [lindex $argv 2]
set globalname [lindex $argv 3]
set newvalue   [lindex $argv 4]

set fh [open $plugin_src r]; fconfigure $fh -encoding utf-8
set src [read $fh]; close $fh

eval [extract_proc $src prep]
eval [extract_proc $src "update_${suffix}"]
if {$suffix eq "A-Flow"} { eval [extract_proc $src set_profile_index] }

# --- load the profile into ::settings ----------------------------------------
set fh [open $profile r]; fconfigure $fh -encoding utf-8
set content [read $fh]; close $fh

if {![regexp -indices {advanced_shot\s+\{} $content m]} { error "no advanced_shot" }
set start [lindex $m 1]
set depth 0; set end -1
for {set i $start} {$i < [string length $content]} {incr i} {
    set ch [string index $content $i]
    if {$ch eq "\{"} { incr depth } elseif {$ch eq "\}"} {
        incr depth -1
        if {$depth == 0} { set end $i; break }
    }
}
set ::settings(advanced_shot) [string range $content [expr {$start + 1}] [expr {$end - 1}]]

# Escaped braces only — a bare `}` in a character class would close this
# braced word early, the same trap as the comment above.
set title "Edited"
regexp -line {^profile_title\s+\{(.*)\}$} $content -> title
set ::settings(profile_title) $title

# prep populates the plugin's globals from the frames — exactly as a profile
# load does in the app.
prep

# the edit
set ::$globalname $newvalue

# regenerate
update_${suffix}

set i 0
foreach f $::settings(advanced_shot) {
    puts "$i $f"
    incr i
}
