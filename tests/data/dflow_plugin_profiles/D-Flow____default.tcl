advanced_shot {{exit_if 1 flow 8 volume 100 max_flow_or_pressure_range 0.2 transition fast exit_flow_under 0 temperature 88 weight 5.00 name Filling pressure 3.0 sensor coffee pump pressure exit_type pressure_over exit_flow_over 6 exit_pressure_over 1.5 max_flow_or_pressure 0 exit_pressure_under 0 seconds 25.00} {exit_if 0 flow 8 volume 100.00 max_flow_or_pressure_range 0.2 transition fast exit_flow_under 0 temperature 88 weight 4.00 name Infusing pressure 3.0 sensor coffee pump pressure exit_type pressure_over exit_flow_over 6 exit_pressure_over 3.0 max_flow_or_pressure 0 exit_pressure_under 0 seconds 60.0} {exit_if 0 flow 1.7 volume 0 max_flow_or_pressure_range 0.2 transition fast exit_flow_under 0 temperature 88 weight 0.00 name Pouring pressure 4.8 sensor coffee pump flow exit_type flow_over exit_flow_over 2.80 exit_pressure_over 11 max_flow_or_pressure 8.5 exit_pressure_under 0 seconds 127}}
espresso_temperature_steps_enabled 0
author Damian
espresso_hold_time 16
preinfusion_time 20
espresso_pressure 6.0
espresso_decline_time 30
pressure_end 4.0
espresso_temperature 86.0
espresso_temperature_0 86.0
espresso_temperature_1 86.0
espresso_temperature_2 86.0
espresso_temperature_3 86.0
settings_profile_type settings_2c
flow_profile_preinfusion 4
flow_profile_preinfusion_time 5
flow_profile_hold 2
flow_profile_hold_time 8
flow_profile_decline 1.2
flow_profile_decline_time 17
flow_profile_minimum_pressure 4
preinfusion_flow_rate 4
profile_notes {A simple to use profiling system By Damian Brakel
        D-Flow's Default profile is provided as an example profile for you to get started in creating your own}
final_desired_shot_volume 54
final_desired_shot_weight 50
final_desired_shot_weight_advanced 50
tank_desired_water_temperature 0
final_desired_shot_volume_advanced 0
profile_language en
preinfusion_stop_pressure 4.0
profile_hide 0
final_desired_shot_volume_advanced_count_start 2
beverage_type espresso
maximum_pressure 0
maximum_pressure_range_advanced 0.2
maximum_flow_range_advanced 0.2
maximum_flow 0
maximum_pressure_range_default 0.9
maximum_flow_range_default 1.0
active_settings_tab "settings_1"
profile_title {D-Flow / default}
