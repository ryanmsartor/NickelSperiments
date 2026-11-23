# Ko Boy core-option policy matrix

This document audits the options advertised by Ko Boy's pinned libretro cores. Ko Boy registers every option so a core can read its default, but only options listed as user-facing or developer-only appear in Ko Boy's menus. A hidden option uses the core default unless Ko Boy fixes it to a frontend policy value.

The discussion column distinguishes an explicit decision from a blanket decision to retain the existing hidden/default behavior. **Not discussed** means the option was added upstream after the last policy audit and has not yet received a product decision; Ko Boy currently keeps it hidden at its core default.

## armGBA

Pinned commit: `d06b9989a0245f53f32e18e95a4ba59f07a38e9c`.

| Core option(s) | Available values; core default | Ko Boy policy | Discussion status |
| --- | --- | --- | --- |
| Skip BIOS Intro (`armgba_skip_bios`) | Off, On; **Off** | Fixed to Off; Ko Boy owns deterministic BIOS selection. | Explicit |
| BIOS (`armgba_bios`) | Nintendo/system, built-in; **Nintendo/system** | Fixed to Nintendo/system, which falls back to armGBA's built-in BIOS when the file is absent. | Explicit |
| CPU Clock; CPU Clock During Boot (`armgba_cpu_clock`, `armgba_cpu_clock_boot`) | 25%–400%; **100%**. Boot: 100% or CPU Clock; **100%** | Hidden; core defaults. | Blanket decision |
| Low Memory (`armgba_low_memory`) | Off, On; **Off** | Hidden; core default. | Blanket decision |
| Clock Source, Speed, day/hour offsets, reset permission, Reset Clock (`armgba_clock_source`, `armgba_clock_speed`, `armgba_clock_days`, `armgba_clock_hours`, `armgba_clock_reset_allowed`, `armgba_clock_reset`) | Real/Game Time; **Real**. Stopped–60×; **1×**. Offsets; **0**. Reset permission; **Off**. Reset action; **Off** | Hidden at core defaults; cartridge-clock controls are explicitly deferred for later design. | Explicit deferment |
| Game Boy Model (`armgba_gb_model`) | Auto, GB, Pocket/Light, GBC, GBA; **Auto** | Hidden; core default. | Blanket decision |
| Game Boy Boot ROM (`armgba_gb_boot_rom`) | Nintendo/system, built-in, Off; **Nintendo/system** | Fixed to Nintendo/system, with built-in fallback. | Explicit |
| Mega Duck Laptop, printer, speech, and cartridge slot (`armgba_megaduck_laptop`, `armgba_megaduck_printer`, `armgba_megaduck_speech`, `armgba_megaduck_slot`) | Auto/On/Off; **Auto**. Two Pass/One Pass/None; **Two Pass**. Spanish/German; **Spanish**. Dynamic cartridge list; **None** | Hidden; core defaults and automatic detection. | Blanket decision |
| Cheat-cartridge game and Game Genie codes 1–3 (`armgba_action_replay_game`, `armgba_game_genie_code_1`, `armgba_game_genie_code_2`, `armgba_game_genie_code_3`) | Dynamic game/code lists; **None** | Hidden; core defaults. | Blanket decision |
| Import Cheats, Cheat Cartridge Reset, Open Menu, Hold Button (`armgba_cheat_import`, `armgba_cheat_restore`, `armgba_action_replay_press`, `armgba_action_replay_hold`) | Action choices returning to Off; **Off** | Hidden; core defaults. | Blanket decision |
| Cheat button mode, switch, and mapping (`armgba_action_replay_press_mode`, `armgba_action_replay_switch`, `armgba_action_replay_button`) | Toggle/Hold; **Toggle**. On/Off; **On**. R3/L3/Off; **R3** | Hidden; core defaults. | Blanket decision |
| Threaded Video, Threaded Audio, Video Threads (`armgba_threaded_video`, `armgba_threaded_audio`, `armgba_video_threads`) | Off/Auto/On/One Frame Late; **Off**. Off/On; **Off**. 1/2; **1** | Hidden; core defaults. | Blanket decision |
| CPU Emulation (`armgba_cpu_core`) | Recompiler/Interpreter; **Recompiler** | Hidden; core default. | **Not discussed** |
| GBA/GBC color correction, GB screen and contrast (`armgba_color_correction`, `armgba_gb_color_correction`, `armgba_gb_screen`, `armgba_gb_contrast`) | Corrections Off/On; **Off**. GB palette: grayscale/measured/Pocket/Light; **grayscale**. Contrast 0%–100%; **50%** | Hidden; Ko Boy's e-ink display pipeline owns presentation. | Blanket decision |
| HD Affine Backgrounds (`armgba_hd_affine`) | Off/2×–8×; **Off** | Hidden; core default. | **Not discussed** |
| GB-on-GBA border and panel (`armgba_gb_advance_border`, `armgba_gb_advance_panel`) | Cropped/Shown; **Cropped**. GBA/GBC panel; **GBA** | Hidden; core defaults. | Blanket decision |
| Frame Skip and Fast-Forward Skip (`armgba_frame_skip`, `armgba_fast_forward_skip`) | Off/Auto/1/2/3; **Off**. Off/On; **On** | Hidden; core defaults. | Blanket decision |
| Layer Selection and visibility toggles (`armgba_layer_selection`, `armgba_layer_bg0`, `armgba_layer_bg1`, `armgba_layer_bg2`, `armgba_layer_bg3`, `armgba_layer_gb_background`, `armgba_layer_gb_window`, `armgba_layer_sprites`) | Off/On; **Off**. Individual layers Shown/Hidden; **Shown** | Developer-only. | Explicit |
| LCD Effects and LCD Ghosting (`armgba_lcd_effects`, `armgba_lcd_ghosting`) | Effects Off/On; **Off**. Ghosting Off/25/50/75/100%; **Off** | Hidden; Ko Boy owns e-ink processing. | Blanket decision |
| Interlaced Rows, LCD Scale, Screen Size (`armgba_lcd_interleave`, `armgba_lcd_grid`, `armgba_lcd_screen`) | Off/On; **Off**. Off/Fit/2×–8×; **Off**. Auto and listed resolutions/custom; **Auto** | Hidden; Ko Boy owns scaling and presentation. | Blanket decision |
| Custom LCD width/height digits (`armgba_lcd_width_hundreds`, `armgba_lcd_width_tens`, `armgba_lcd_width_ones`, `armgba_lcd_height_hundreds`, `armgba_lcd_height_tens`, `armgba_lcd_height_ones`) | Numeric components; defaults form **1280×720** | Hidden; inactive while LCD Scale is Off. | Blanket decision |
| Pixel Gaps, LCD Subpixels, GB Pixel Shadow and distance, LCD Effect Threads (`armgba_lcd_gaps`, `armgba_lcd_subpixels`, `armgba_lcd_shadow`, `armgba_lcd_shadow_distance`, `armgba_lcd_threads`) | Gaps Off/On; **Off**. Subpixels Off–100%; **Off**. Shadow Off/eight directions; **Off**. 0.5–2.5; **1.5**. Threads Auto/1–4; **Auto** | Hidden; Ko Boy owns presentation. | Blanket decision |
| GBA Audio Output, GB Audio Filter, Low-Pass Filter (`armgba_gba_audio_output`, `armgba_gb_audio_filter`, `armgba_low_pass_filter`) | Speaker/Headphones/Off; **Speaker**. Accurate/offset-only/Off; **Accurate**. Off/20/40/60/80%; **Off** | Hidden at core defaults; audio-character controls are explicitly deferred. | Explicit deferment |
| Channel Selection and channel switches (`armgba_channel_selection`, `armgba_channel_pulse1`, `armgba_channel_pulse2`, `armgba_channel_wave`, `armgba_channel_noise`, `armgba_channel_direct_a`, `armgba_channel_direct_b`) | Off/On; **Off**. Individual channels On/Off; **On** | Hidden; core defaults. | **Not discussed** |
| Left Stick as D-Pad (`armgba_left_stick_dpad`) | Off, On; **Off** | Hidden; core default. | Blanket decision |
| Turbo Button Period (`armgba_turbo_period`) | Dynamically generated frame periods; **4** | Hidden; core default. | Blanket decision |
| L2/R2 and L3 behavior (`armgba_l2_r2`, `armgba_l3_button`) | Fast-forward/toggle/turbo; **Fast-forward**. Fast-forward toggle/soft reset/Off; **Fast-forward toggle** | Hidden; core defaults. | Blanket decision |
| Tilt Control (`armgba_gb_tilt`) | Left stick, right stick, D-pad, device motion; **left stick** | Fixed to D-pad; the core consults it only for applicable motion-sensor cartridges. | Explicit |
| Device Motion Rotation (`armgba_motion_rotation`) | 0°, 90°, 180°, 270°; **0°** | Hidden; core default. | Blanket decision |
| Solar Sensor (`armgba_solar_sensor`) | 0–10; **5** | User-facing only when the core reports applicable solar hardware. | Explicit |
| Restart Combo (`armgba_restart_buttons`) | Several combos or Off; **L+R+Start** | Hidden; core default. | Blanket decision |
| Link Mode and Shared Controls (`armgba_link_mode`, `armgba_shared_controls`) | Off/Together/Separate/GameCube; **Off**. Off/Everyone/Pass Around; **Off** | Hidden; link features are outside the current Ko Boy UI. | Blanket decision |
| Link players and Player 2 game (`armgba_link_players`, `armgba_player2_game`) | 2/3/4; **2**. Dynamic game list; **Same Game** | Hidden; inactive with Link Mode Off. | Blanket decision |
| Link cartridge/ports/timing/printer/GameCube address (`armgba_link_cartridge`, `armgba_link_port`, `armgba_gb_link_port`, `armgba_gb_link_timing`, `armgba_gb_printer`, `armgba_gamecube_address`) | Inserted/Removed; **Inserted**. Cable/Wireless; **Cable**. Cable/Four Player; **Cable**. Standard/Exact; **Standard**. Off/On; **Off**. Network/localhost; **Network** | Hidden; link features are outside the current Ko Boy UI. | Blanket decision |
| Link screen layout and controls (`armgba_link_screens`, `armgba_link_l2`, `armgba_r2_follows`, `armgba_switch_controller`, `armgba_player_numbers`) | Layout choices; **Side by Side**. L2 fast-forward/previous; **Fast-forward**. Follow Auto/1–4; **Auto**. Controller combos/Off; **Start+L2+R2**. Numbers Off/On; **On** | Hidden; link features are outside the current Ko Boy UI. | Blanket decision |
| Send/receive/back up/load saves (`armgba_send_save`, `armgba_receive_into`, `armgba_back_up_save`, `armgba_load_save`, `armgba_load_into`) | Dynamic actions/targets; action defaults **Off**, target defaults **Auto/Same Player** | Hidden; Ko Boy owns its save UI and placement. | Blanket decision |
| Netplay progress, new-player saves, Player 1 folder (`armgba_keep_progress`, `armgba_new_save`, `armgba_player1_saves`) | Off/On; **Off**. New/Copy; **New**. Save folder/armGBA folder; **Save folder** | Hidden; Ko Boy owns save placement and does not expose link/netplay. | Blanket decision |
| Backups kept/listed (`armgba_keep_saves`, `armgba_list_saves`) | 5/10/20/All; **10**. 10/20/40/80/126; **10** | Hidden; core defaults. | Blanket decision |
| Safe Saves and Fast Flash Saves (`armgba_safe_saves`, `armgba_fast_flash_saves`) | Off/On; **On**. Off/On; **Off** | Hidden; core defaults. | Blanket decision |

## armNES

Pinned commit: `12655bb17b115ec66fd08e10e02c6fbb53c85370`.

| Core option(s) | Available values; core default | Ko Boy policy | Discussion status |
| --- | --- | --- | --- |
| Region (`armnes_region`) | Auto, NTSC, PAL, Dendy; **Auto** | Hidden; core default/header detection. | Blanket decision |
| PPU Accuracy (`armnes_ppu`) | Fast, Balanced, Exact; **Fast** | Developer-only. | Explicit |
| Overclock (`armnes_overclock`) | Off, 2×/3×/4× vblank, 2×/3× after picture; **Off** | Hidden; core default. | Blanket decision |
| Disk System Fast Loading (`armnes_fds_fast`) | Off, On; **Off** | User-facing only for detected FDS games. | Explicit |
| Game Genie and game-on-top (`armnes_game_genie`, `armnes_par_game`) | Off/On; **Off**. Dynamic game list; **None** | Hidden; core defaults. | Blanket decision |
| Cheat Codes 1–5 (`armnes_cart_code_1`, `armnes_cart_code_2`, `armnes_cart_code_3`, `armnes_cart_code_4`, `armnes_cart_code_5`) | Dynamic code lists; **None** | Hidden; core defaults. | Blanket decision |
| Cart Jumper (`armnes_jumper`) | Auto, 0–23; **Auto** | Hidden; core default. | Blanket decision |
| Palette, NTSC/PAL Filter, Scanlines (`armnes_palette`, `armnes_filter`, `armnes_scanlines`) | Dynamic palette list; **armNES NTSC**. Off/Composite/S-Video; **Off**. Off/Light/Medium/Dark; **Off** | Hidden; Ko Boy owns monochrome e-ink presentation. | Blanket decision |
| Sprite Limit (`armnes_sprite_limit`) | 8 per line, No limit; **8 per line** | User-facing. | Explicit |
| Aspect Ratio and crops (`armnes_aspect`, `armnes_crop_vertical`, `armnes_crop_horizontal`) | 4:3/8:7/square; **4:3**. 0/4/8/12/16; vertical **8**, horizontal **0** | Hidden; Ko Boy currently applies its own NES viewport policy. | Blanket decision |
| Main-channel volume and pan (`armnes_volume_pulse1`, `armnes_pan_pulse1`, `armnes_volume_pulse2`, `armnes_pan_pulse2`, `armnes_volume_triangle`, `armnes_pan_triangle`, `armnes_volume_noise`, `armnes_pan_noise`, `armnes_volume_dmc`, `armnes_pan_dmc`, `armnes_volume_cart`, `armnes_pan_cart`) | Volume 0%–100%; **100%**. Pan Left–Right; **Center** | Hidden; core defaults. | Blanket decision |
| Expansion-voice volume and pan (`armnes_volume_*`, `armnes_pan_*` for VRC6, VRC7, Sunsoft 5B, Namco 163, and MMC5 voices) | Volume 0%–100%; **100%**. Pan Left–Right; **Center** | Hidden; shown by the core only for the applicable chip. | Blanket decision |
| Layer Selection; background/front/behind sprite visibility (`armnes_layer_selection`, `armnes_layer_background`, `armnes_layer_sprites_front`, `armnes_layer_sprites_behind`) | Selection Off/On; **Off**. Layers Shown/Hidden; **Shown** | Developer-only. | Explicit |
| Main-channel switches (`armnes_channel_selection`, `armnes_channel_pulse1`, `armnes_channel_pulse2`, `armnes_channel_triangle`, `armnes_channel_noise`, `armnes_channel_dmc`, `armnes_channel_cart`) | Selection Off/On; **Off**. Individual channels On/Off; **On** | Hidden; core defaults. | **Not discussed** |
| Expansion-voice switches (`armnes_channel_*` for VRC6, VRC7, Sunsoft 5B, Namco 163, and MMC5 voices) | On/Off; **On** | Hidden; shown by the core only for the applicable chip. | **Not discussed** |
| Supervision channel switches (`armnes_channel_sv_square1`, `armnes_channel_sv_square2`, `armnes_channel_sv_noise`, `armnes_channel_sv_samples`) | On/Off; **On** | Hidden; shown by the core only for Supervision games with Channel Selection On. Ko Boy does not route Supervision games. | **Not discussed** |
| Gamate channel switches (`armnes_channel_gm_a`, `armnes_channel_gm_b`, `armnes_channel_gm_c`) | On/Off; **On** | Hidden; shown by the core only for Gamate games with Channel Selection On. Ko Boy does not route Gamate games through armNES. | **Not discussed** |
| Supervision multicart and LCD (`armnes_sv_game`, `armnes_sv_palette`, `armnes_sv_ghosting`) | Games 1–8; **Game 1**. Grey, Game Boy, Game Boy Pocket, Game Boy Light, Amber, Gamate; **Grey**. Off/25%/50%/75%/100%; **Off** | Hidden; core defaults. Game selection is shown by the core only for multicart Supervision games. | **Not discussed** |
| Gamate link cable (`armnes_gm_link`, `armnes_gm_link_timing`, `armnes_gm_link_screen`, `armnes_gm_link_l2`, `armnes_gm_r2_follows`, `armnes_gm_switch_controller`, `armnes_gm_player_numbers`) | Link Mode Off/Together/Separate; **Off**. Timing Standard/Exact; **Standard**. Screen layouts; **Side by Side**. L2 Fast-Forward/Previous Layout; **Fast-Forward**. Follow Auto/1/2; **Auto**. Combos/Off; **Start+L2+R2**. Numbers Off/On; **On** | Hidden at core defaults, so no link cable is attached. Link features are outside the current Ko Boy UI. | **Not discussed** |
| Turbo Button Period (`armnes_turbo_period`) | Dynamically generated frame periods; **4** | Hidden; core default. | **Not discussed** |
| Allow Opposing Directions; Left Stick as D-Pad (`armnes_opposing`, `armnes_left_stick_dpad`) | Off/On; **Off**. Off/On; **On** | Hidden; core defaults. | Blanket decision |
| Four Players; Famicom Expansion Port (`armnes_four_players`, `armnes_expansion`) | Auto/Off/Four Score/Famicom; **Auto**. Auto/None/Paddle/Power Pad/Keyboard; **Auto** | Hidden; core/header defaults. | Blanket decision |
| Zapper input, sensitivity, and stick invert (`armnes_zapper_aim`, `armnes_zapper_sensitivity`, `armnes_stick_invert`) | Stick pan/absolute, light gun, mouse; **stick pan**. 25%–300%; **100%**. Off/horizontal/vertical/both; **Off** | Hidden; core defaults. | Blanket decision |
| Zapper motion mode/range/sensitivity/invert/rotation (`armnes_motion_sensor`, `armnes_motion_range`, `armnes_motion_sensitivity`, `armnes_motion_invert`, `armnes_motion_rotation`) | Off/gyro/accelerometer; **Off**. Whole/Fine; **Whole**. 25%–300%; **100%**. Invert choices; **Off**. 0°/90°/180°/270°; **0°** | Hidden; core defaults. | Blanket decision |
| VS PPU and controller swap (`armnes_vs_ppu`, `armnes_vs_swap`) | Auto and hardware PPUs; **Auto**. Auto/Off/On; **Auto** | Hidden; core/header defaults. | Blanket decision |
| VS DIP switches 1–16 (`armnes_vs_dip1`, `armnes_vs_dip2`, `armnes_vs_dip3`, `armnes_vs_dip4`, `armnes_vs_dip5`, `armnes_vs_dip6`, `armnes_vs_dip7`, `armnes_vs_dip8`, `armnes_vs_dip9`, `armnes_vs_dip10`, `armnes_vs_dip11`, `armnes_vs_dip12`, `armnes_vs_dip13`, `armnes_vs_dip14`, `armnes_vs_dip15`, `armnes_vs_dip16`) | Off, On; **Off** | Hidden; core defaults. | Blanket decision |
| VS DualSystem Screens (`armnes_vs_screens`) | Both/Main/Second; **Both** | Hidden; core default. | Blanket decision |

## superwhizkid

Pinned commit: `9d0dee3f8364660212c23faa72308d2743fae542`.

| Core option | Available values; core default | Ko Boy policy | Discussion status |
| --- | --- | --- | --- |
| Palette (`superwhizkid_palette`) | Original, Gamate LCD, Greyscale, RGB; **Original** | Fixed to Greyscale. | Explicit |
| Contrast (`superwhizkid_contrast`) | 1–16; **8** | Developer-only. | Explicit |
| Audio Output (`superwhizkid_audio_output`) | Stereo, Mono, Built-in Speaker; **Stereo** | User-facing only when Bluetooth audio was connected at game launch. | Explicit |
| Catch quick taps (`superwhizkid_tap_latch`) | Off, On; **Off** | User-facing. | Explicit |
| LCD Ghosting (`superwhizkid_lcd_ghosting`) | Off, 1–5; **Off** | Fixed to Off. | Explicit |
| Redraw Flicker Reduction (`superwhizkid_flicker_reduction`) | Off, 1–3 frames; **Off** | Developer-only; Ko Boy default is 1 frame. | Explicit |
| BIOS Priority (`superwhizkid_bios`) | UMC, Bit, SWK; **UMC** | Fixed to UMC. Upstream falls back UMC and Bit to each other, then SWK; SWK falls back to UMC, then Bit. Ko Boy requires UMC or Bit, so an SWK-only setup is not accepted. | Explicit; SWK value **Not discussed** |
| Skip BIOS intro (`superwhizkid_skip_intro`) | Off, On; **Off** | Fixed to Off. | Explicit |
