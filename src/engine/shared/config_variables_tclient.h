// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Tcme, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Tcme, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Tcme, ScriptName, Len, Def, Save, Desc) ;
#endif

#if defined(CONF_FAMILY_WINDOWS)
MACRO_CONFIG_INT(TcAllowAnyRes, tc_allow_any_res, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Whether to allow any resolution in game when zoom is allowed (buggy on Windows)")
#else
MACRO_CONFIG_INT(TcAllowAnyRes, tc_allow_any_res, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Whether to allow any resolution in game when zoom is allowed (buggy on Windows)")
#endif

MACRO_CONFIG_INT(TcShowChatClient, tc_show_chat_client, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show chat messages from the client such as echo")

MACRO_CONFIG_INT(TcShowFrozenText, tc_frozen_tees_text, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show how many tees in your team are currently frozen. (0 - off, 1 - show alive, 2 - show frozen)")
MACRO_CONFIG_INT(TcShowFrozenHud, tc_frozen_tees_hud, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show frozen tee HUD")
MACRO_CONFIG_INT(TcShowFrozenHudSkins, tc_frozen_tees_hud_skins, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use ninja skin, or darkened skin for frozen tees on hud")

MACRO_CONFIG_INT(TcFrozenHudTeeSize, tc_frozen_tees_size, 15, 8, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of tees in frozen tee hud. (Default : 15)")
MACRO_CONFIG_INT(TcFrozenMaxRows, tc_frozen_tees_max_rows, 1, 1, 6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum number of rows in frozen tee HUD display")
MACRO_CONFIG_INT(TcFrozenHudTeamOnly, tc_frozen_tees_only_inteam, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Only render frozen tee HUD display while in team")

MACRO_CONFIG_INT(TcNameplatePingCircle, tc_nameplate_ping_circle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows a circle to indicate ping in the nameplate")
MACRO_CONFIG_INT(TcNameplateCountry, tc_nameplate_country, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows the country flag in the nameplate")
MACRO_CONFIG_INT(TcNameplateSkins, tc_nameplate_skins, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows skin names in nameplates, good for finding missing skins")

MACRO_CONFIG_INT(TcFakeCtfFlags, tc_fake_ctf_flags, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows fake CTF flags on people (0 = off, 1 = red, 2 = blue)")

MACRO_CONFIG_INT(TcLimitMouseToScreen, tc_limit_mouse_to_screen, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Limit mouse to screen boundaries")
MACRO_CONFIG_INT(TcScaleMouseDistance, tc_scale_mouse_distance, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Improve mouse precision by scaling max distance to 1000")

MACRO_CONFIG_INT(TcHammerRotatesWithCursor, tc_hammer_rotates_with_cursor, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Allow your hammer to rotate like other weapons")

MACRO_CONFIG_INT(TcMiniVoteHud, tc_mini_vote_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "When enabled makes the vote UI small")

// Anti Latency Tools
MACRO_CONFIG_INT(TcRemoveAnti, tc_remove_anti, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Removes some amount of antiping & player prediction in freeze")
MACRO_CONFIG_INT(TcUnfreezeLagTicks, tc_remove_anti_ticks, 5, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "The biggest amount of prediction ticks that are removed")
MACRO_CONFIG_INT(TcUnfreezeLagDelayTicks, tc_remove_anti_delay_ticks, 25, 5, 150, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How many ticks it takes to remove the maximum prediction after being frozen")

MACRO_CONFIG_INT(TcUnpredOthersInFreeze, tc_unpred_others_in_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dont predict other players if you are frozen")
MACRO_CONFIG_INT(TcPredMarginInFreeze, tc_pred_margin_in_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable changing prediction margin while frozen")
MACRO_CONFIG_INT(TcPredMarginInFreezeAmount, tc_pred_margin_in_freeze_amount, 15, 0, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Set what your prediction margin while frozen should be")

MACRO_CONFIG_INT(TcShowOthersGhosts, tc_show_others_ghosts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show ghosts for other players in their unpredicted position")
MACRO_CONFIG_INT(TcSwapGhosts, tc_swap_ghosts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show predicted players as ghost and normal players as unpredicted")
MACRO_CONFIG_INT(TcHideFrozenGhosts, tc_hide_frozen_ghosts, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide Ghosts of other players if they are frozen")

MACRO_CONFIG_INT(TcPredGhostsAlpha, tc_pred_ghosts_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alpha of predicted ghosts (0-100)")
MACRO_CONFIG_INT(TcUnpredGhostsAlpha, tc_unpred_ghosts_alpha, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alpha of unpredicted ghosts (0-100)")
MACRO_CONFIG_INT(TcRenderGhostAsCircle, tc_render_ghost_as_circle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render Ghosts as circles instead of tee")

MACRO_CONFIG_INT(TcShowCenter, tc_show_center, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draws lines to show the center of your screen/hitbox")
MACRO_CONFIG_INT(TcShowCenterWidth, tc_show_center_width, 0, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Center lines width (enabled by tc_show_center)")
MACRO_CONFIG_COL(TcShowCenterColor, tc_show_center_color, 1694498688, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Center lines color (enabled by tc_show_center)") // transparent red

MACRO_CONFIG_INT(TcFastInput, tc_fast_input, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Uses input for prediction before the next tick (automatically inactive while tc_anti_void is on: avoid decides once per tick and fast input would reship the raw input over it)")
MACRO_CONFIG_INT(TcFastInputAmount, tc_fast_input_amount, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How many milliseconds fast input will apply")
MACRO_CONFIG_INT(TcFastInputOthers, tc_fast_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply fast input to other tees")

MACRO_CONFIG_INT(TcAntiPingImproved, tc_antiping_improved, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Different antiping smoothing algorithm, not compatible with cl_antiping_smooth")
MACRO_CONFIG_INT(TcAntiPingNegativeBuffer, tc_antiping_negative_buffer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Helps in Gores. Allows internal certainty value to be negative which causes more conservative prediction")
MACRO_CONFIG_INT(TcAntiPingStableDirection, tc_antiping_stable_direction, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Predicts optimistically along the tees stable axis to reduce delay in gaining overall stability")
MACRO_CONFIG_INT(TcAntiPingUncertaintyScale, tc_antiping_uncertainty_scale, 150, 25, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Determines uncertainty duration as a factor of ping, 100 = 1.0")

MACRO_CONFIG_INT(TcColorFreeze, tc_color_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use skin colors for frozen tees")
MACRO_CONFIG_INT(TcColorFreezeDarken, tc_color_freeze_darken, 90, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Makes color of tees darker when in freeze (0-100)")
MACRO_CONFIG_INT(TcColorFreezeFeet, tc_color_freeze_feet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Also use color for frozen tee feet")

// Revert Variables
MACRO_CONFIG_INT(TcSmoothPredictionMargin, tc_prediction_margin_smooth, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Makes prediction margin transition smooth, causes worse ping jitter adjustment (reverts a DDNet change)")
MACRO_CONFIG_INT(TcFrozenKatana, tc_frozen_katana, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show katana on frozen players (reverts a DDNet change)")
MACRO_CONFIG_INT(TcOldTeamColors, tc_old_team_colors, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use rainbow team colors (reverts a DDNet change)")
MACRO_CONFIG_INT(TcRevertHookLine, tc_revert_hook_line, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Revert hookline tip behavior")

// Outline Variables
MACRO_CONFIG_INT(TcOutline, tc_outline, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draws outlines")
MACRO_CONFIG_INT(TcOutlineEntities, tc_outline_in_entities, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Only show outlines in entities")

MACRO_CONFIG_INT(TcOutlineSolid, tc_outline_solid, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draws outline around hook and unhook")
MACRO_CONFIG_INT(TcOutlineFreeze, tc_outline_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draws outline around freeze and deep")
MACRO_CONFIG_INT(TcOutlineUnfreeze, tc_outline_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draws outline around unfreeze and undeep")
MACRO_CONFIG_INT(TcOutlineKill, tc_outline_kill, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draws outline around kill")
MACRO_CONFIG_INT(TcOutlineTele, tc_outline_tele, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draws outline around teleporters")

MACRO_CONFIG_INT(TcOutlineWidthSolid, tc_outline_width_solid, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Width of outline around hook and unhook")
MACRO_CONFIG_INT(TcOutlineWidthFreeze, tc_outline_width_freeze, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Width of outline around freeze and deep")
MACRO_CONFIG_INT(TcOutlineWidthUnfreeze, tc_outline_width_unfreeze, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Width of outline around unfreeze and undeep")
MACRO_CONFIG_INT(TcOutlineWidthKill, tc_outline_width_kill, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Width of outline around kill")
MACRO_CONFIG_INT(TcOutlineWidthTele, tc_outline_width_tele, 2, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Width of outline around teleporters")

MACRO_CONFIG_COL(TcOutlineColorSolid, tc_outline_color_solid, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Color of outline around hook and unhook") // 255 0 0 0
MACRO_CONFIG_COL(TcOutlineColorFreeze, tc_outline_color_freeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Color of outline around freeze and deep") // 255 0 0 0
MACRO_CONFIG_COL(TcOutlineColorUnfreeze, tc_outline_color_unfreeze, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Color of outline around unfreeze and undeep") // 255 0 0 0
MACRO_CONFIG_COL(TcOutlineColorKill, tc_outline_color_kill, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Color of outline around kill") // 0 0 0
MACRO_CONFIG_COL(TcOutlineColorTele, tc_outline_color_tele, 4294901760, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Color of outline around teleporters") // 255 0 0 0

// Indicator Variables
MACRO_CONFIG_COL(TcIndicatorAlive, tc_indicator_alive, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color of alive tees in player indicator")
MACRO_CONFIG_COL(TcIndicatorFreeze, tc_indicator_freeze, 65407, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color of frozen tees in player indicator")
MACRO_CONFIG_COL(TcIndicatorSaved, tc_indicator_dead, 0, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color of tees who is getting saved in player indicator")
MACRO_CONFIG_INT(TcIndicatorOffset, tc_indicator_offset, 42, 16, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "(16-128) Offset of indicator position")
MACRO_CONFIG_INT(TcIndicatorOffsetMax, tc_indicator_offset_max, 100, 16, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "(16-128) Max indicator offset for variable offset setting")
MACRO_CONFIG_INT(TcIndicatorVariableDistance, tc_indicator_variable_distance, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Indicator circles will be further away the further the tee is")
MACRO_CONFIG_INT(TcIndicatorMaxDistance, tc_indicator_variable_max_distance, 1000, 500, 7000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum tee distance for variable offset")
MACRO_CONFIG_INT(TcIndicatorRadius, tc_indicator_radius, 4, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "(1-16) indicator circle size")
MACRO_CONFIG_INT(TcIndicatorOpacity, tc_indicator_opacity, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Opacity of indicator circles")
MACRO_CONFIG_INT(TcPlayerIndicator, tc_player_indicator, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show radial indicator of other tees")
MACRO_CONFIG_INT(TcPlayerIndicatorFreeze, tc_player_indicator_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Only show frozen tees in indicator")
MACRO_CONFIG_INT(TcIndicatorTeamOnly, tc_indicator_inteam, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Only show indicator while in team")
MACRO_CONFIG_INT(TcIndicatorTees, tc_indicator_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show tees instead of circles")
MACRO_CONFIG_INT(TcIndicatorHideVisible, tc_indicator_hide_visible_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Don't show tees that are on your screen")

// Bind Wheel
MACRO_CONFIG_INT(TcResetBindWheelMouse, tc_reset_bindwheel_mouse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reset position of mouse when opening bindwheel")

// Regex chat matching
MACRO_CONFIG_STR(TcRegexChatIgnore, tc_regex_chat_ignore, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Filters out chat messages based on a regular expression.")

// Misc visual
MACRO_CONFIG_INT(TcWhiteFeet, tc_white_feet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render all feet as perfectly white base color")
MACRO_CONFIG_STR(TcWhiteFeetSkin, tc_white_feet_skin, 255, "x_ninja", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Base skin for white feet")
MACRO_CONFIG_INT(TcRenderWeaponsAsGun, tc_render_weapons_as_gun, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Renders weapons as the gun sprite instead of the weapon, with the exception of hammer and ninja (1 = with hue, 2 = without hue)")
MACRO_CONFIG_INT(TcMovingTilesEntities, tc_moving_tiles_entities, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show moving tiles in entities")

MACRO_CONFIG_INT(TcMiniDebug, tc_mini_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show position and angle")

MACRO_CONFIG_INT(TcNotifyWhenLast, tc_last_notify, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notify when you are last")
MACRO_CONFIG_STR(TcNotifyWhenLastText, tc_last_notify_text, 64, "Last!", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Text for last notify")
MACRO_CONFIG_COL(TcNotifyWhenLastColor, tc_last_notify_color, 256, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color for last notify")
MACRO_CONFIG_INT(TcNotifyWhenLastX, tc_last_notify_x, 20, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal position for last notify as percentage of screen width")
MACRO_CONFIG_INT(TcNotifyWhenLastY, tc_last_notify_y, 1, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical position for last notify as percentage of screen height")
MACRO_CONFIG_INT(TcNotifyWhenLastSize, tc_last_notify_size, 10, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Font size for last notify")

MACRO_CONFIG_INT(TcRenderCursorSpec, tc_cursor_in_spec, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render your gun cursor when spectating in freeview")
MACRO_CONFIG_INT(TcRenderCursorSpecAlpha, tc_cursor_in_spec_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alpha of cursor in freeview")

// MACRO_CONFIG_INT(TcRenderNameplateSpec, tc_render_nameplate_spec, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render nameplates when spectating")

MACRO_CONFIG_INT(TcTinyTees, tc_tiny_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render tees smaller")
MACRO_CONFIG_INT(TcTinyTeeSize, tc_indicator_tees_size, 100, 85, 115, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Define the Size of the Tiny Tee")
MACRO_CONFIG_INT(TcTinyTeesOthers, tc_tiny_tees_others, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render other tees smaller")

MACRO_CONFIG_INT(TcCursorScale, tc_cursor_scale, 100, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Percentage to scale the in game cursor by as a percentage (50 = half, 200 = double)")

// Profiles
MACRO_CONFIG_INT(TcProfileSkin, tc_profile_skin, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply skin in profiles")
MACRO_CONFIG_INT(TcProfileName, tc_profile_name, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply name in profiles")
MACRO_CONFIG_INT(TcProfileClan, tc_profile_clan, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply clan in profiles")
MACRO_CONFIG_INT(TcProfileFlag, tc_profile_flag, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply flag in profiles")
MACRO_CONFIG_INT(TcProfileColors, tc_profile_colors, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply colors in profiles")
MACRO_CONFIG_INT(TcProfileEmote, tc_profile_emote, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply emote in profiles")
MACRO_CONFIG_INT(TcProfileOverwriteClanWithEmpty, tc_profile_overwrite_clan_with_empty, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Overwrite clan name even if profile has an empty clan name")

// Rainbow
MACRO_CONFIG_INT(TcRainbowTees, tc_rainbow_tees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Turn on rainbow client side")
MACRO_CONFIG_INT(TcRainbowHook, tc_rainbow_hook, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rainbow hook")
MACRO_CONFIG_INT(TcRainbowWeapon, tc_rainbow_weapon, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rainbow Weapons")

MACRO_CONFIG_INT(TcRainbowOthers, tc_rainbow_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Turn on rainbow client side for others")
MACRO_CONFIG_INT(TcRainbowMode, tc_rainbow_mode, 1, 1, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rainbow mode (1: rainbow, 2: pulse, 3: darkness, 4: random)")
MACRO_CONFIG_INT(TcRainbowSpeed, tc_rainbow_speed, 100, 0, 10000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rainbow speed as a percentage (50 = half speed, 200 = double speed)")

// War List
MACRO_CONFIG_INT(TcWarList, tc_warlist, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggles war list visuals")
MACRO_CONFIG_INT(TcWarListShowClan, tc_warlist_show_clan_if_war, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show clan in nameplate if there is a war")
MACRO_CONFIG_INT(TcWarListReason, tc_warlist_reason, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show war reason")
MACRO_CONFIG_INT(TcWarListChat, tc_warlist_chat, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show war colors in chat")
MACRO_CONFIG_INT(TcWarListScoreboard, tc_warlist_scoreboard, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show war colors in scoreboard")
MACRO_CONFIG_INT(TcWarListAllowDuplicates, tc_warlist_allow_duplicates, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Allow duplicate war entries")
MACRO_CONFIG_INT(TcWarListSpectate, tc_warlist_spectate, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show war colors in spectator menu")

MACRO_CONFIG_INT(TcWarListIndicator, tc_warlist_indicator, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use warlist for indicator")
MACRO_CONFIG_INT(TcWarListIndicatorColors, tc_warlist_indicator_colors, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show warlist colors instead of freeze colors")
MACRO_CONFIG_INT(TcWarListIndicatorAll, tc_warlist_indicator_all, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show all groups")
MACRO_CONFIG_INT(TcWarListIndicatorEnemy, tc_warlist_indicator_enemy, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show players from the first group")
MACRO_CONFIG_INT(TcWarListIndicatorTeam, tc_warlist_indicator_team, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show players from second group")

// Status Bar
MACRO_CONFIG_INT(TcStatusBar, tc_statusbar, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable status bar")

MACRO_CONFIG_INT(TcStatusBar12HourClock, tc_statusbar_12_hour_clock, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use 12 hour clock in local time")
MACRO_CONFIG_INT(TcStatusBarLocalTimeSeocnds, tc_statusbar_local_time_seconds, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show seconds in local time")
MACRO_CONFIG_INT(TcStatusBarHeight, tc_statusbar_height, 8, 1, 16, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Height of the status bar")

MACRO_CONFIG_COL(TcStatusBarColor, tc_statusbar_color, 3221225472, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar background color")
MACRO_CONFIG_COL(TcStatusBarTextColor, tc_statusbar_text_color, 4278190335, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar text color")
MACRO_CONFIG_INT(TcStatusBarAlpha, tc_statusbar_alpha, 75, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar background alpha")
MACRO_CONFIG_INT(TcStatusBarTextAlpha, tc_statusbar_text_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Status bar text alpha")

MACRO_CONFIG_INT(TcStatusBarLabels, tc_statusbar_labels, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show labels on status bar entries")
MACRO_CONFIG_STR(TcStatusBarScheme, tc_statusbar_scheme, 128, "ac pf r", CFGFLAG_CLIENT | CFGFLAG_SAVE, "The order in which to show status bar items")

// Trails
MACRO_CONFIG_INT(TcTeeTrail, tc_tee_trail, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Tee trails")
MACRO_CONFIG_INT(TcTeeTrailOthers, tc_tee_trail_others, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show tee trails for other players")
MACRO_CONFIG_INT(TcTeeTrailWidth, tc_tee_trail_width, 15, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee trail width")
MACRO_CONFIG_INT(TcTeeTrailLength, tc_tee_trail_length, 25, 5, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee trail length")
MACRO_CONFIG_INT(TcTeeTrailAlpha, tc_tee_trail_alpha, 80, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee trail alpha")
MACRO_CONFIG_COL(TcTeeTrailColor, tc_tee_trail_color, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee trail color")
MACRO_CONFIG_INT(TcTeeTrailTaper, tc_tee_trail_taper, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Taper tee trail over length")
MACRO_CONFIG_INT(TcTeeTrailFade, tc_tee_trail_fade, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fade trail alpha over length")
MACRO_CONFIG_INT(TcTeeTrailColorMode, tc_tee_trail_color_mode, 1, 1, 5, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tee trail color mode (1: Solid color, 2: Current Tee color, 3: Rainbow, 4: Color based on Tee speed, 5: Random)")

// Chat Reply
MACRO_CONFIG_INT(TcAutoReplyMuted, tc_auto_reply_muted, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto reply to muted players with a message")
MACRO_CONFIG_STR(TcAutoReplyMutedMessage, tc_auto_reply_muted_message, 128, "I have muted you", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Message to reply to muted players")
MACRO_CONFIG_INT(TcAutoReplyMinimized, tc_auto_reply_minimized, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto reply when your game is minimized")
MACRO_CONFIG_STR(TcAutoReplyMinimizedMessage, tc_auto_reply_minimized_message, 128, "I am not tabbed in", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Message to reply when your game is minimized")

// Voting
MACRO_CONFIG_INT(TcAutoVoteWhenFar, tc_auto_vote_when_far, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto vote no if you far on a map")
MACRO_CONFIG_STR(TcAutoVoteWhenFarMessage, tc_auto_vote_when_far_message, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Message to send when auto far vote happens, leave empty to disable")
MACRO_CONFIG_INT(TcAutoVoteWhenFarTime, tc_auto_vote_when_far_time, 5, 0, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long until auto vote far happens")

// Font
MACRO_CONFIG_STR(TcCustomFont, tc_custom_font, 255, "DejaVu Sans", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom font face")

// Bg Draw
MACRO_CONFIG_INT(TcBgDrawWidth, tc_bg_draw_width, 5, 1, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Width of background draw strokes")
MACRO_CONFIG_INT(TcBgDrawFadeTime, tc_bg_draw_fade_time, 0, 0, 600, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Time until strokes disappear (0 = never)")
MACRO_CONFIG_INT(TcBgDrawMaxItems, tc_bg_draw_max_items, 128, 0, 2048, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum number of strokes")
MACRO_CONFIG_COL(TcBgDrawColor, tc_bg_draw_color, 14024576, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color of background draw strokes")
MACRO_CONFIG_INT(TcBgDrawAutoSaveLoad, tc_bg_draw_auto_save_load, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically save and load background drawings")

// Translate
MACRO_CONFIG_STR(TcTranslateBackend, tc_translate_backend, 32, "ftapi", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translate backends (ftapi, libretranslate)")
MACRO_CONFIG_STR(TcTranslateTarget, tc_translate_target, 16, "en", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translate target language (must be 2 character ISO 639 code)")
MACRO_CONFIG_STR(TcTranslateEndpoint, tc_translate_endpoint, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "For backends which need it, endpoint to use (must be https)")
MACRO_CONFIG_STR(TcTranslateKey, tc_translate_key, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "For backends which need it, api key to use")
MACRO_CONFIG_INT(TcTranslateAuto, tc_translate_auto, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically translate messages, only some backends support this (FTApi does not)")

// Animations
MACRO_CONFIG_INT(TcAnimateWheelTime, tc_animate_wheel_time, 80, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Duration of emote and bind wheel animations, in milliseconds (0 == no animation, 1000 = 1 second)")

// Pets
MACRO_CONFIG_INT(TcPetShow, tc_pet_show, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show a pet")
MACRO_CONFIG_STR(TcPetSkin, tc_pet_skin, 24, "twinbop", CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "Pet skin")
MACRO_CONFIG_INT(TcPetSize, tc_pet_size, 60, 10, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of the pet as a percentage of a normal player")
MACRO_CONFIG_INT(TcPetAlpha, tc_pet_alpha, 90, 10, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alpha of pet (100 = fully opaque, 50 = half transparent)")

// Change name near finish
MACRO_CONFIG_INT(TcChangeNameNearFinish, tc_change_name_near_finish, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Attempt to change your name when near finish")
MACRO_CONFIG_STR(TcFinishName, tc_finish_name, 16, "nameless tee", CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "Name to change to when near finish when tc_change_name_near_finish is 1")

// Flags
MACRO_CONFIG_INT(TcTClientSettingsTabs, tc_tclient_settings_tabs, 0, 0, 65536, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bit flags to disable settings tabs")

// Volleyball
MACRO_CONFIG_INT(TcVolleyBallBetterBall, tc_volleyball_better_ball, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Make frozen players in volleyball look more like volleyballs (0 = disabled, 1 = in volleyball maps, 2 = always)")
MACRO_CONFIG_STR(TcVolleyBallBetterBallSkin, tc_volleyball_better_ball_skin, 24, "Volleyball", CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "Player skin to use for better volleyball ball")

// Mod
MACRO_CONFIG_INT(TcShowPlayerHitBoxes, tc_show_player_hit_boxes, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show player hit boxes (1 = predicted, 2 = predicted and unpredicted)")
MACRO_CONFIG_INT(TcHideChatBubbles, tc_hide_chat_bubbles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide your own chat bubbles, only works when authed in remote console")
MACRO_CONFIG_INT(TcModWeapon, tc_mod_weapon, 0, 0, 1, CFGFLAG_CLIENT, "Run a command (default kill) when you point and shoot at someone, only works when authed in remote console")
MACRO_CONFIG_STR(TcModWeaponCommand, tc_mod_weapon_command, 256, "rcon kill_pl", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Command to run with tc_mod_weapon, id is appended to end of command")

// Run on join
MACRO_CONFIG_STR(TcExecuteOnConnect, tc_execute_on_connect, 100, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "")
MACRO_CONFIG_STR(TcExecuteOnJoin, tc_execute_on_join, 100, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "")
MACRO_CONFIG_INT(TcExecuteOnJoinDelay, tc_execute_on_join_delay, 2, 7, 50000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tick delay before executing tc_execute_on_join")

// Custom Communities
MACRO_CONFIG_STR(TcCustomCommunitiesUrl, tc_custom_communities_url, 256, "https://raw.githubusercontent.com/SollyBunny/ddnet-custom-communities/refs/heads/main/custom-communities-ddnet-info.json", CFGFLAG_CLIENT | CFGFLAG_SAVE, "URL to fetch custom communities from (must be https), empty to disable")

// Discord RPC
MACRO_CONFIG_INT(TcDiscordRPC, tc_discord_rpc, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle discord RPC (requires restart)") // broken

MACRO_CONFIG_INT(TcShowLocalTimeSeconds, tc_show_local_time_seconds, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show local time in seconds")

// Configs tab UI
MACRO_CONFIG_INT(TcUiShowDDNet, tc_ui_show_ddnet, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show DDNet domain in Configs tab")
MACRO_CONFIG_INT(TcUiShowTClient, tc_ui_show_tclient, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show TClient domain in Configs tab")
MACRO_CONFIG_INT(TcUiOnlyModified, tc_ui_only_modified, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show only modified settings in Configs tab")
MACRO_CONFIG_INT(TcUiCompactList, tc_ui_compact_list, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use compact row layout in Configs tab")

// Avoid (KRX-style Blatant): simulate the real core physics ahead with your current input and
// block only the inputs (hook / direction) the simulation proves deadly, at the last safe tick.
MACRO_CONFIG_INT(TcAntiVoid, tc_anti_void, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid: physics-predicted input blocking. Simulates ahead with your current input and overrides hook/direction only when the prediction says you die otherwise")
MACRO_CONFIG_INT(TcAvoidCheckTicks, tc_avoid_check_ticks, 19, 5, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid: prediction window in ticks (50 = 1s). The simulation runs this far ahead to see a death coming; higher = spots danger earlier but more CPU")
MACRO_CONFIG_INT(TcAvoidKickTicks, tc_avoid_kick_ticks, 1, 1, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid: reaction margin in ticks. The bot overrides only on the last tick where waiting this many more ticks would lose the rescue, so it acts at the very last moment (closest to the void). 1 (default) = most blatant: if you release the hook yourself before that tick it never fires. Raise it if it fails to save you in time (ping/lag)")
MACRO_CONFIG_INT(TcAvoidHook, tc_avoid_hook, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid: allowed to release/block your hook when the simulation says the hook is what kills you")
MACRO_CONFIG_INT(TcAvoidResumeHook, tc_avoid_resume_hook, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid, what happens to your hook after it was force-released: 0 = it stays off until you physically let go of the key. 1 = it comes back on the very next tick, so a single safe tick is enough for it to fly again — but note that the release only saves you while the hook STAYS off, so re-throwing straight back into the same pull is on you. 2 = it comes back as soon as the simulation says holding it no longer kills you, which is the setting where a release actually rescues")
MACRO_CONFIG_INT(TcAvoidResumeCone, tc_avoid_resume_cone, 150, 0, 360, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid: the hook is only ever handed back while your cursor sits inside this cone, measured as the full opening angle around STRAIGHT UP (150 = 75 degrees to either side of vertical). Aiming sideways or down usually means the throw would go back into whatever avoid just pulled you out of, so the hook stays off until you look up again. 360 = no restriction")
MACRO_CONFIG_INT(TcAvoidDirection, tc_avoid_direction, 2, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid steering, AIRBORNE ONLY and FREEZE WALLS ONLY: it reacts to a wall of freeze you would fly into sideways (from either side) and to nothing else — ceilings, pits and kill tiles are the hook release's job. No tick settings: it brakes at the last physics moment that still clears the wall and only hands your keys back once the wall is really behind you. 0 = never touch direction, 1 = only let go of your movement key, 2 = also press the opposite key, which kills sideways speed far faster")
MACRO_CONFIG_INT(TcAvoidNsif, tc_avoid_nsif, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid NSIF (No Safe Input Found): when nothing FULLY saves you, still apply the input that survives longest. OFF by default because in tight terrain that 'best effort' brake often just changes your trajectory into a fall you'd otherwise have cleared — with it off, if there is no real rescue the bot leaves your momentum alone")
MACRO_CONFIG_INT(TcAvoidNsifGain, tc_avoid_nsif_gain, 5, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid NSIF: how many ticks of extra survival a best-effort steer has to buy before it is worth taking over your movement. NSIF fires when nothing fully saves you, so without a floor it will grab your keys for a single tick of delay and still let you die")
// Avoid rescue hook: the last-resort aimbot throw
MACRO_CONFIG_INT(TcRescueHook, tc_rescue_hook, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rescue hook (aimbot): when neither steering nor releasing the hook can save you (free fall into the void), automatically throw the hook at a surface the simulation proves saves you, at the last affordable tick, and hold it until your own input survives again")
MACRO_CONFIG_INT(TcRescueHookSegments, tc_rescue_hook_segments, 12, 4, 48, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rescue hook: how many throw directions are tried across the FOV, nearest-to-cursor first; the first one that survives wins. More = better coverage but more CPU in the dying moment")
MACRO_CONFIG_INT(TcRescueHookFov, tc_rescue_hook_fov, 360, 30, 360, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rescue hook: field of view in degrees, centered on your cursor, that throws may aim within. 360 = any direction (full aimbot); smaller keeps the save near where you were already aiming")
MACRO_CONFIG_INT(TcRescueHookMargin, tc_rescue_hook_margin, 2, 1, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rescue hook: reaction margin in ticks — fires only when waiting this much longer would lose the throw. The sim models the whole hook flight, so it can stay tiny (blatant); raise it for ping/lag headroom")
MACRO_CONFIG_INT(TcRescueHookHoldMode, tc_rescue_hook_hold, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rescue hook hold: once the rescue hook grabs, keep hanging on it until there is real footing under you or you tap the hook key to take over, instead of letting go the moment a drop would merely survive — stops the weak-hook balancing spam over a deadly pit. Capped at ~5s")
MACRO_CONFIG_INT(TcAvoidAfkProtection, tc_avoid_afk_protection, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid: automatically stop intervening after your inputs have been idle for tc_avoid_afk_seconds")
MACRO_CONFIG_INT(TcAvoidAfkSeconds, tc_avoid_afk_seconds, 5, 1, 60, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid AFK protection: seconds of unchanged input before the bot stops intervening")
// Avoid: which tile types count as the void
MACRO_CONFIG_INT(TcAntiVoidTele, tc_anti_void_tele, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid treats teleporter tiles as deadly")
MACRO_CONFIG_INT(TcAntiVoidDeath, tc_anti_void_death, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid treats death (kill) tiles as deadly")
MACRO_CONFIG_INT(TcAntiVoidFreeze, tc_anti_void_freeze, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid considers freeze tiles (recoverable: you lose control and slide, only real death if the slide reaches the void; see tc_avoid_unfreeze)")
MACRO_CONFIG_INT(TcAntiVoidDeepFreeze, tc_anti_void_deep_freeze, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid treats deep freeze tiles as deadly")
MACRO_CONFIG_INT(TcAntiVoidLiveFreeze, tc_anti_void_live_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid treats live freeze tiles as deadly")
MACRO_CONFIG_INT(TcAvoidUnfreeze, tc_avoid_unfreeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid: OFF (default) = strict, any freeze contact counts as death, so the hook is released before it drags you into freeze (full protection; combine with a low Kick-in-ticks so it only fires at the very last moment). ON = lenient, plain freeze is not death and only a frozen slide into a kill tile / deep / off the map is")
MACRO_CONFIG_INT(TcAvoidUnfreezeTicks, tc_avoid_unfreeze_ticks, 150, 20, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid: how many extra ticks to follow a frozen slide/fall past the normal window, to see whether it settles safely or falls into the void below")
MACRO_CONFIG_INT(TcAvoidFreezeMargin, tc_avoid_freeze_margin, 0, 0, 14, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid: pixels of slack around your CENTRE that already count as touching freeze. The game itself freezes you by the centre tile alone, so 0 = exactly the game's rule and you can graze freeze with the edges of your hitbox; 6 (default) bails out that much earlier and covers the ~1px drift of the lightweight sim. Kill tiles and the map edge are always checked against the full hitbox and are not affected")
MACRO_CONFIG_INT(TcAvoidThreadFreeze, tc_avoid_thread_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid: ONLY for maps where freeze is harmless and you dive through it on purpose. ON = do NOT release your hook just because it pulls you through freeze; only release for a REAL kill/deep/edge. WARNING: on a map whose 'void' IS freeze this disables your freeze protection — the hook will happily pull you up into freeze. OFF (default) = freeze counts, so the hook is released before it drags you into it")
MACRO_CONFIG_INT(TcAntiVoidShow, tc_anti_void_show, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid: draw the predicted path and the dangerous tiles around you")
MACRO_CONFIG_INT(TcAntiVoidDebug, tc_anti_void_debug, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avoid debug log: 1 = one detailed report per decision change, 2 = a report every tick while a death is predicted (loud)")
// Anti-void: rocket (grenade) counter
MACRO_CONFIG_INT(TcAntiVoidRocket, tc_anti_void_rocket, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Anti-void: if you have the grenade launcher, auto-fire a rocket toward the void so the explosion knocks you back to safety (works for falling down, flying up, or drifting sideways)")
MACRO_CONFIG_INT(TcAntiVoidRocketDistance, tc_anti_void_rocket_distance, 10, 1, 32000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Anti-void rocket: how close to the void before it fires (the firing 'timing'), stored in HUNDREDTHS of a pixel so 100 = 1px (320px = 1 tile is value 32000); min 1 = 0.01px, i.e. fire at the very last hundredth of a pixel before the edge")
MACRO_CONFIG_INT(TcAntiVoidRocketCooldown, tc_anti_void_rocket_cooldown, 1, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Anti-void rocket: minimum ticks between auto-fired rockets so it doesn't dump all your ammo at once")
MACRO_CONFIG_INT(TcAntiVoidRocketAimVoid, tc_anti_void_rocket_aim_void, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Anti-void rocket aiming: 1 = aim at the nearest void in the direction you're moving (fires up at a ceiling void even while drifting sideways); 0 = fire straight along your velocity vector (inertia)")

// Anti-void: laser self-ricochet counter
MACRO_CONFIG_INT(TcAntiVoidLaser, tc_anti_void_laser, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Anti-void: if you have the laser rifle, auto-fire at the nearest wall so the laser ricochets back into you to save you from death/void")
MACRO_CONFIG_INT(TcAntiVoidLaserDistance, tc_anti_void_laser_distance, 10, 1, 32000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Anti-void laser: how close to the void before it fires (the firing 'timing'), stored in HUNDREDTHS of a pixel so 100 = 1px (320px = 1 tile is value 32000); min 1 = 0.01px")
MACRO_CONFIG_INT(TcAntiVoidLaserCooldown, tc_anti_void_laser_cooldown, 1, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Anti-void laser: minimum ticks between auto-fired lasers")
MACRO_CONFIG_INT(TcAntiVoidLaserDebug, tc_anti_void_laser_debug, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Anti-void laser debug log: 1 = detailed report on detection and firing, 2 = verbose scan logs every tick")

// Weapon spinner (visual only, off by default; does NOT affect aim/hook/fire)
MACRO_CONFIG_INT(TcWeaponSpin, tc_weapon_spin, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Visually spin the weapon sprite (cosmetic only, does not affect your real aim, hook or fire direction)")
MACRO_CONFIG_INT(TcWeaponSpinMode, tc_weapon_spin_mode, 0, 0, 7, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon spinner mode: 0=spin CW, 1=spin CCW, 2=pendulum, 3=random flicks, 4=jitter, 5=snap 8-dir, 6=random drift, 7=chaos")
MACRO_CONFIG_INT(TcWeaponSpinSpeed, tc_weapon_spin_speed, 60, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon spinner: rotation speed / mode rate")
MACRO_CONFIG_INT(TcWeaponSpinRandom, tc_weapon_spin_random, 0, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon spinner: amount of random erratic motion added on top of the spin (0 = smooth spin)")
MACRO_CONFIG_INT(TcWeaponSpinOthers, tc_weapon_spin_others, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon spinner: also spin other players' weapons (visual only on your screen)")
MACRO_CONFIG_INT(TcWeaponSpinReal, tc_weapon_spin_real, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Weapon spinner: send the spinning aim to the server so OTHER players also see your weapon spinning. Real aim is kept on the exact ticks you hook or fire so those still go where you point")

// Hook Aim
MACRO_CONFIG_INT(TcHookAim, tc_hook_aim, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "When hooking, snap aim toward the nearest player within tc_hook_aim_angle degrees")
MACRO_CONFIG_INT(TcHookAimAngle, tc_hook_aim_angle, 30, 1, 180, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum angle in degrees to snap the hook toward a nearby player (requires tc_hook_aim)")

// Balancer: when a nearby tee is hovering over the void, auto-correct your left/right movement so you sit
// perfectly centered on top of their head instead of sliding off the rounded hitbox.
MACRO_CONFIG_INT(TcBalancer, tc_balancer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Balancer: while standing on a tee that is over the void, only when you start to slide off the head it nudges you back to center so you don't fall in. Does nothing when the tee is not over the void, and leaves your movement free while you are not near the edge (bindable via tc_balancer_toggle)")
MACRO_CONFIG_INT(TcBalancerDistance, tc_balancer_distance, 96, 16, 640, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Balancer: max distance in pixels to the tee you are balancing on for the balancer to engage (32px = 1 tile)")
MACRO_CONFIG_INT(TcBalancerEdge, tc_balancer_edge, 16, 0, 28, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Balancer: how far off-center (pixels) you may drift on the head before the balancer steers you back. Smaller = earlier/stricter rescue, larger = more freedom; inside this zone your movement is untouched (head radius is 28px)")
MACRO_CONFIG_INT(TcBalancerVoidDepth, tc_balancer_void_depth, 8, 1, 40, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Balancer: how many tiles below the tee to scan when deciding if it is 'in the void'. The tee counts as in the void unless safe solid ground is found within this depth (death/freeze tiles or the map edge count as void)")
MACRO_CONFIG_INT(TcBalancerOnlyAbove, tc_balancer_only_above, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Balancer: only engage when you are above the tee, so you balance on their head instead of being dragged sideways")
MACRO_CONFIG_INT(TcBalancerDisableRocket, tc_balancer_disable_rocket, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Balancer: while the balancer is engaged on a tee, suppress the rocket anti-void (tc_anti_void_rocket) so an auto-fired rocket doesn't blow you off the head")
MACRO_CONFIG_INT(TcBalancerDebug, tc_balancer_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Balancer: log to console when it locks onto an in-void tee and when it rescues you, to help tuning")

// Hole assist: while activated via the +tc_hole_assist bind (any key the user picks), auto-detect the
// nearest narrow gap in the walls around you and press left/right/stop (counter-strafe) so your tee ends up
// centered on it. Only ever touches the horizontal direction; jump and hook stay under your control.
MACRO_CONFIG_INT(TcHoleAssist, tc_hole_assist, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hole assist: while the +tc_hole_assist bind is active, auto-steer left/right and brake so your tee stops perfectly aligned with the nearest narrow gap. Only controls horizontal movement; you handle jump/hook")
MACRO_CONFIG_INT(TcHoleAssistHold, tc_hole_assist_hold, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hole assist activation mode: 1 = active only while the bound key is held down, 0 = pressing the key toggles it on/off")
MACRO_CONFIG_INT(TcHoleAssistBrake, tc_hole_assist_brake, 10, 2, 60, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hole assist braking point (in tenths): the tee starts counter-strafing when its distance to the gap drops below this many tenths of its horizontal speed. Lower = brakes later / carries more speed (stops closer), higher = brakes earlier. Raise if you overshoot, lower if it brakes too soon and you fall short")

// Safety while chatting: the server ignores movement/fire from a player whose input is flagged as chatting,
// so the safety features (anti-void/balancer/rocket) normally stop working the moment you open the chat.
// When this is on, the client sends the input as 'playing' instead while a safety feature is enabled, so they
// keep working while you type. Side effect: others won't see your "typing" bubble while a safety feature is on.
MACRO_CONFIG_INT(TcSafetyInChat, tc_safety_in_chat, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keep the safety features (anti-void/balancer/rocket) working while the chat is open (sends input as playing; hides your typing bubble while a safety feature is on)")

// Proxy: route the game's UDP traffic for joining servers through a proxy.
// SOCKS5 needs a proxy that supports UDP ASSOCIATE (RFC 1928); Shadowsocks uses AEAD UDP relay.
MACRO_CONFIG_INT(TcSocks5, tc_socks5, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Route the game connection through a proxy (see tc_socks5_type). Apply with net_reset")
MACRO_CONFIG_INT(TcSocks5Type, tc_socks5_type, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Proxy protocol: 0 = SOCKS5 (UDP ASSOCIATE), 1 = Shadowsocks (AEAD)")
MACRO_CONFIG_STR(TcSocks5Host, tc_socks5_host, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Proxy host (ip address or domain)")
MACRO_CONFIG_INT(TcSocks5Port, tc_socks5_port, 1080, 1, 65535, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Proxy port")
MACRO_CONFIG_STR(TcSocks5User, tc_socks5_user, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "SOCKS5 username (leave empty for no authentication; unused for Shadowsocks)")
MACRO_CONFIG_STR(TcSocks5Pass, tc_socks5_pass, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Proxy password (SOCKS5 auth password or Shadowsocks password)")
MACRO_CONFIG_STR(TcSocks5Method, tc_socks5_method, 32, "chacha20-ietf-poly1305", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shadowsocks cipher: chacha20-ietf-poly1305, aes-256-gcm or aes-128-gcm")

// Dummy Info
MACRO_CONFIG_INT(TcShowhudDummyPosition, tc_showhud_dummy_position, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show ingame HUD (Dummy Position)")
MACRO_CONFIG_INT(TcShowhudDummySpeed, tc_showhud_dummy_speed, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show ingame HUD (Dummy Speed)")
MACRO_CONFIG_INT(TcShowhudDummyAngle, tc_showhud_dummy_angle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show ingame HUD (Dummy Aim Angle)")
