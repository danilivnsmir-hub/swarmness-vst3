Optional artwork. Any PNG here is built into the plug-in (re-run cmake after adding files) and replaces
the drawn version of that element; everything without a PNG keeps its drawn look, so any part of the
set can be missing. Names, sizes (drawn at 2x) and 9-slice corners: ASSET_SPEC.md. Loader: Source/GUI/Skin.h.

Picked up by the code:

- bg_main, bg_emblem, header_bar, logo                     - window background, bee, header strip, logo
- panel, panel_footboard, panel_inset                      - section panels, footswitch board, graph windows (9-slice)
- knob_base, knob_cap, knob_cap_small, knob_big_cap         - knobs: fixed base with its ticks at -135..+135 deg,
                                                             cap turning with the value (pointer straight up)
- power_off, power_on, led_off, led_amber, led_green, led_red
- button_normal, button_hover, button_down                 - header / IR / capture buttons (9-slice)
- arrow_left, arrow_right                                   - the < > buttons (arrow drawn in)
- pill_off, pill_on, segment_bg, segment_on                 - toggles and segmented selectors
- chain_hex_off, chain_hex_on, chain_hex_selected, chain_connector, chain_in, chain_out
- footswitch_up, footswitch_down, footswitch_bypass_up, footswitch_bypass_down, toggle_up, toggle_down
- scene_off, scene_on, meter_bg
- amp_plate_<chrome|brit|steel|nam>                         - AMP panel behind the knobs, 220 px high (9-slice)
- amp_head_<chrome|brit|steel|nam>                          - the head on the nameplate, 300 x 200 px
- cab_grille_0 .. cab_grille_4                              - CAB grille cloth, 210 px high (9-slice)
- tuner_panel, tuner_cell_off, tuner_cell_on, tuner_cell_center

Not used yet: knob_cap_hover (hover is drawn), icon_<block> (for a future mini view).
Keep everything behind text dark enough for the light captions on top of it.
